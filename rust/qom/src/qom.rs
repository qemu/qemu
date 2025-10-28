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
//!   The traits have the appropriate specialization of `IsA<>` as a supertrait,
//!   for example `IsA<DeviceState>` for `DeviceImpl`.
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
//!
//! # Class structures
//!
//! Each QOM class that has virtual methods describes them in a
//! _class struct_.  Class structs include a parent field corresponding
//! to the vtable of the parent class, all the way up to [`ObjectClass`].
//!
//! As mentioned above, virtual methods are defined via traits such as
//! `DeviceImpl`.  Class structs do not define any trait but, conventionally,
//! all of them have a `class_init` method to initialize the virtual methods
//! based on the trait and then call the same method on the superclass.
//!
//! ```ignore
//! impl YourSubclassClass
//! {
//!     pub fn class_init<T: YourSubclassImpl>(&mut self) {
//!         ...
//!         klass.parent_class::class_init<T>();
//!     }
//! }
//! ```
//!
//! If a class implements a QOM interface.  In that case, the function must
//! contain, for each interface, an extra forwarding call as follows:
//!
//! ```ignore
//! ResettableClass::cast::<Self>(self).class_init::<Self>();
//! ```
//!
//! These `class_init` functions are methods on the class rather than a trait,
//! because the bound on `T` (`DeviceImpl` in this case), will change for every
//! class struct.  The functions are pointed to by the
//! [`ObjectImpl::CLASS_INIT`] function pointer. While there is no default
//! implementation, in most cases it will be enough to write it as follows:
//!
//! ```ignore
//! const CLASS_INIT: fn(&mut Self::Class)> = Self::Class::class_init::<Self>;
//! ```
//!
//! This design incurs a small amount of code duplication but, by not using
//! traits, it allows the flexibility of implementing bindings in any crate,
//! without incurring into violations of orphan rules for traits.

use std::{
    ffi::{c_void, CStr},
    fmt,
    marker::PhantomData,
    mem::{ManuallyDrop, MaybeUninit},
    ops::{Deref, DerefMut},
    ptr::NonNull,
};

use common::Opaque;
use migration::{impl_vmstate_pointer, impl_vmstate_transparent};

use crate::bindings::{
    self, object_class_dynamic_cast, object_dynamic_cast, object_get_class, object_get_typename,
    object_new, object_ref, object_unref, TypeInfo,
};
pub use crate::bindings::{type_register_static, ObjectClass};

/// A safe wrapper around [`bindings::Object`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct Object(Opaque<bindings::Object>);

unsafe impl Send for Object {}
unsafe impl Sync for Object {}

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
            unsafe impl $crate::IsA<$parent> for $struct {}

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
/// #[derive(qom::Object)]
/// pub struct MyDevice {
///     parent: ParentField<DeviceState>,
///     ...
/// }
/// ```
#[derive(Debug)]
#[repr(transparent)]
pub struct ParentField<T: ObjectType>(std::mem::ManuallyDrop<T>);
impl_vmstate_transparent!(ParentField<T> where T: VMState + ObjectType);

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

/// This struct knows that the superclasses of the object have already been
/// initialized.
///
/// The declaration of `ParentInit` is.. *"a kind of magic"*.  It uses a
/// technique that is found in several crates, the main ones probably being
/// `ghost-cell` (in fact it was introduced by the [`GhostCell` paper](https://plv.mpi-sws.org/rustbelt/ghostcell/))
/// and `generativity`.
///
/// The `PhantomData` makes the `ParentInit` type *invariant* with respect to
/// the lifetime argument `'init`.  This, together with the `for<'...>` in
/// `[ParentInit::with]`, block any attempt of the compiler to be creative when
/// operating on types of type `ParentInit` and to extend their lifetimes.  In
/// particular, it ensures that the `ParentInit` cannot be made to outlive the
/// `rust_instance_init()` function that creates it, and therefore that the
/// `&'init T` reference is valid.
///
/// This implementation of the same concept, without the QOM baggage, can help
/// understanding the effect:
///
/// ```
/// use std::marker::PhantomData;
///
/// #[derive(PartialEq, Eq)]
/// pub struct Jail<'closure, T: Copy>(&'closure T, PhantomData<fn(&'closure ()) -> &'closure ()>);
///
/// impl<'closure, T: Copy> Jail<'closure, T> {
///     fn get(&self) -> T {
///         *self.0
///     }
///
///     #[inline]
///     fn with<U>(v: T, f: impl for<'id> FnOnce(Jail<'id, T>) -> U) -> U {
///         let parent_init = Jail(&v, PhantomData);
///         f(parent_init)
///     }
/// }
/// ```
///
/// It's impossible to escape the `Jail`; `token1` cannot be moved out of the
/// closure:
///
/// ```ignore
/// let x = 42;
/// let escape = Jail::with(&x, |token1| {
///     println!("{}", token1.get());
///     // fails to compile...
///     token1
/// });
/// // ... so you cannot do this:
/// println!("{}", escape.get());
/// ```
///
/// Likewise, in the QOM case the `ParentInit` cannot be moved out of
/// `instance_init()`. Without this trick it would be possible to stash a
/// `ParentInit` and use it later to access uninitialized memory.
///
/// Here is another example, showing how separately-created "identities" stay
/// isolated:
///
/// ```ignore
/// impl<'closure, T: Copy> Clone for Jail<'closure, T> {
///     fn clone(&self) -> Jail<'closure, T> {
///         Jail(self.0, PhantomData)
///     }
/// }
///
/// fn main() {
///     Jail::with(42, |token1| {
///         // this works and returns true: the clone has the same "identity"
///         println!("{}", token1 == token1.clone());
///         Jail::with(42, |token2| {
///             // here the outer token remains accessible...
///             println!("{}", token1.get());
///             // ... but the two are separate: this fails to compile:
///             println!("{}", token1 == token2);
///         });
///     });
/// }
/// ```
pub struct ParentInit<'init, T>(
    &'init mut MaybeUninit<T>,
    PhantomData<fn(&'init ()) -> &'init ()>,
);

