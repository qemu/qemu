// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings to access QOM functionality from Rust.
//!
//! The QEMU Object Model (QOM) provides inheritance and dynamic typing for QEMU
//! devices. This module makes QOM's features available in Rust through three
//! main mechanisms:
//!
//! * Automatic creation and registration of `TypeInfo` for classes that are
//!   written in Rust, as well as mapping between Rust traits and QOM vtables.
//!
//! * Type-safe casting between parent and child classes, through the [`IsA`]
//!   trait and methods such as [`upcast`](ObjectCast::upcast) and
//!   [`downcast`](ObjectCast::downcast).
//!
//! * Automatic delegation of parent class methods to child classes. When a
//!   trait uses [`IsA`] as a bound, its contents become available to all child
//!   classes through blanket implementations. This works both for class methods
//!   and for instance methods accessed through references or smart pointers.
//!
//! # Structure of a class
//!
//! A leaf class only needs a struct holding instance state. The struct must
//! implement the [`ObjectType`] and [`IsA`] traits, as well as any `*Impl`
//! traits that exist for its superclasses.
//!
//! If a class has subclasses, it will also provide a struct for instance data,
//! with the same characteristics as for concrete classes, but it also needs
//! additional components to support virtual methods:
//!
//! * a struct for class data, for example `DeviceClass`. This corresponds to
//!   the C "class struct" and holds the vtable that is used by instances of the
//!   class and its subclasses. It must start with its parent's class struct.
//!
//! * a trait for virtual method implementations, for example `DeviceImpl`.
//!   Child classes implement this trait to provide their own behavior for
//!   virtual methods. The trait's methods take `&self` to access instance data.
//!
//! * an implementation of [`ClassInitImpl`], for example
//!   `ClassInitImpl<DeviceClass>`. This fills the vtable in the class struct;
//!   the source for this is the `*Impl` trait; the associated consts and
//!   functions if needed are wrapped to map C types into Rust types.
//!
//! * a trait for instance methods, for example `DeviceMethods`. This trait is
//!   automatically implemented for any reference or smart pointer to a device
//!   instance.  It calls into the vtable provides access across all subclasses
//!   to methods defined for the class.
//!
//! * optionally, a trait for class methods, for example `DeviceClassMethods`.
//!   This provides access to class-wide functionality that doesn't depend on
//!   instance data. Like instance methods, these are automatically inherited by
//!   child classes.

use std::{
    ffi::CStr,
    fmt,
    ops::{Deref, DerefMut},
    os::raw::c_void,
    ptr::NonNull,
};

pub use bindings::{Object, ObjectClass};

use crate::bindings::{self, object_dynamic_cast, object_get_class, object_get_typename, TypeInfo};

/// Marker trait: `Self` can be statically upcasted to `P` (i.e. `P` is a direct
/// or indirect parent of `Self`).
///
/// # Safety
///
/// The struct `Self` must be `#[repr(C)]` and must begin, directly or
/// indirectly, with a field of type `P`.  This ensures that invalid casts,
/// which rely on `IsA<>` for static checking, are rejected at compile time.
pub unsafe trait IsA<P: ObjectType>: ObjectType {}

// SAFETY: it is always safe to cast to your own type
unsafe impl<T: ObjectType> IsA<T> for T {}

/// Macro to mark superclasses of QOM classes.  This enables type-safe
/// up- and downcasting.
///
/// # Safety
///
/// This macro is a thin wrapper around the [`IsA`] trait and performs
/// no checking whatsoever of what is declared.  It is the caller's
/// responsibility to have $struct begin, directly or indirectly, with
/// a field of type `$parent`.
#[macro_export]
macro_rules! qom_isa {
    ($struct:ty : $($parent:ty),* ) => {
        $(
            // SAFETY: it is the caller responsibility to have $parent as the
            // first field
            unsafe impl $crate::qom::IsA<$parent> for $struct {}

            impl AsRef<$parent> for $struct {
                fn as_ref(&self) -> &$parent {
                    // SAFETY: follows the same rules as for IsA<U>, which is
                    // declared above.
                    let ptr: *const Self = self;
                    unsafe { &*ptr.cast::<$parent>() }
                }
            }
        )*
    };
}

