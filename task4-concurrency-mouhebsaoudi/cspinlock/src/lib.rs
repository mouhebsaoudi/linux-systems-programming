#[allow(non_camel_case_types)]
pub struct cspinlock_t {}


/// acquire the lock
#[no_mangle]
pub extern "C" fn cspin_lock(raw_lock: *mut cspinlock_t) -> libc::c_int {
    return 1;
}

/// try the lock
#[no_mangle]
pub extern "C" fn cspin_trylock(raw_lock: *mut cspinlock_t) -> libc::c_int {
    return 1;
}

/// release the lock
#[no_mangle]
pub extern "C" fn cspin_unlock(raw_lock: *mut cspinlock_t) -> libc::c_int {
    return 1;
}

/// allocate a lock
#[no_mangle]
pub extern "C" fn cspin_alloc() -> *mut cspinlock_t {
    return std::ptr::null_mut();
}

/// free a lock
#[no_mangle]
pub extern "C" fn cspin_free(lock: *mut cspinlock_t) {}
