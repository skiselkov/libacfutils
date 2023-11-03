/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::sync::Mutex;
use std::panic::PanicInfo;
use crate::logMsg;

static INITED: Mutex<Option<()>> = Mutex::new(None);

pub fn install_panic_handler() {
	let mut inited = INITED.lock().expect("Mutex is panicked");
	if !inited.is_some() {
		std::panic::set_hook(Box::new(|pi| panic_handler(pi)));
		*inited = Some(());
	}
}

fn panic_handler(pi: &PanicInfo) {
	logMsg!("{}\nBacktrace:\n{}",
	    pi, std::backtrace::Backtrace::force_capture());
}
