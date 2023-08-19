/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use build_target::*;

fn main() {
	let (plat_short, _plat_long) = match target_os().unwrap() {
	Os::Windows => ("win64", "win-64"),
	Os::Linux => ("lin64", "linux-64"),
	Os::MacOs => ("mac64", "mac-64"),
	_ => unreachable!()
	};
	println!("cargo:rustc-link-search=native=../qmake/{}", plat_short);
	println!("cargo:rustc-link-lib=static=acfutils");
}