/// This is the same as [`ManuallyDrop<T>`](std::mem::ManuallyDrop), though
/// it hides the standard methods of `ManuallyDrop`.
///
/// The first field of an `ObjectType` must be of type `ParentField<T>`.
/// (Technically, this is only necessary if there is at least one Rust
/// superclass in the hierarchy).  This is to ensure that the parent field is
/// dropped after the subclass; this drop order is enforced by the C
/// `object_deinit` function.
///
/// # Examples
///
/// ```ignore
/// #[repr(C)]
/// #[derive(qemu_api_macros::Object)]
/// pub struct MyDevice {
///     parent: ParentField<DeviceState>,
///     ...
/// }
/// ```
#[derive(Debug)]
#[repr(transparent)]
pub struct ParentField<T: ObjectType>(std::mem::ManuallyDrop<T>);

impl<T: ObjectType> Deref for ParentField<T> {
    type Target = T;

    #[inline(always)]
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T: ObjectType> DerefMut for ParentField<T> {
    #[inline(always)]
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl<T: fmt::Display + ObjectType> fmt::Display for ParentField<T> {
    #[inline(always)]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        self.0.fmt(f)
    }
}

unsafe extern "C" fn rust_instance_init<T: ObjectImpl>(obj: *mut Object) {
    let mut state = NonNull::new(obj).unwrap().cast::<T>();
    // SAFETY: obj is an instance of T, since rust_instance_init<T>
    // is called from QOM core as the instance_init function
    // for class T
    unsafe {
        T::INSTANCE_INIT.unwrap()(state.as_mut());
    }
}

unsafe extern "C" fn rust_instance_post_init<T: ObjectImpl>(obj: *mut Object) {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    // SAFETY: obj is an instance of T, since rust_instance_post_init<T>
    // is called from QOM core as the instance_post_init function
    // for class T
    T::INSTANCE_POST_INIT.unwrap()(unsafe { state.as_ref() });
}

unsafe extern "C" fn rust_class_init<T: ObjectType + ClassInitImpl<T::Class>>(
    klass: *mut ObjectClass,
    _data: *mut c_void,
) {
    let mut klass = NonNull::new(klass)
        .unwrap()
        .cast::<<T as ObjectType>::Class>();
    // SAFETY: klass is a T::Class, since rust_class_init<T>
    // is called from QOM core as the class_init function
    // for class T
    T::class_init(unsafe { klass.as_mut() })
}

unsafe extern "C" fn drop_object<T: ObjectImpl>(obj: *mut Object) {
    // SAFETY: obj is an instance of T, since drop_object<T> is called
    // from the QOM core function object_deinit() as the instance_finalize
    // function for class T.  Note that while object_deinit() will drop the
    // superclass field separately after this function returns, `T` must
    // implement the unsafe trait ObjectType; the safety rules for the
    // trait mandate that the parent field is manually dropped.
    unsafe { std::ptr::drop_in_place(obj.cast::<T>()) }
}

/// Trait exposed by all structs corresponding to QOM objects.
///
/// # Safety
///
/// For classes declared in C:
///
/// - `Class` and `TYPE` must match the data in the `TypeInfo`;
///
/// - the first field of the struct must be of the instance type corresponding
///   to the superclass, as declared in the `TypeInfo`
///
/// - likewise, the first field of the `Class` struct must be of the class type
///   corresponding to the superclass
///
/// For classes declared in Rust and implementing [`ObjectImpl`]:
///
/// - the struct must be `#[repr(C)]`;
///
/// - the first field of the struct must be of type
///   [`ParentField<T>`](ParentField), where `T` is the parent type
///   [`ObjectImpl::ParentType`]
///
/// - the first field of the `Class` must be of the class struct corresponding
///   to the superclass, which is `ObjectImpl::ParentType::Class`. `ParentField`
///   is not needed here.
///
/// In both cases, having a separate class type is not necessary if the subclass
/// does not add any field.
pub unsafe trait ObjectType: Sized {
    /// The QOM class object corresponding to this struct.  This is used
    /// to automatically generate a `class_init` method.
    type Class;

