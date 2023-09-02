/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::os::raw::c_char;
use std::os::raw::c_void;
use std::ffi::CString;
use std::ffi::CStr;

pub struct Conf {
	conf: *mut conf_t
}

impl Conf {
	pub fn new() -> Self {
		unsafe {
			Conf{ conf: conf_create_empty() }
		}
	}
	pub unsafe fn to_raw_conf_t(&self) -> *mut conf_t {
		self.conf
	}
	pub fn from_file(filename: &str, errline: Option<&mut i32>) ->
	    Option<Conf> {
		let conf = unsafe {
			let c_filename = CString::new(filename).unwrap();
			match errline {
			Some(linenr) =>
				conf_read_file(c_filename.as_ptr(), linenr),
			None =>
				conf_read_file(c_filename.as_ptr(),
				    std::ptr::null_mut::<i32>()),
			}
		};
		if !conf.is_null() {
			Some(Conf{conf: conf})
		} else {
			None
		}
	}
	pub fn from_buf(buf: &[u8], errline: Option<&mut i32>) -> Option<Conf> {
		let conf = unsafe {
			match errline {
			Some(linenr) =>
				conf_read_buf(buf.as_ptr() as *const c_void,
				    buf.len(), linenr),
			None =>
				conf_read_buf(buf.as_ptr() as *const c_void,
				    buf.len(), std::ptr::null_mut::<i32>()),
			}
		};
		if !conf.is_null() {
			Some(Conf{conf: conf})
		} else {
			None
		}
	}
	pub fn to_file(&self, filename: &str) -> bool {
		unsafe {
			let c_filename = CString::new(filename).unwrap();
			conf_write_file(self.conf, c_filename.as_ptr())
		}
	}
	pub fn to_file2(&self, filename: &str, compressed: bool) -> bool {
		unsafe {
			let c_filename = CString::new(filename).unwrap();
			conf_write_file2(self.conf, c_filename.as_ptr(),
			    compressed)
		}
	}
	pub fn to_buf(&self) -> Vec<u8> {
		unsafe {
			let n = conf_write_buf(self.conf,
			    std::ptr::null_mut(), 0);
			let mut buf: Vec<u8> = vec![0; n];
			conf_write_buf(self.conf,
			    buf.as_mut_ptr() as *mut c_void, buf.len());
			buf
		}
	}
	pub fn merge(&self, conf_from: &Conf) {
		unsafe {
			conf_merge(conf_from.conf, self.conf)
		}
	}
	pub fn get_str(&self, key: &str) -> Option<&str> {
		unsafe {
			let c_key = CString::new(key).unwrap();
			let mut value: *const c_char =
			    std::ptr::null::<c_char>();

			if conf_get_str(self.conf, c_key.as_ptr(), &mut value) {
				let c_str = std::ffi::CStr::from_ptr(value);
				match c_str.to_str() {
				Ok(the_str) => Some(the_str),
				Err(_) => None
				}
			} else {
				None
			}
		}
	}
	pub fn get_i32(&self, key: &str) -> Option<i32> {
		let c_key = CString::new(key).unwrap();
		let mut val: i32 = 0;

		if unsafe { conf_get_i(self.conf, c_key.as_ptr(), &mut val) } {
			Some(val)
		} else {
			None
		}
	}
	pub fn get_i64(&self, key: &str) -> Option<i64> {
		let c_key = CString::new(key).unwrap();
		let mut val: i64 = 0;

		if unsafe { conf_get_lli(self.conf, c_key.as_ptr(),
		    &mut val) } {
			Some(val)
		} else {
			None
		}
	}
	pub fn get_f32(&self, key: &str) -> Option<f32> {
		let c_key = CString::new(key).unwrap();
		let mut val: f32 = 0.0;

		if unsafe { conf_get_f(self.conf, c_key.as_ptr(), &mut val) } {
			Some(val)
		} else {
			None
		}
	}
	pub fn get_f64(&self, key: &str) -> Option<f64> {
		let c_key = CString::new(key).unwrap();
		let mut val: f64 = 0.0;

		if unsafe { conf_get_d(self.conf, c_key.as_ptr(), &mut val) } {
			Some(val)
		} else {
			None
		}
	}
	pub fn get_f64_exact(&self, key: &str) -> Option<f64> {
		let c_key = CString::new(key).unwrap();
		let mut val: f64 = 0.0;

		if unsafe { conf_get_da(self.conf, c_key.as_ptr(), &mut val) } {
			Some(val)
		} else {
			None
		}
	}
	pub fn get_bool(&self, key: &str) -> Option<bool> {
		let c_key = CString::new(key).unwrap();
		let mut val: bool = false;

		if unsafe { conf_get_b2(self.conf, c_key.as_ptr(), &mut val) } {
			Some(val)
		} else {
			None
		}
	}
	pub fn get_data(&self, key: &str) -> Option<Vec<u8>> {
		unsafe {
			let c_key = CString::new(key).unwrap();
			let n = conf_get_data(self.conf, c_key.as_ptr(),
			    std::ptr::null_mut(), 0);
			if n != 0 {
				let mut buf: Vec<u8> = vec![0; n];
				conf_get_data(self.conf, c_key.as_ptr(),
				    buf.as_mut_ptr() as *mut c_void, buf.len());
				Some(buf)
			} else {
				None
			}
		}
	}
	pub fn remove_val(&self, key: &str) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_str(self.conf, c_key.as_ptr(),
			    std::ptr::null());
		}
	}
	pub fn set_str(&self, key: &str, value: &str) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			let c_value = CString::new(value).unwrap();
			conf_set_str(self.conf, c_key.as_ptr(),
			    c_value.as_ptr());
		}
	}
	pub fn set_i32(&self, key: &str, value: i32) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_i(self.conf, c_key.as_ptr(), value);
		}
	}
	pub fn set_i64(&self, key: &str, value: i64) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_lli(self.conf, c_key.as_ptr(), value);
		}
	}
	pub fn set_f32(&self, key: &str, value: f32) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_f(self.conf, c_key.as_ptr(), value);
		}
	}
	pub fn set_f64(&self, key: &str, value: f64) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_d(self.conf, c_key.as_ptr(), value);
		}
	}
	pub fn set_f64_exact(&self, key: &str, value: f64) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_da(self.conf, c_key.as_ptr(), value);
		}
	}
	pub fn set_bool(&self, key: &str, value: bool) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_b2(self.conf, c_key.as_ptr(), value);
		}
	}
	pub fn set_data(&self, key: &str, value: &Vec<u8>) {
		unsafe {
			let c_key = CString::new(key).unwrap();
			conf_set_data(self.conf, c_key.as_ptr(),
			    value.as_ptr() as *const c_void, value.len());
		}
	}
	pub fn iter(&self) -> ConfIterator {
		ConfIterator { conf: self, cookie: std::ptr::null() }
	}
}

