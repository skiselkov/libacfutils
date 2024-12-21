/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use crate::logMsg;
use std::panic::PanicHookInfo;
use std::sync::Mutex;

static INITED: Mutex<Option<()>> = Mutex::new(None);

pub fn install_panic_handler() {
    let mut inited = INITED.lock().expect("Mutex is panicked");
    if !inited.is_some() {
        std::panic::set_hook(Box::new(|pi| panic_handler(pi)));
        *inited = Some(());
    }
}

fn panic_handler(pi: &PanicHookInfo) {
    if let Some(s) = pi.payload().downcast_ref::<&str>() {
        logMsg!(
            "{}\nBacktrace:\n{}",
            s,
            std::backtrace::Backtrace::force_capture()
        );
    } else if let Some(s) = pi.payload().downcast_ref::<String>() {
        logMsg!(
            "{}\nBacktrace:\n{}",
            s,
            std::backtrace::Backtrace::force_capture()
        );
    } else {
        logMsg!(
            "(unknown panic type)\nBacktrace:\n{}",
            std::backtrace::Backtrace::force_capture()
        );
    }
}
