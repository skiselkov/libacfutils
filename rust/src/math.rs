/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::ops::{Add, Sub, Mul};

pub trait RoundTo {
	fn round_to(self: Self, multiple: Self) -> Self;
}

macro_rules! impl_round_to {
    ($t:ty) => {
	impl RoundTo for $t {
		fn round_to(self: $t, multiple: $t) -> $t {
			assert_ne!(multiple, 0.0);
			(self / multiple).round() * multiple
		}
	}
    };
}

impl_round_to!(f32);
impl_round_to!(f64);

pub trait FilterIn {
	/**
	 * Provides a gradual method of integrating an old value until it
	 * approaches a new target value. This is used in iterative processes
	 * by calling the filter_in method repeatedly a certain time intervals
	 * (d_t = delta-time). As time progresses, old_val will gradually be
	 * made to approach new_val. The lag serves to make the approach
	 * slower or faster (e.g. a value of '2' and d_t in seconds makes
	 * old_val approach new_val with a ramp that is approximately 2
	 * seconds long).
	 *
	 * @note self and new_val must NOT be NAN or infinite.
	 */
	fn filter_in(&mut self, new_val: Self, d_t: f64, lag: f64);
	/**
	 * Same as `filter_in()`, but handles NAN and infinite values for
	 * self and new_val properly.
	 * - if new_val is NAN or infinite, new_val is returned,
	 * - else if self is NAN or infinite, new_val is returned,
	 *	(without gradual filtering).
	 * - otherwise this simply implements the filter_in algorithm.
	 */
	fn filter_in_nan(&mut self, new_val: Self, d_t: f64, lag: f64);
}

macro_rules! impl_filter_in {
    ($t:ty) => {
	impl FilterIn for $t {
		fn filter_in(&mut self, new_val: $t, d_t: f64, lag: f64) {
			assert!(self.is_finite());
			assert!(new_val.is_finite());
			assert!(d_t >= 0.0);
			assert!(lag >= 0.0);

			let alpha = (1.0 / (1.0 + d_t / lag)) as $t;
			*self = alpha * (*self) + (1.0 - alpha) * new_val;
		}
		fn filter_in_nan(&mut self, new_val: $t, d_t: f64, lag: f64) {
			assert!(d_t >= 0.0);
			assert!(lag >= 0.0);

			if (self.is_finite() && new_val.is_finite()) {
				let alpha = 1.0 / (1.0 + d_t / lag) as $t;
				*self = alpha * (*self) +
				    (1.0 - alpha) * new_val;
			} else {
				*self = new_val;
			}
		}
	}
    };
}

impl_filter_in!(f32);
impl_filter_in!(f64);

pub fn wavg<T>(x: T, y: T, w: T) -> T
where
    T: Add<Output = T> + Sub<Output = T> + Mul<Output = T> + Copy,
{
	x + (y - x) * w
}

mod tests {
	#[test]
	fn test_wavg() {
		assert_eq!(crate::math::wavg(5.0, 10.0, 0.0), 5.0);
		assert_eq!(crate::math::wavg(5.0, 10.0, 1.0), 10.0);
	}
}
