/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::os::raw::c_void;

pub fn init() {
    unsafe { c_intf::crc64_init() }
}
pub fn new() -> u64 {
    let mut crc: u64 = 0;
    unsafe { c_intf::crc64_state_init_impl(&mut crc as *mut u64) };
    crc
}
pub fn append(crc: u64, input: &[u8]) -> u64 {
    unsafe { c_intf::crc64_append(crc, input.as_ptr() as *const c_void, input.len()) }
}
pub fn calc(input: &[u8]) -> u64 {
    unsafe { c_intf::crc64_append(new(), input.as_ptr() as *const c_void, input.len()) }
}
pub fn srand(seed: u64) {
    unsafe { c_intf::crc64_srand(seed) }
}
pub fn rand() -> u64 {
    unsafe { c_intf::crc64_rand() }
}
pub fn rand_fract() -> f64 {
    unsafe { c_intf::crc64_rand_fract() }
}
pub fn rand_normal(sigma: f64) -> f64 {
    unsafe { c_intf::crc64_rand_normal(sigma) }
}
mod c_intf {
    use std::os::raw::c_void;
    extern "C" {
        pub fn crc64_init();
        pub fn crc64_state_init_impl(crc: *mut u64);
        pub fn crc64_append(crc: u64, input: *const c_void, sz: usize) -> u64;
        pub fn crc64_srand(seed: u64);
        pub fn crc64_rand() -> u64;
        pub fn crc64_rand_fract() -> f64;
        pub fn crc64_rand_normal(sigma: f64) -> f64;
    }
}

mod tests {
    #[test]
    fn crc64_init_test() {
        crate::crc64::init()
    }
    #[test]
    fn crc64_calc_test() {
        crate::crc64::init();
        assert_eq!(
            crate::crc64::calc("Hello World!".as_bytes()),
            0x8afbadba3615901d
        );
        assert_eq!(
            crate::crc64::calc(
                concat!(
                    "Lorem ipsum dolor sit amet, consectetur ",
                    "adipiscing elit, sed do eiusmod tempor incididunt ut ",
                    "labore et dolore magna aliqua. Ut enim ad minim veniam, ",
                    "quis nostrud exercitation ullamco laboris nisi ut ",
                    "aliquip ex ea commodo consequat. Duis aute irure dolor ",
                    "in reprehenderit in voluptate velit esse cillum dolore ",
                    "eu fugiat nulla pariatur. Excepteur sint occaecat ",
                    "cupidatat non proident, sunt in culpa qui officia ",
                    "deserunt mollit anim id est laborum."
                )
                .as_bytes()
            ),
            0x1880b03012207bfa
        );
    }
    #[test]
    fn crc64_rand_test() {
        crate::crc64::init();
        crate::crc64::srand(0xe8a89de49c32f4ef);
        assert_eq!(crate::crc64::rand(), 0xa6c98d2919e3ffd1);
        assert_eq!(crate::crc64::rand(), 0xf659599943d882a);
        assert_eq!(crate::crc64::rand(), 0x6365e5b4030441d9);
    }
}
