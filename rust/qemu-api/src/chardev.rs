// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for character devices
//!
//! Character devices in QEMU can run under the big QEMU lock or in a separate
//! `GMainContext`. Here we only support the former, because the bindings
//! enforce that the BQL is taken whenever the functions in [`CharBackend`] are
//! called.

use std::{
    ffi::CStr,
    fmt::{self, Debug},
    io::{self, ErrorKind, Write},
    marker::PhantomPinned,
    os::raw::{c_int, c_void},
    ptr::addr_of_mut,
    slice,
};

use crate::{
    bindings,
    callbacks::FnCall,
    cell::{BqlRefMut, Opaque},
    prelude::*,
};

/// A safe wrapper around [`bindings::Chardev`].
#[repr(transparent)]
#[derive(qemu_api_macros::Wrapper)]
pub struct Chardev(Opaque<bindings::Chardev>);

pub type ChardevClass = bindings::ChardevClass;
pub type Event = bindings::QEMUChrEvent;

/// A safe wrapper around [`bindings::CharBackend`], denoting the character
/// back-end that is used for example by a device.  Compared to the
/// underlying C struct it adds BQL protection, and is marked as pinned
/// because the QOM object ([`bindings::Chardev`]) contains a pointer to
/// the `CharBackend`.
pub struct CharBackend {
    inner: BqlRefCell<bindings::CharBackend>,
    _pin: PhantomPinned,
}

impl Write for BqlRefMut<'_, bindings::CharBackend> {
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }

    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let chr: &mut bindings::CharBackend = self;

        let len = buf.len().try_into().unwrap();
        let r = unsafe { bindings::qemu_chr_fe_write(addr_of_mut!(*chr), buf.as_ptr(), len) };
        errno::into_io_result(r).map(|cnt| cnt as usize)
    }

    fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        let chr: &mut bindings::CharBackend = self;

        let len = buf.len().try_into().unwrap();
        let r = unsafe { bindings::qemu_chr_fe_write_all(addr_of_mut!(*chr), buf.as_ptr(), len) };
        errno::into_io_result(r).and_then(|cnt| {
            if cnt as usize == buf.len() {
                Ok(())
            } else {
                Err(ErrorKind::WriteZero.into())
            }
        })
    }
}

impl Debug for CharBackend {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // SAFETY: accessed just to print the values
        let chr = self.inner.as_ptr();
        Debug::fmt(unsafe { &*chr }, f)
    }
}

// FIXME: use something like PinnedDrop from the pinned_init crate
impl Drop for CharBackend {
    fn drop(&mut self) {
        self.disable_handlers();
    }
}

