use libc;

#[derive(Debug)]
#[repr(C)]
pub struct command {
  next: *const command,

  argv: *const *const libc::c_char,
  argv_cap: libc::size_t,
  argc: libc::c_int,
  output_redir: *const libc::c_char,
  input_redir: *const libc::c_char,
}

#[derive(Debug)]
#[repr(C)]
pub struct pipeline {
  first_command: command,
  background: libc::c_char,
}

#[derive(Debug)]
#[repr(C)]
#[allow(non_camel_case_types)]
pub enum builtin_type {
  BUILTIN_NONE = 0,
  BUILTIN_EXIT = 1,
  BUILTIN_WAIT = 2,
  BUILTIN_KILL = 3,
}

#[no_mangle]
pub extern "C" fn run_pipeline(p: *const pipeline) -> i32 { return 2; }

#[no_mangle]
pub extern "C" fn run_builtin(builtin: builtin_type, builtin_arg: *const libc::c_char) -> i32 { return 2; }
