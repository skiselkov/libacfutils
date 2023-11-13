/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

fn add_test_config() {
	// use build_target::*;
	// let (plat_short, plat_long) = match target_os().unwrap() {
	//     Os::Windows => ("win64", "win-64"),
	//     Os::Linux => ("lin64", "linux-64"),
	//     Os::MacOs => ("mac64", "mac-64"),
	//     _ => unreachable!()
	// };
	// println!("cargo:rustc-link-search=native=../qmake/{}", plat_short);
	// println!("cargo:rustc-link-lib=static=acfutils");

	// println!("cargo:rustc-link-search=native=../curl/libcurl-{}/lib",
	//     plat_long);
	// println!("cargo:rustc-link-lib=static=curl");

	// println!("cargo:rustc-link-search=native=../ssl/openssl-{}/lib",
	//     plat_long);
	// println!("cargo:rustc-link-lib=static=crypto");
	// println!("cargo:rustc-link-lib=static=ssl");

	// println!("cargo:rustc-link-search=native=../zlib/zlib-{}/lib",
	//     plat_long);
	// println!("cargo:rustc-link-lib=static=z");
	vcpkg::find_package("curl").unwrap();
	vcpkg::find_package("openssl").unwrap();
	vcpkg::find_package("zlib").unwrap();
}

fn main() {
	if cfg!(feature = "test") {
		add_test_config();
	}
}
