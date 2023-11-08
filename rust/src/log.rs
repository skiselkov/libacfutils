/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::sync::RwLock;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

#[macro_export]
// No need to include a trailing newline, it will be appended.
macro_rules! logMsg {
    ($($x:expr),*) => {
	{
		use std::ffi::CString;
		let c_format = CString::new("%s").unwrap();
		let filename_cow = std::path::Path::new(file!())
		    .components()
		    .last()
		    .unwrap_or(std::path::Component::Normal("<empty>".as_ref()))
		    .as_os_str()
		    .to_string_lossy();
		let c_filename = CString::new(filename_cow.as_ref())
		    .expect("file!() returned string with stray NUL byte");
		let c_message = CString::new(format!($($x,)*))
		    .expect("passed log message contained stray NUL byte");
		unsafe {
			crate::log::log_impl(c_filename.as_ptr(),
			    line!(), c_format.as_ptr(), c_message.as_ptr());
		}
	}
    };
}
/*
 * Custom logging function which you can supply.
 * N.B. the trailing newline is already include, so don't use println!()
 * or similar functions, otherwise you'll end up with double newlines.
 */
type CustomLogFunc = fn(&str);

pub static LOG_FUNC_CB: RwLock<Option<CustomLogFunc>> = RwLock::new(None);

pub fn init(log_func: Option<CustomLogFunc>, prefix: &str) {
	let mut log_func_cb = LOG_FUNC_CB.write()
	    .expect("LOG_FUNC_CB mutex is panicked");
	*log_func_cb = log_func;
	unsafe {
		let c_prefix = CString::new(prefix)
		    .expect("`prefix` contains a stray NUL byte");
		log_init(my_log_func, c_prefix.as_ptr());
	}
}

pub fn fini() {
	let mut log_func_cb = LOG_FUNC_CB.write()
	    .expect("LOG_FUNC_CB mutex is panicked");
	*log_func_cb = None;
	unsafe { log_fini(); }
}

extern "C" fn my_log_func(c_message: *const c_char) {
	use std::ops::Deref;
	let log_func_cb = LOG_FUNC_CB.read()
	    .expect("LOG_FUNC_CB mutex is panicked");
	assert!(!c_message.is_null());
	let cs_message = unsafe { CStr::from_ptr(c_message) };
	let message = cs_message.to_string_lossy().into_owned();
	if let Some(log_func) = log_func_cb.deref() {
		log_func(&message);
	} else {
		// `c_message` already contains a trailing newline
		print!("{}", &message);
	}
}

type LogFuncC = extern "C" fn(*const c_char);
extern "C" {
	fn log_init(log_func: LogFuncC, prefix: *const c_char);
	fn log_fini();
	pub fn log_impl(filename: *const c_char, line: u32,
	    fmt: *const c_char, arg: *const c_char);
}

mod tests {
	use std::sync::RwLock;

	static CUSTOM_LOGGER_CALLED: RwLock<bool> = RwLock::new(false);
	/*
	 * N.B. we can't define more than 1 test, because cargo will
	 * attempt to run them all in parallel and libacfutils' log.h
	 * subsystem initialization state is shared.
	 */
	#[test]
	fn log_msg_test() {
		use crate::log;
		let stock = "stock";
		let custom = "custom";

		log::init(None, "Test");
		logMsg!("message for {} logger", stock);
		log::fini();
		assert!(!*CUSTOM_LOGGER_CALLED.read().unwrap());

		log::init(Some(custom_logger), "Test");
		logMsg!("message for {} logger", custom);
		log::fini();
		assert!(*CUSTOM_LOGGER_CALLED.read().unwrap());
	}
	#[allow(dead_code)]
	fn custom_logger(msg: &str) {
		*CUSTOM_LOGGER_CALLED.write().unwrap() = true;
		print!("Custom logger says: {}", msg);
	}
}
