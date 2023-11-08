/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::ffi::{CStr, CString};

pub fn icao2cc(icao: &str) -> Option<&'static str> {
	mod c_intf {
		use std::os::raw::c_char;
		extern "C" {
			pub fn icao2cc(icao: *const c_char) -> *const c_char;
		}
	}
	let c_icao = CString::new(icao)
	    .expect("`value` contains a stray NUL byte");
	let c_cc = unsafe { c_intf::icao2cc(c_icao.as_ptr()) };
	if !c_cc.is_null() {
		let cc = unsafe { CStr::from_ptr(c_cc) };
		Some(cc.to_str()
		    .expect("returned country code isn't valid UTF-8"))
	} else {
		None
	}
}

pub fn icao2lang(icao: &str) -> Option<&'static str> {
	mod c_intf {
		use std::os::raw::c_char;
		extern "C" {
			pub fn icao2lang(icao: *const c_char) -> *const c_char;
		}
	}
	let c_icao = CString::new(icao)
	    .expect("`value` contains a stray NUL byte");
	let c_lang = unsafe { c_intf::icao2lang(c_icao.as_ptr()) };
	assert!(!c_lang.is_null());
	let lang = unsafe { CStr::from_ptr(c_lang) }.to_str()
	    .expect("returned country code isn't valid UTF-8");
	if lang != "XX" {
		Some(lang)
	} else {
		None
	}
}

mod tests {
	#[test]
	fn icao2cc_test() {
		use crate::icao2cc::icao2cc;
		assert!(icao2cc("KSFO").unwrap() == "US");
		assert!(icao2cc("EDDF").unwrap() == "DE");
		assert!(icao2cc("CYQB").unwrap() == "CA");
		assert!(icao2cc("ZZZZ") == None);
	}
	#[test]
	fn icao2lang_test() {
		use crate::icao2cc::icao2lang;
		assert!(icao2lang("KSFO").unwrap() == "en");
		assert!(icao2lang("EDDF").unwrap() == "de");
		assert!(icao2lang("CYQB").unwrap() == "fr");
		assert!(icao2lang("CYVR").unwrap() == "en");
		assert!(icao2lang("UUWW").unwrap() == "ru");
		assert!(icao2lang("ZZZZ") == None);
	}
}
