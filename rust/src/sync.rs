/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::sync::{Mutex, MutexGuard};
use std::sync::{RwLock, RwLockReadGuard, RwLockWriteGuard};

/**
 * Adds the f_read() and f_write() methods to std::sync::RwLock.
 * These add "force" read and write methods, which automatically
 * panic if the underlying lock is in a panicked state. Basically
 * saves you the need to call unwrap() or expect() on the lock result.
 */
pub trait RwLockForceOps<T> {
    /**
     * Force-read() lock on a std::sync::RwLock. This acquires the
     * lock for reading, or panic if the underlying lock is in a
     * panicked state. Basically saves you the need to call unwrap()
     * or expect() on the lock result.
     * @return The RwLockReadGuard, which allows you to access the
     *     protected data structure safely.
     */
    fn f_read(&self) -> RwLockReadGuard<'_, T>;
    /**
     * Force-write() lock on a std::sync::RwLock. This acquires the
     * lock for writing, or panic if the underlying lock is in a
     * panicked state. Basically saves you the need to call unwrap()
     * or expect() on the lock result.
     * @return The RwLockWriteGuard, which allows you to access the
     *     protected data structure safely.
     */
    fn f_write(&self) -> RwLockWriteGuard<'_, T>;
}

impl<T> RwLockForceOps<T> for RwLock<T> {
    fn f_read(&self) -> RwLockReadGuard<'_, T> {
        self.read().expect("Cannot RwLock.read(): lock is panicked")
    }
    fn f_write(&self) -> RwLockWriteGuard<'_, T> {
        self.write()
            .expect("Cannot RwLock.write(): lock is panicked")
    }
}

/**
 * Adds the f_lock() method to std::sync::Mutex. This is a "force" lock
 * method, which automatically panics if the underlying lock is in a
 * panicked state. Basically saves you the need to call unwrap() or
 * expect() on the lock result.
 */
pub trait MutexForceOps<T> {
    /**
     * Force-lock()s a std::sync::Mutex. This acquires the mutex, or
     * panicks if the underlying lock is in a panicked state. Basically
     * saves you the need to call unwrap() or expect() on the lock result.
     * @return The MutexGuard, which allows you to access the protected
     *     data structure safely.
     */
    fn f_lock(&self) -> MutexGuard<'_, T>;
}

impl<T> MutexForceOps<T> for Mutex<T> {
    fn f_lock(&self) -> MutexGuard<'_, T> {
        self.lock().expect("Cannot Mutex.lock(): lock is panicked")
    }
}
