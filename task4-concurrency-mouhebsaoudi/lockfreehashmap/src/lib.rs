pub struct HM {}

/// allocate a hashmap with given number of buckets
#[no_mangle]
pub extern "C" fn alloc_hashmap(n_buckets: libc::size_t) -> *mut HM {
    return std::ptr::null_mut();
}

/// free a hashmap
#[no_mangle]
pub extern "C" fn free_hashmap(hm: *mut HM) {}

///insert val into the hm and return 0 if successful
#[no_mangle]
pub extern "C" fn insert_item(hm: *mut HM, val: libc::c_long) -> libc::c_int {
    return 1;
}

///remove val from the hm, if it exist and return 0 if successful
#[no_mangle]
pub extern "C" fn remove_item(hm: *mut HM, val: libc::c_long) -> libc::c_int {
    return 1;
}

///check if val exists in hm, return 0 if found
#[no_mangle]
pub extern "C" fn lookup_item(hm: *mut HM, val: libc::c_long) -> libc::c_int {
    return 1
}

///print all elements in the hashmap as follows:
///Bucket 1 - val1 - val2 - val3 ...
///Bucket 2 - val4 - val5 - val6 ...
///Bucket N -  ...
#[no_mangle]
pub extern "C" fn print_hashmap(hm: *mut HM) {}