impl Clone for Conf {
	fn clone(&self) -> Self {
		unsafe {
			Conf { conf: conf_create_copy(self.conf) }
		}
	}
}

impl Drop for Conf {
	fn drop(&mut self) {
		unsafe {
			conf_free(self.conf)
		}
	}
}

impl std::fmt::Display for Conf {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
	    Result<(), std::fmt::Error> {
		if let Ok(str) = String::from_utf8(self.to_buf()) {
			write!(f, "{}", str)
		} else {
			Err(std::fmt::Error)
		}
	}
}

pub struct ConfIterator<'a> {
	conf: &'a Conf,
	cookie: *const c_void,
}

impl Iterator for ConfIterator<'_> {
	type Item = (String, String);

	fn next(&mut self) -> Option<Self::Item> {
		unsafe {
			let mut c_key: *const c_char = std::ptr::null();
			let mut c_value: *const c_char = std::ptr::null();

			if conf_walk(self.conf.conf, &mut c_key,
			    &mut c_value, &mut self.cookie) {
				let key = CStr::from_ptr(c_key)
				    .to_string_lossy()
				    .into_owned();
				let value = CStr::from_ptr(c_value)
				    .to_string_lossy()
				    .into_owned();
				Some((key, value))
			} else {
				None
			}
		}
	}
}

#[repr(C)]
pub struct conf_t {
	_unused: [u8; 0],
}

extern "C" {
	fn conf_create_empty() -> *mut conf_t;
	fn conf_create_copy(conf: *const conf_t) -> *mut conf_t;
	fn conf_free(conf: *mut conf_t);

	fn conf_read_file(filename: *const c_char, errline: *mut i32) ->
	    *mut conf_t;
	fn conf_read_buf(buf: *const c_void, cap: usize, errline: *mut i32) ->
	    *mut conf_t;

	fn conf_write_file(conf: *const conf_t, filename: *const c_char) ->
	    bool;
	fn conf_write_file2(conf: *const conf_t, filename: *const c_char,
	    compressed: bool) -> bool;
	fn conf_write_buf(conf: *const conf_t, buf: *mut c_void, cap: usize) ->
	    usize;

	fn conf_merge(conf_from: *const conf_t, conf_to: *mut conf_t);

	fn conf_get_str(conf: *const conf_t, key: *const c_char,
	    value: *mut*const c_char) -> bool;
	fn conf_get_i(conf: *const conf_t, key: *const c_char,
	    value: *mut i32) -> bool;
	fn conf_get_lli(conf: *const conf_t, key: *const c_char,
	    value: *mut i64) -> bool;
	fn conf_get_f(conf: *const conf_t, key: *const c_char,
	    value: *mut f32) -> bool;
	fn conf_get_d(conf: *const conf_t, key: *const c_char,
	    value: *mut f64) -> bool;
	fn conf_get_da(conf: *const conf_t, key: *const c_char,
	    value: *mut f64) -> bool;
	fn conf_get_b2(conf: *const conf_t, key: *const c_char,
	    value: *mut bool) -> bool;
	fn conf_get_data(conf: *const conf_t, key: *const c_char,
	    buf: *mut c_void, cap: usize) -> usize;
	fn conf_set_str(conf: *mut conf_t, key: *const c_char,
	    value: *const c_char);
	fn conf_set_i(conf: *mut conf_t, key: *const c_char, value: i32);
	fn conf_set_lli(conf: *mut conf_t, key: *const c_char, value: i64);
	fn conf_set_f(conf: *mut conf_t, key: *const c_char, value: f32);
	fn conf_set_d(conf: *mut conf_t, key: *const c_char, value: f64);
	fn conf_set_da(conf: *mut conf_t, key: *const c_char, value: f64);
	fn conf_set_b2(conf: *mut conf_t, key: *const c_char, value: bool);
	fn conf_set_data(conf: *mut conf_t, key: *const c_char,
	    buf: *const c_void, sz: usize);
	fn conf_walk(conf: *const conf_t, key: *mut*const c_char,
	    value: *mut*const c_char, cookie: *mut*const c_void) -> bool;
}