impl<'init, T> ParentInit<'init, T> {
    #[inline]
    pub fn with(obj: &'init mut MaybeUninit<T>, f: impl for<'id> FnOnce(ParentInit<'id, T>)) {
        let parent_init = ParentInit(obj, PhantomData);
        f(parent_init)
    }
}

impl<T: ObjectType> ParentInit<'_, T> {
    /// Return the receiver as a mutable raw pointer to Object.
    ///
    /// # Safety
    ///
    /// Fields beyond `Object` could be uninitialized and it's your
    /// responsibility to avoid that they're used when the pointer is
    /// dereferenced, either directly or through a cast.
    pub const fn as_object_mut_ptr(&self) -> *mut bindings::Object {
        self.as_object_ptr().cast_mut()
    }

    /// Return the receiver as a mutable raw pointer to Object.
    ///
    /// # Safety
    ///
    /// Fields beyond `Object` could be uninitialized and it's your
    /// responsibility to avoid that they're used when the pointer is
    /// dereferenced, either directly or through a cast.
    pub const fn as_object_ptr(&self) -> *const bindings::Object {
        self.0.as_ptr().cast()
    }
}

impl<'a, T: ObjectImpl> ParentInit<'a, T> {
    /// Convert from a derived type to one of its parent types, which
    /// have already been initialized.
    ///
    /// # Safety
    ///
    /// Structurally this is always a safe operation; the [`IsA`] trait
    /// provides static verification trait that `Self` dereferences to `U` or
    /// a child of `U`, and only parent types of `T` are allowed.
    ///
    /// However, while the fields of the resulting reference are initialized,
    /// calls might use uninitialized fields of the subclass.  It is your
    /// responsibility to avoid this.
    pub const unsafe fn upcast<U: ObjectType>(&self) -> &'a U
    where
        T::ParentType: IsA<U>,
    {
        // SAFETY: soundness is declared via IsA<U>, which is an unsafe trait;
        // the parent has been initialized before `instance_init `is called
        unsafe { &*(self.0.as_ptr().cast::<U>()) }
    }

    /// Convert from a derived type to one of its parent types, which
    /// have already been initialized.
    ///
    /// # Safety
    ///
    /// Structurally this is always a safe operation; the [`IsA`] trait
    /// provides static verification trait that `Self` dereferences to `U` or
    /// a child of `U`, and only parent types of `T` are allowed.
    ///
    /// However, while the fields of the resulting reference are initialized,
    /// calls might use uninitialized fields of the subclass.  It is your
    /// responsibility to avoid this.
    pub unsafe fn upcast_mut<U: ObjectType>(&mut self) -> &'a mut U
    where
        T::ParentType: IsA<U>,
    {
        // SAFETY: soundness is declared via IsA<U>, which is an unsafe trait;
        // the parent has been initialized before `instance_init `is called
        unsafe { &mut *(self.0.as_mut_ptr().cast::<U>()) }
    }
}

impl<T> Deref for ParentInit<'_, T> {
    type Target = MaybeUninit<T>;

    fn deref(&self) -> &Self::Target {
        self.0
    }
}

impl<T> DerefMut for ParentInit<'_, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.0
    }
}