    /// The name of the type, which can be passed to `object_new()` to
    /// generate an instance of this type.
    const TYPE_NAME: &'static CStr;

    /// Return the receiver as an Object.  This is always safe, even
    /// if this type represents an interface.
    fn as_object(&self) -> &Object {
        unsafe { &*self.as_object_ptr() }
    }

    /// Return the receiver as a const raw pointer to Object.
    /// This is preferrable to `as_object_mut_ptr()` if a C
    /// function only needs a `const Object *`.
    fn as_object_ptr(&self) -> *const Object {
        self.as_ptr().cast()
    }

    /// Return the receiver as a mutable raw pointer to Object.
    ///
    /// # Safety
    ///
    /// This cast is always safe, but because the result is mutable
    /// and the incoming reference is not, this should only be used
    /// for calls to C functions, and only if needed.
    unsafe fn as_object_mut_ptr(&self) -> *mut Object {
        self.as_object_ptr() as *mut _
    }
}

/// This trait provides safe casting operations for QOM objects to raw pointers,
/// to be used for example for FFI. The trait can be applied to any kind of
/// reference or smart pointers, and enforces correctness through the [`IsA`]
/// trait.
pub trait ObjectDeref: Deref
where
    Self::Target: ObjectType,
{
    /// Convert to a const Rust pointer, to be used for example for FFI.
    /// The target pointer type must be the type of `self` or a superclass
    fn as_ptr<U: ObjectType>(&self) -> *const U
    where
        Self::Target: IsA<U>,
    {
        let ptr: *const Self::Target = self.deref();
        ptr.cast::<U>()
    }

    /// Convert to a mutable Rust pointer, to be used for example for FFI.
    /// The target pointer type must be the type of `self` or a superclass.
    /// Used to implement interior mutability for objects.
    ///
    /// # Safety
    ///
    /// This method is unsafe because it overrides const-ness of `&self`.
    /// Bindings to C APIs will use it a lot, but otherwise it should not
    /// be necessary.
    unsafe fn as_mut_ptr<U: ObjectType>(&self) -> *mut U
    where
        Self::Target: IsA<U>,
    {
        #[allow(clippy::as_ptr_cast_mut)]
        {
            self.as_ptr::<U>() as *mut _
        }
    }
}

/// Trait that adds extra functionality for `&T` where `T` is a QOM
/// object type.  Allows conversion to/from C objects in generic code.
pub trait ObjectCast: ObjectDeref + Copy
where
    Self::Target: ObjectType,
{
    /// Safely convert from a derived type to one of its parent types.
    ///
    /// This is always safe; the [`IsA`] trait provides static verification
    /// trait that `Self` dereferences to `U` or a child of `U`.
    fn upcast<'a, U: ObjectType>(self) -> &'a U
    where
        Self::Target: IsA<U>,
        Self: 'a,
    {
        // SAFETY: soundness is declared via IsA<U>, which is an unsafe trait
        unsafe { self.unsafe_cast::<U>() }
    }

    /// Attempt to convert to a derived type.
    ///
    /// Returns `None` if the object is not actually of type `U`. This is
    /// verified at runtime by checking the object's type information.
    fn downcast<'a, U: IsA<Self::Target>>(self) -> Option<&'a U>
    where
        Self: 'a,
    {
        self.dynamic_cast::<U>()
    }

    /// Attempt to convert between any two types in the QOM hierarchy.
    ///
    /// Returns `None` if the object is not actually of type `U`. This is
    /// verified at runtime by checking the object's type information.
    fn dynamic_cast<'a, U: ObjectType>(self) -> Option<&'a U>
    where
        Self: 'a,
    {
        unsafe {
            // SAFETY: upcasting to Object is always valid, and the
            // return type is either NULL or the argument itself
            let result: *const U =
                object_dynamic_cast(self.as_object_mut_ptr(), U::TYPE_NAME.as_ptr()).cast();

            result.as_ref()
        }
    }

    /// Convert to any QOM type without verification.
    ///
    /// # Safety
    ///
    /// What safety? You need to know yourself that the cast is correct; only
    /// use when performance is paramount.  It is still better than a raw
    /// pointer `cast()`, which does not even check that you remain in the
    /// realm of QOM `ObjectType`s.
    ///
    /// `unsafe_cast::<Object>()` is always safe.
    unsafe fn unsafe_cast<'a, U: ObjectType>(self) -> &'a U
    where
        Self: 'a,
    {
        unsafe { &*(self.as_ptr::<Self::Target>().cast::<U>()) }
    }
}

impl<T: ObjectType> ObjectDeref for &T {}
impl<T: ObjectType> ObjectCast for &T {}

/// Trait for mutable type casting operations in the QOM hierarchy.
///
/// This trait provides the mutable counterparts to [`ObjectCast`]'s conversion
/// functions. Unlike `ObjectCast`, this trait returns `Result` for fallible
/// conversions to preserve the original smart pointer if the cast fails. This
/// is necessary because mutable references cannot be copied, so a failed cast
/// must return ownership of the original reference. For example:
///
/// ```ignore
/// let mut dev = get_device();
/// // If this fails, we need the original `dev` back to try something else
/// match dev.dynamic_cast_mut::<FooDevice>() {
///    Ok(foodev) => /* use foodev */,
///    Err(dev) => /* still have ownership of dev */
/// }
/// ```
pub trait ObjectCastMut: Sized + ObjectDeref + DerefMut
where
    Self::Target: ObjectType,
{
    /// Safely convert from a derived type to one of its parent types.
    ///
    /// This is always safe; the [`IsA`] trait provides static verification
    /// that `Self` dereferences to `U` or a child of `U`.
    fn upcast_mut<'a, U: ObjectType>(self) -> &'a mut U
    where
        Self::Target: IsA<U>,
        Self: 'a,
    {
        // SAFETY: soundness is declared via IsA<U>, which is an unsafe trait
        unsafe { self.unsafe_cast_mut::<U>() }
    }

    /// Attempt to convert to a derived type.
    ///
    /// Returns `Ok(..)` if the object is of type `U`, or `Err(self)` if the
    /// object if the conversion failed. This is verified at runtime by
    /// checking the object's type information.
    fn downcast_mut<'a, U: IsA<Self::Target>>(self) -> Result<&'a mut U, Self>
    where
        Self: 'a,
    {
        self.dynamic_cast_mut::<U>()
    }

    /// Attempt to convert between any two types in the QOM hierarchy.
    ///
    /// Returns `Ok(..)` if the object is of type `U`, or `Err(self)` if the
    /// object if the conversion failed. This is verified at runtime by
    /// checking the object's type information.
    fn dynamic_cast_mut<'a, U: ObjectType>(self) -> Result<&'a mut U, Self>
    where
        Self: 'a,
    {
        unsafe {
            // SAFETY: upcasting to Object is always valid, and the
            // return type is either NULL or the argument itself
            let result: *mut U =
                object_dynamic_cast(self.as_object_mut_ptr(), U::TYPE_NAME.as_ptr()).cast();

            result.as_mut().ok_or(self)
        }
    }

    /// Convert to any QOM type without verification.
    ///
    /// # Safety
    ///
    /// What safety? You need to know yourself that the cast is correct; only
    /// use when performance is paramount.  It is still better than a raw
    /// pointer `cast()`, which does not even check that you remain in the
    /// realm of QOM `ObjectType`s.
    ///
    /// `unsafe_cast::<Object>()` is always safe.
    unsafe fn unsafe_cast_mut<'a, U: ObjectType>(self) -> &'a mut U
    where
        Self: 'a,
    {
        unsafe { &mut *self.as_mut_ptr::<Self::Target>().cast::<U>() }
    }
}

impl<T: ObjectType> ObjectDeref for &mut T {}
impl<T: ObjectType> ObjectCastMut for &mut T {}

/// Trait a type must implement to be registered with QEMU.
pub trait ObjectImpl: ObjectType + ClassInitImpl<Self::Class> {
    /// The parent of the type.  This should match the first field of the
    /// struct that implements `ObjectImpl`, minus the `ParentField<_>` wrapper.
    type ParentType: ObjectType;