impl CharBackend {
    /// Enable the front-end's character device handlers, if there is an
    /// associated `Chardev`.
    pub fn enable_handlers<
        'chardev,
        'owner: 'chardev,
        T,
        CanReceiveFn: for<'a> FnCall<(&'a T,), u32>,
        ReceiveFn: for<'a, 'b> FnCall<(&'a T, &'b [u8])>,
        EventFn: for<'a> FnCall<(&'a T, Event)>,
    >(
        // When "self" is dropped, the handlers are automatically disabled.
        // However, this is not necessarily true if the owner is dropped.
        // So require the owner to outlive the character device.
        &'chardev self,
        owner: &'owner T,
        _can_receive: CanReceiveFn,
        _receive: ReceiveFn,
        _event: EventFn,
    ) {
        unsafe extern "C" fn rust_can_receive_cb<T, F: for<'a> FnCall<(&'a T,), u32>>(
            opaque: *mut c_void,
        ) -> c_int {
            // SAFETY: the values are safe according to the contract of
            // enable_handlers() and qemu_chr_fe_set_handlers()
            let owner: &T = unsafe { &*(opaque.cast::<T>()) };
            let r = F::call((owner,));
            r.try_into().unwrap()
        }

        unsafe extern "C" fn rust_receive_cb<T, F: for<'a, 'b> FnCall<(&'a T, &'b [u8])>>(
            opaque: *mut c_void,
            buf: *const u8,
            size: c_int,
        ) {
            // SAFETY: the values are safe according to the contract of
            // enable_handlers() and qemu_chr_fe_set_handlers()
            let owner: &T = unsafe { &*(opaque.cast::<T>()) };
            let buf = unsafe { slice::from_raw_parts(buf, size.try_into().unwrap()) };
            F::call((owner, buf))
        }

        unsafe extern "C" fn rust_event_cb<T, F: for<'a> FnCall<(&'a T, Event)>>(
            opaque: *mut c_void,
            event: Event,
        ) {
            // SAFETY: the values are safe according to the contract of
            // enable_handlers() and qemu_chr_fe_set_handlers()
            let owner: &T = unsafe { &*(opaque.cast::<T>()) };
            F::call((owner, event))
        }

        let _: () = CanReceiveFn::ASSERT_IS_SOME;
        let receive_cb: Option<unsafe extern "C" fn(*mut c_void, *const u8, c_int)> =
            if ReceiveFn::is_some() {
                Some(rust_receive_cb::<T, ReceiveFn>)
            } else {
                None
            };
        let event_cb: Option<unsafe extern "C" fn(*mut c_void, Event)> = if EventFn::is_some() {
            Some(rust_event_cb::<T, EventFn>)
        } else {
            None
        };

        let mut chr = self.inner.borrow_mut();
        // SAFETY: the borrow promises that the BQL is taken
        unsafe {
            bindings::qemu_chr_fe_set_handlers(
                addr_of_mut!(*chr),
                Some(rust_can_receive_cb::<T, CanReceiveFn>),
                receive_cb,
                event_cb,
                None,
                (owner as *const T as *mut T).cast::<c_void>(),
                core::ptr::null_mut(),
                true,
            );
        }
    }

    /// Disable the front-end's character device handlers.
    pub fn disable_handlers(&self) {
        let mut chr = self.inner.borrow_mut();
        // SAFETY: the borrow promises that the BQL is taken
        unsafe {
            bindings::qemu_chr_fe_set_handlers(
                addr_of_mut!(*chr),
                None,
                None,
                None,
                None,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                true,
            );
        }
    }

    /// Notify that the frontend is ready to receive data.
    pub fn accept_input(&self) {
        let mut chr = self.inner.borrow_mut();
        // SAFETY: the borrow promises that the BQL is taken
        unsafe { bindings::qemu_chr_fe_accept_input(addr_of_mut!(*chr)) }
    }

    /// Temporarily borrow the character device, allowing it to be used
    /// as an implementor of `Write`.  Note that it is not valid to drop
    /// the big QEMU lock while the character device is borrowed, as
    /// that might cause C code to write to the character device.
    pub fn borrow_mut(&self) -> impl Write + '_ {
        self.inner.borrow_mut()
    }

    /// Send a continuous stream of zero bits on the line if `enabled` is
    /// true, or a short stream if `enabled` is false.
    pub fn send_break(&self, long: bool) -> io::Result<()> {
        let mut chr = self.inner.borrow_mut();
        let mut duration: c_int = long.into();
        // SAFETY: the borrow promises that the BQL is taken
        let r = unsafe {
            bindings::qemu_chr_fe_ioctl(
                addr_of_mut!(*chr),
                bindings::CHR_IOCTL_SERIAL_SET_BREAK as i32,
                addr_of_mut!(duration).cast::<c_void>(),
            )
        };

        errno::into_io_result(r).map(|_| ())
    }

    /// Write data to a character backend from the front end.  This function
    /// will send data from the front end to the back end.  Unlike
    /// `write`, this function will block if the back end cannot
    /// consume all of the data attempted to be written.
    ///
    /// Returns the number of bytes consumed (0 if no associated Chardev) or an
    /// error.
    pub fn write(&self, buf: &[u8]) -> io::Result<usize> {
        let len = buf.len().try_into().unwrap();
        // SAFETY: qemu_chr_fe_write is thread-safe
        let r = unsafe { bindings::qemu_chr_fe_write(self.inner.as_ptr(), buf.as_ptr(), len) };
        errno::into_io_result(r).map(|cnt| cnt as usize)
    }

    /// Write data to a character backend from the front end.  This function
    /// will send data from the front end to the back end.  Unlike
    /// `write`, this function will block if the back end cannot
    /// consume all of the data attempted to be written.
    ///
    /// Returns the number of bytes consumed (0 if no associated Chardev) or an
    /// error.
    pub fn write_all(&self, buf: &[u8]) -> io::Result<()> {
        let len = buf.len().try_into().unwrap();
        // SAFETY: qemu_chr_fe_write_all is thread-safe
        let r = unsafe { bindings::qemu_chr_fe_write_all(self.inner.as_ptr(), buf.as_ptr(), len) };
        errno::into_io_result(r).and_then(|cnt| {
            if cnt as usize == buf.len() {
                Ok(())
            } else {
                Err(ErrorKind::WriteZero.into())
            }
        })
    }
}

unsafe impl ObjectType for Chardev {
    type Class = ChardevClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_CHARDEV) };
}
qom_isa!(Chardev: Object);
