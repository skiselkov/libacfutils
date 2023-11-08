/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

pub mod units {
	use crate::geom::conv::*;

	#[derive(Clone, Debug)]
	#[allow(non_camel_case_types)]
	pub enum Normalize {
	    NormNone,
	    Norm_0_2,
	    Norm_m1_1,
	}
	#[derive(Clone, Debug)]
	pub struct Angle {
		a: f64,		/* radians */
	}
	impl Angle {
		pub fn from_rad(rad: f64) -> Self {
			assert!(rad.is_finite());
			Self { a: rad }
		}
		pub fn from_deg(deg: f64) -> Self {
			assert!(deg.is_finite());
			Self { a: deg2rad(deg) }
		}
		pub fn as_rad(&self, normalize: Normalize) -> f64 {
			match normalize {
			    Normalize::NormNone => self.a,
			    Normalize::Norm_0_2 =>
				Self::normalize_0_2(self.a),
			    Normalize::Norm_m1_1 =>
				Self::normalize_m1_1(self.a),
			}
		}
		pub fn as_deg(&self, normalize: Normalize) -> f64 {
			match normalize {
			    Normalize::NormNone => rad2deg(self.a),
			    Normalize::Norm_0_2 => rad2deg(
				Self::normalize_0_2(self.a)),
			    Normalize::Norm_m1_1 => rad2deg(
				Self::normalize_m1_1(self.a)),
			}
		}
		fn normalize_m1_1(a: f64) -> f64 {
			let a = Self::normalize_0_2(a);
			if a > 180.0 {
				a - 360.0
			} else {
				a
			}
		}
		fn normalize_0_2(a: f64) -> f64 {
			const TWO_PI: f64 = 2.0 * std::f64::consts::PI;
			let mut a = a % TWO_PI;
			/* Flip negative into positive */
			if a < 0.0 {
				a = (a + TWO_PI).clamp(0.0, TWO_PI);
			}
			/* Avoid negative zero */
			if a == 0.0 {
				a = 0.0;
			}
			a
		}
	}
	impl std::ops::Sub for Angle {
		type Output = Angle;
		fn sub(self, rhs: Angle) -> Self::Output {
			use std::f64::consts::PI;
			let a1 = Self::normalize_0_2(rhs.a);
			let a2 = Self::normalize_0_2(self.a);
			if a1 > a2 {
				if a1 > a2 + PI {
					Self { a: (2.0 * PI) -a1 + a2 }
				} else {
					Self { a: a2 - a1 }
				}
			} else {
				if a2 > a1 + PI {
					Self { a: -((2.0 * PI) - a2 + a1) }
				} else {
					Self { a: a2 - a1 }
				}
			}
		}
	}
	impl std::cmp::PartialEq for Angle {
		fn eq(&self, other: &Self) -> bool {
			(self.as_rad(Normalize::Norm_0_2) -
			    other.as_rad(Normalize::Norm_0_2)).abs() < 1e-12
		}
	}
}

pub mod conv {
	pub fn deg2rad(deg: f64) -> f64 {
		assert!(deg.is_finite());
		(deg / 360.0) * 2.0 * std::f64::consts::PI
	}
	pub fn rad2deg(rad: f64) -> f64 {
		assert!(rad.is_finite());
		(rad / (2.0 * std::f64::consts::PI)) * 360.0
	}
}