    /// Whether the object can be instantiated
    const ABSTRACT: bool = false;

    /// Function that is called to initialize an object.  The parent class will
    /// have already been initialized so the type is only responsible for
    /// initializing its own members.
    ///
    /// FIXME: The argument is not really a valid reference. `&mut
    /// MaybeUninit<Self>` would be a better description.
    const INSTANCE_INIT: Option<unsafe fn(&mut Self)> = None;

    /// Function that is called to finish initialization of an object, once
    /// `INSTANCE_INIT` functions have been called.
    const INSTANCE_POST_INIT: Option<fn(&Self)> = None;

    /// Called on descendent classes after all parent class initialization
    /// has occurred, but before the class itself is initialized.  This
    /// is only useful if a class is not a leaf, and can be used to undo
    /// the effects of copying the contents of the parent's class struct
    /// to the descendants.
    const CLASS_BASE_INIT: Option<
        unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void),
    > = None;

    const TYPE_INFO: TypeInfo = TypeInfo {
        name: Self::TYPE_NAME.as_ptr(),
        parent: Self::ParentType::TYPE_NAME.as_ptr(),
        instance_size: core::mem::size_of::<Self>(),
        instance_align: core::mem::align_of::<Self>(),
        instance_init: match Self::INSTANCE_INIT {
            None => None,
            Some(_) => Some(rust_instance_init::<Self>),
        },
        instance_post_init: match Self::INSTANCE_POST_INIT {
            None => None,
            Some(_) => Some(rust_instance_post_init::<Self>),
        },
        instance_finalize: Some(drop_object::<Self>),
        abstract_: Self::ABSTRACT,
        class_size: core::mem::size_of::<Self::Class>(),
        class_init: Some(rust_class_init::<Self>),
        class_base_init: Self::CLASS_BASE_INIT,
        class_data: core::ptr::null_mut(),
        interfaces: core::ptr::null_mut(),
    };