unsafe extern "C" fn rust_instance_init<T: ObjectImpl>(obj: *mut bindings::Object) {
    let mut state = NonNull::new(obj).unwrap().cast::<MaybeUninit<T>>();

    // SAFETY: obj is an instance of T, since rust_instance_init<T>
    // is called from QOM core as the instance_init function
    // for class T
    unsafe {
        ParentInit::with(state.as_mut(), |parent_init| {
            T::INSTANCE_INIT.unwrap()(parent_init);
        });
    }
}

unsafe extern "C" fn rust_instance_post_init<T: ObjectImpl>(obj: *mut bindings::Object) {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    // SAFETY: obj is an instance of T, since rust_instance_post_init<T>
    // is called from QOM core as the instance_post_init function
    // for class T
    T::INSTANCE_POST_INIT.unwrap()(unsafe { state.as_ref() });
}

unsafe extern "C" fn rust_class_init<T: ObjectType + ObjectImpl>(
    klass: *mut ObjectClass,
    _data: *const c_void,
) {
    let mut klass = NonNull::new(klass)
        .unwrap()
        .cast::<<T as ObjectType>::Class>();
    // SAFETY: klass is a T::Class, since rust_class_init<T>
    // is called from QOM core as the class_init function
    // for class T
    <T as ObjectImpl>::CLASS_INIT(unsafe { klass.as_mut() })
}

unsafe extern "C" fn drop_object<T: ObjectImpl>(obj: *mut bindings::Object) {
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
        unsafe { &*self.as_ptr().cast() }
    }

    /// Return the receiver as a const raw pointer to Object.
    /// This is preferable to `as_object_mut_ptr()` if a C
    /// function only needs a `const Object *`.
    fn as_object_ptr(&self) -> *const bindings::Object {
        self.as_object().as_ptr()
    }

    /// Return the receiver as a mutable raw pointer to Object.
    ///
    /// # Safety
    ///
    /// This cast is always safe, but because the result is mutable
    /// and the incoming reference is not, this should only be used
    /// for calls to C functions, and only if needed.
    unsafe fn as_object_mut_ptr(&self) -> *mut bindings::Object {
        self.as_object().as_mut_ptr()
    }
}

/// Trait exposed by all structs corresponding to QOM interfaces.
/// Unlike `ObjectType`, it is implemented on the class type (which provides
/// the vtable for the interfaces).
///
/// # Safety
///
/// `TYPE` must match the contents of the `TypeInfo` as found in the C code;
/// right now, interfaces can only be declared in C.
pub unsafe trait InterfaceType: Sized {
    /// The name of the type, which can be passed to
    /// `object_class_dynamic_cast()` to obtain the pointer to the vtable
    /// for this interface.
    const TYPE_NAME: &'static CStr;

