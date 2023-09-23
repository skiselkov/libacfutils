/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::ops::{Add, Sub, Mul, Div};
use std::time::Duration;

pub trait RoundTo {
	fn round_to(self, multiple: Self) -> Self;
}

macro_rules! impl_round_to {
    ($t:ty) => {
	impl RoundTo for $t {
		fn round_to(self, multiple: $t) -> $t {
			assert_ne!(multiple, 0.0);
			(self / multiple).round() * multiple
		}
	}
    };
}

impl_round_to!(f32);
impl_round_to!(f64);

pub trait FilterIn<T>
where
    T: Add<Output = T> + Mul<f64, Output = T>
{
	/**
	 * Provides a gradual method of integrating an old value until it
	 * approaches a new target value. This is used in iterative processes
	 * by calling the filter_in method repeatedly a certain time intervals
	 * (d_t = delta-time). As time progresses, self will gradually be
	 * made to approach new_val. The lag serves to make the approach
	 * slower or faster (e.g. a lag of 2 seconds makes self approach
	 * new_val with a ramp that is approximately 2 seconds long).
	 */
	fn filter_in(self, new_val: T, d_t: Duration, lag: Duration) -> T;
}

impl<T> FilterIn<T> for T
where
    T: Add<Output = T> + Mul<f64, Output = T>,
{
	fn filter_in(self, new_val: T, d_t: Duration, lag: Duration) -> T {
		assert!(d_t.as_secs_f64() >= 0.0);
		assert!(lag.as_secs_f64() >= 0.0);

		let alpha = 1.0 / (1.0 + d_t.as_secs_f64() / lag.as_secs_f64());
		self * alpha + new_val * (1.0 - alpha)
	}
}

pub fn lerp<T, Scalar>(x: T, y: T, w: Scalar) -> T
where
    T: Copy + Add<Output = T> + Sub<Output = T> + Mul<Scalar, Output = T>,
{
	x + (y - x) * w
}

mod tests {
	#[test]
	fn test_lerp() {
		assert_eq!(crate::math::lerp(5.0, 10.0, 0.0), 5.0);
		assert_eq!(crate::math::lerp(5.0, 10.0, 1.0), 10.0);
	}
}

pub fn fx_lin<Tx, Ty>(x: Tx, x1: Tx, y1: Ty, x2: Tx, y2: Ty) -> Ty
where
    Tx: Copy + PartialEq +
	Sub<Output = Tx> + Mul<Ty, Output = Ty> + Div<Output = Tx>,
    Ty: Copy + Add<Output = Ty> + Sub<Output = Ty>,
{
	assert!(x1 != x2);
	((x - x1) / (x2 - x1)) * (y2 - y1) + y1
}

pub fn fx_lin_multi<Tx, Ty>(x: Tx, points: &[(Tx, Ty)]) -> Ty
where
    Tx: Copy + PartialOrd + PartialEq +
	Sub<Output = Tx> + Mul<Ty, Output = Ty> + Div<Output = Tx>,
    Ty: Copy + Add<Output = Ty> + Sub<Output = Ty>,
{
	assert!(points.len() >= 2);

	// x outside of range to the left
	if x < points[0].0 {
		return fx_lin(x, points[0].0, points[0].1,
		    points[1].0, points[1].1);
	}
	for i in 0 .. points.len() - 1 {
		let p1 = &points[i];
		let p2 = &points[i + 1];

		if x <= p2.0 {
			return fx_lin(x, p1.0, p1.1, p2.0, p2.1);
		}
	}
	#[allow(clippy::needless_return)]
	// x outside of range to the right
	return fx_lin(x,
	    points[points.len() - 2].0, points[points.len() - 2].1,
	    points[points.len() - 1].0, points[points.len() - 1].1);
}