    // methods on ObjectClass
    const UNPARENT: Option<fn(&Self)> = None;
}

/// Internal trait used to automatically fill in a class struct.
///
/// Each QOM class that has virtual methods describes them in a
/// _class struct_.  Class structs include a parent field corresponding
/// to the vtable of the parent class, all the way up to [`ObjectClass`].
/// Each QOM type has one such class struct; this trait takes care of
/// initializing the `T` part of the class struct, for the type that
/// implements the trait.
///
/// Each struct will implement this trait with `T` equal to each
/// superclass.  For example, a device should implement at least
/// `ClassInitImpl<`[`DeviceClass`](crate::qdev::DeviceClass)`>` and
/// `ClassInitImpl<`[`ObjectClass`]`>`.  Such implementations are made
/// in one of two ways.
///
/// For most superclasses, `ClassInitImpl` is provided by the `qemu-api`
/// crate itself.  The Rust implementation of methods will come from a
/// trait like [`ObjectImpl`] or [`DeviceImpl`](crate::qdev::DeviceImpl),
/// and `ClassInitImpl` is provided by blanket implementations that
/// operate on all implementors of the `*Impl`* trait.  For example:
///
/// ```ignore
/// impl<T> ClassInitImpl<DeviceClass> for T
/// where
///     T: ClassInitImpl<ObjectClass> + DeviceImpl,
/// ```
///
/// The bound on `ClassInitImpl<ObjectClass>` is needed so that,
/// after initializing the `DeviceClass` part of the class struct,
/// the parent [`ObjectClass`] is initialized as well.
///
/// The other case is when manual implementation of the trait is needed.
/// This covers the following cases:
///
/// * if a class implements a QOM interface, the Rust code _has_ to define its
///   own class struct `FooClass` and implement `ClassInitImpl<FooClass>`.
///   `ClassInitImpl<FooClass>`'s `class_init` method will then forward to
///   multiple other `class_init`s, for the interfaces as well as the
///   superclass. (Note that there is no Rust example yet for using interfaces).
///
/// * for classes implemented outside the ``qemu-api`` crate, it's not possible
///   to add blanket implementations like the above one, due to orphan rules. In
///   that case, the easiest solution is to implement
///   `ClassInitImpl<YourSuperclass>` for each subclass and not have a
///   `YourSuperclassImpl` trait at all.
///
/// ```ignore
/// impl ClassInitImpl<YourSuperclass> for YourSubclass {
///     fn class_init(klass: &mut YourSuperclass) {
///         klass.some_method = Some(Self::some_method);
///         <Self as ClassInitImpl<SysBusDeviceClass>>::class_init(&mut klass.parent_class);
///     }
/// }
/// ```
///
///   While this method incurs a small amount of code duplication,
///   it is generally limited to the recursive call on the last line.
///   This is because classes defined in Rust do not need the same
///   glue code that is needed when the classes are defined in C code.
///   You may consider using a macro if you have many subclasses.
pub trait ClassInitImpl<T> {
    /// Initialize `klass` to point to the virtual method implementations
    /// for `Self`.  On entry, the virtual method pointers are set to
    /// the default values coming from the parent classes; the function
    /// can change them to override virtual methods of a parent class.
    ///
    /// The virtual method implementations usually come from another
    /// trait, for example [`DeviceImpl`](crate::qdev::DeviceImpl)
    /// when `T` is [`DeviceClass`](crate::qdev::DeviceClass).
    ///
    /// On entry, `klass`'s parent class is initialized, while the other fields
    /// are all zero; it is therefore assumed that all fields in `T` can be
    /// zeroed, otherwise it would not be possible to provide the class as a
    /// `&mut T`.  TODO: add a bound of [`Zeroable`](crate::zeroable::Zeroable)
    /// to T; this is more easily done once Zeroable does not require a manual
    /// implementation (Rust 1.75.0).
    fn class_init(klass: &mut T);
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_unparent_fn<T: ObjectImpl>(dev: *mut Object) {
    let state = NonNull::new(dev).unwrap().cast::<T>();
    T::UNPARENT.unwrap()(unsafe { state.as_ref() });
}

impl<T> ClassInitImpl<ObjectClass> for T
where
    T: ObjectImpl,
{
    fn class_init(oc: &mut ObjectClass) {
        if <T as ObjectImpl>::UNPARENT.is_some() {
            oc.unparent = Some(rust_unparent_fn::<T>);
        }
    }
}

unsafe impl ObjectType for Object {
    type Class = ObjectClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_OBJECT) };
}

/// Trait for methods exposed by the Object class.  The methods can be
/// called on all objects that have the trait `IsA<Object>`.
///
/// The trait should only be used through the blanket implementation,
/// which guarantees safety via `IsA`
pub trait ObjectMethods: ObjectDeref
where
    Self::Target: IsA<Object>,
{
    /// Return the name of the type of `self`
    fn typename(&self) -> std::borrow::Cow<'_, str> {
        let obj = self.upcast::<Object>();
        // SAFETY: safety of this is the requirement for implementing IsA
        // The result of the C API has static lifetime
        unsafe {
            let p = object_get_typename(obj.as_mut_ptr());
            CStr::from_ptr(p).to_string_lossy()
        }
    }

    fn get_class(&self) -> &'static <Self::Target as ObjectType>::Class {
        let obj = self.upcast::<Object>();

        // SAFETY: all objects can call object_get_class; the actual class
        // type is guaranteed by the implementation of `ObjectType` and
        // `ObjectImpl`.
        let klass: &'static <Self::Target as ObjectType>::Class =
            unsafe { &*object_get_class(obj.as_mut_ptr()).cast() };

        klass
    }
}

impl<R: ObjectDeref> ObjectMethods for R where R::Target: IsA<Object> {}