    /// Return the vtable for the interface; `U` is the type that
    /// lists the interface in its `TypeInfo`.
    ///
    /// # Examples
    ///
    /// This function is usually called by a `class_init` method in `U::Class`.
    /// For example, `DeviceClass::class_init<T>` initializes its `Resettable`
    /// interface as follows:
    ///
    /// ```ignore
    /// ResettableClass::cast::<DeviceState>(self).class_init::<T>();
    /// ```
    ///
    /// where `T` is the concrete subclass that is being initialized.
    ///
    /// # Panics
    ///
    /// Panic if the incoming argument if `T` does not implement the interface.
    fn cast<U: ObjectType>(klass: &mut U::Class) -> &mut Self {
        unsafe {
            // SAFETY: upcasting to ObjectClass is always valid, and the
            // return type is either NULL or the argument itself
            let result: *mut Self = object_class_dynamic_cast(
                (klass as *mut U::Class).cast(),
                Self::TYPE_NAME.as_ptr(),
            )
            .cast();
            result.as_mut().unwrap()
        }
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
    /// This method is safe because only the actual dereference of the pointer
    /// has to be unsafe.  Bindings to C APIs will use it a lot, but care has
    /// to be taken because it overrides the const-ness of `&self`.
    fn as_mut_ptr<U: ObjectType>(&self) -> *mut U
    where
        Self::Target: IsA<U>,
    {
        #[allow(clippy::as_ptr_cast_mut)]
        {
            self.as_ptr::<U>().cast_mut()
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

impl<T: ObjectType> ObjectDeref for &mut T {}

/// Trait a type must implement to be registered with QEMU.
pub trait ObjectImpl: ObjectType + IsA<Object> {
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
    const INSTANCE_INIT: Option<unsafe fn(ParentInit<Self>)> = None;

    /// Function that is called to finish initialization of an object, once
    /// `INSTANCE_INIT` functions have been called.
    const INSTANCE_POST_INIT: Option<fn(&Self)> = None;

    /// Called on descendant classes after all parent class initialization
    /// has occurred, but before the class itself is initialized.  This
    /// is only useful if a class is not a leaf, and can be used to undo
    /// the effects of copying the contents of the parent's class struct
    /// to the descendants.
    const CLASS_BASE_INIT: Option<
        unsafe extern "C" fn(klass: *mut ObjectClass, data: *const c_void),
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
        class_data: core::ptr::null(),
        interfaces: core::ptr::null(),
    };

    // methods on ObjectClass
    const UNPARENT: Option<fn(&Self)> = None;

    /// Store into the argument the virtual method implementations
    /// for `Self`.  On entry, the virtual method pointers are set to
    /// the default values coming from the parent classes; the function
    /// can change them to override virtual methods of a parent class.
    ///
    /// Usually defined simply as `Self::Class::class_init::<Self>`;
    /// however a default implementation cannot be included here, because the
    /// bounds that the `Self::Class::class_init` method places on `Self` are
    /// not known in advance.
    ///
    /// # Safety
    ///
    /// While `klass`'s parent class is initialized on entry, the other fields
    /// are all zero; it is therefore assumed that all fields in `T` can be
    /// zeroed, otherwise it would not be possible to provide the class as a
    /// `&mut T`.  TODO: it may be possible to add an unsafe trait that checks
    /// that all fields *after the parent class* (but not the parent class
    /// itself) are Zeroable.  This unsafe trait can be added via a derive
    /// macro.
    const CLASS_INIT: fn(&mut Self::Class);
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_unparent_fn<T: ObjectImpl>(dev: *mut bindings::Object) {
    let state = NonNull::new(dev).unwrap().cast::<T>();
    T::UNPARENT.unwrap()(unsafe { state.as_ref() });
}

impl ObjectClass {
    /// Fill in the virtual methods of `ObjectClass` based on the definitions in
    /// the `ObjectImpl` trait.
    pub fn class_init<T: ObjectImpl>(&mut self) {
        if <T as ObjectImpl>::UNPARENT.is_some() {
            self.unparent = Some(rust_unparent_fn::<T>);
        }
    }
}

unsafe impl ObjectType for Object {
    type Class = ObjectClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_OBJECT) };
}

/// A reference-counted pointer to a QOM object.
///
/// `Owned<T>` wraps `T` with automatic reference counting.  It increases the
/// reference count when created via [`Owned::from`] or cloned, and decreases
/// it when dropped.  This ensures that the reference count remains elevated
/// as long as any `Owned<T>` references to it exist.
///
/// `Owned<T>` can be used for two reasons:
/// * because the lifetime of the QOM object is unknown and someone else could
///   take a reference (similar to `Arc<T>`, for example): in this case, the
///   object can escape and outlive the Rust struct that contains the `Owned<T>`
///   field;
///
/// * to ensure that the object stays alive until after `Drop::drop` is called
///   on the Rust struct: in this case, the object will always die together with
///   the Rust struct that contains the `Owned<T>` field.
///
/// Child properties are an example of the second case: in C, an object that
/// is created with `object_initialize_child` will die *before*
/// `instance_finalize` is called, whereas Rust expects the struct to have valid
/// contents when `Drop::drop` is called.  Therefore Rust structs that have
/// child properties need to keep a reference to the child object.  Right now
/// this can be done with `Owned<T>`; in the future one might have a separate
/// `Child<'parent, T>` smart pointer that keeps a reference to a `T`, like
/// `Owned`, but does not allow cloning.
///
/// Note that dropping an `Owned<T>` requires the big QEMU lock to be taken.
#[repr(transparent)]
#[derive(PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Owned<T: ObjectType>(NonNull<T>);

// The following rationale for safety is taken from Linux's kernel::sync::Arc.

// SAFETY: It is safe to send `Owned<T>` to another thread when the underlying
// `T` is `Sync` because it effectively means sharing `&T` (which is safe
// because `T` is `Sync`); additionally, it needs `T` to be `Send` because any
// thread that has an `Owned<T>` may ultimately access `T` using a
// mutable reference when the reference count reaches zero and `T` is dropped.
unsafe impl<T: ObjectType + Send + Sync> Send for Owned<T> {}

// SAFETY: It is safe to send `&Owned<T>` to another thread when the underlying
// `T` is `Sync` because it effectively means sharing `&T` (which is safe
// because `T` is `Sync`); additionally, it needs `T` to be `Send` because any
// thread that has a `&Owned<T>` may clone it and get an `Owned<T>` on that
// thread, so the thread may ultimately access `T` using a mutable reference
// when the reference count reaches zero and `T` is dropped.
unsafe impl<T: ObjectType + Sync + Send> Sync for Owned<T> {}

impl<T: ObjectType> Owned<T> {
    /// Convert a raw C pointer into an owned reference to the QOM
    /// object it points to.  The object's reference count will be
    /// decreased when the `Owned` is dropped.
    ///
    /// # Panics
    ///
    /// Panics if `ptr` is NULL.
    ///
    /// # Safety
    ///
    /// The caller must indeed own a reference to the QOM object.
    /// The object must not be embedded in another unless the outer
    /// object is guaranteed to have a longer lifetime.
    ///
    /// A raw pointer obtained via [`Owned::into_raw()`] can always be passed
    /// back to `from_raw()` (assuming the original `Owned` was valid!),
    /// since the owned reference remains there between the calls to
    /// `into_raw()` and `from_raw()`.
    pub unsafe fn from_raw(ptr: *const T) -> Self {
        // SAFETY NOTE: while NonNull requires a mutable pointer, only
        // Deref is implemented so the pointer passed to from_raw
        // remains const
        Owned(NonNull::new(ptr.cast_mut()).unwrap())
    }

    /// Obtain a raw C pointer from a reference.  `src` is consumed
    /// and the reference is leaked.
    #[allow(clippy::missing_const_for_fn)]
    pub fn into_raw(src: Owned<T>) -> *mut T {
        let src = ManuallyDrop::new(src);
        src.0.as_ptr()
    }

    /// Increase the reference count of a QOM object and return
    /// a new owned reference to it.
    ///
    /// # Safety
    ///
    /// The object must not be embedded in another, unless the outer
    /// object is guaranteed to have a longer lifetime.
    pub unsafe fn from(obj: &T) -> Self {
        unsafe {
            object_ref(obj.as_object_mut_ptr().cast::<c_void>());

            // SAFETY NOTE: while NonNull requires a mutable pointer, only
            // Deref is implemented so the reference passed to from_raw
            // remains shared
            Owned(NonNull::new_unchecked(obj.as_mut_ptr()))
        }
    }
}

impl<T: ObjectType> Clone for Owned<T> {
    fn clone(&self) -> Self {
        // SAFETY: creation method is unsafe; whoever calls it has
        // responsibility that the pointer is valid, and remains valid
        // throughout the lifetime of the `Owned<T>` and its clones.
        unsafe { Owned::from(self.deref()) }
    }
}

impl<T: ObjectType> Deref for Owned<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: creation method is unsafe; whoever calls it has
        // responsibility that the pointer is valid, and remains valid
        // throughout the lifetime of the `Owned<T>` and its clones.
        // With that guarantee, reference counting ensures that
        // the object remains alive.
        unsafe { &*self.0.as_ptr() }
    }
}
impl<T: ObjectType> ObjectDeref for Owned<T> {}

