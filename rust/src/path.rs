/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

/**
 * Variadic macro which takes a series of &str path segments, welds them
 * together using the platform-appropriate path separators, and returns
 * a PathBuf object.
 */
#[macro_export]
macro_rules! mkpathbuf {
    ( $( $x:expr ),* ) => {
	{
	    let new_path: PathBuf = [$( $x, )*].iter().collect();
	    new_path
	}
    };
}

/**
 * Variadic macro which takes a series of &str path segments, welds them
 * together using the platform-appropriate path separators, and returns
 * a String object.
 * @note The path components MUST be valid UTF-8, otherwise this macro
 * panics. If your inputs might not be valid UTF-8, use the
 * mkpathbuf() macro to create a PathBuf object instead.
 */
#[macro_export]
macro_rules! mkpathstring {
    ( $( $x:expr ),* ) => {
	{
	    let new_path: PathBuf = [$( $x, )*].iter().collect();
	    new_path.into_os_string().into_string()
		.expect("OS string isn't valid UTF-8")
	}
    };
}
