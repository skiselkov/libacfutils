/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::sync::{RwLock, RwLockReadGuard, RwLockWriteGuard};
use std::sync::{Mutex, MutexGuard};

pub trait RwLockSafeOps<T> {
	fn read_safe(&self) -> RwLockReadGuard<'_, T>;
	fn write_safe(&self) -> RwLockWriteGuard<'_, T>;
}

impl<T> RwLockSafeOps<T> for RwLock<T> {
	fn read_safe(&self) -> RwLockReadGuard<'_, T> {
		self.read().expect("Cannot RwLock.read(): lock is panicked")
	}
	fn write_safe(&self) -> RwLockWriteGuard<'_, T> {
		self.write().expect("Cannot RwLock.write(): lock is panicked")
	}
}

pub trait MutexSafeOps<T> {
	fn lock_safe(&self) -> MutexGuard<'_, T>;
}

impl<T> MutexSafeOps<T> for Mutex<T> {
	fn lock_safe(&self) -> MutexGuard<'_, T> {
		self.lock().expect("Cannot Mutex.lock(): lock is panicked")
	}
}