impl<T: ObjectType> Drop for Owned<T> {
    fn drop(&mut self) {
        assert!(bql::is_locked());
        // SAFETY: creation method is unsafe, and whoever calls it has
        // responsibility that the pointer is valid, and remains valid
        // throughout the lifetime of the `Owned<T>` and its clones.
        unsafe {
            object_unref(self.as_object_mut_ptr().cast::<c_void>());
        }
    }
}

impl<T: IsA<Object>> fmt::Debug for Owned<T> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.deref().debug_fmt(f)
    }
}

/// Trait for class methods exposed by the Object class.  The methods can be
/// called on all objects that have the trait `IsA<Object>`.
///
/// The trait should only be used through the blanket implementation,
/// which guarantees safety via `IsA`
pub trait ObjectClassMethods: IsA<Object> {
    /// Return a new reference counted instance of this class
    fn new() -> Owned<Self> {
        assert!(bql::is_locked());
        // SAFETY: the object created by object_new is allocated on
        // the heap and has a reference count of 1
        unsafe {
            let raw_obj = object_new(Self::TYPE_NAME.as_ptr());
            let obj = Object::from_raw(raw_obj).unsafe_cast::<Self>();
            Owned::from_raw(obj)
        }
    }
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

    /// Convenience function for implementing the Debug trait
    fn debug_fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_tuple(&self.typename())
            .field(&(self as *const Self))
            .finish()
    }
}

impl<T> ObjectClassMethods for T where T: IsA<Object> {}
impl<R: ObjectDeref> ObjectMethods for R where R::Target: IsA<Object> {}

impl_vmstate_pointer!(Owned<T> where T: VMState + ObjectType);
