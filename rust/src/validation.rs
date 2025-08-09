/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use crate::phys::conv::feet2met;

pub trait AltitudeValidation: Sized {
    fn validate_alt_m(self) -> Option<Self>;
    fn validate_alt_ft(self) -> Option<Self>;
}

macro_rules! impl_altitude_validation {
    ($t:ty) => {
        impl AltitudeValidation for $t {
            fn validate_alt_m(self) -> Option<Self> {
                if self >= feet2met(-2000.0) as $t
                    && self <= feet2met(100000.0) as $t
                {
                    Some(self)
                } else {
                    None
                }
            }
            fn validate_alt_ft(self) -> Option<Self> {
                if self >= -2000 as $t && self <= 100000 as $t {
                    Some(self)
                } else {
                    None
                }
            }
        }
    };
}

impl_altitude_validation!(i32);
impl_altitude_validation!(i64);
impl_altitude_validation!(f32);
impl_altitude_validation!(f64);

pub trait CourseValidation: Sized {
    fn validate_crs_deg(self) -> Option<Self>;
    fn validate_crs_rad(self) -> Option<Self>;
}

macro_rules! impl_course_validation {
    ($t:ty) => {
        impl CourseValidation for $t {
            fn validate_crs_deg(self) -> Option<Self> {
                if (0.0..=360.0).contains(&self) {
                    Some(self)
                } else {
                    None
                }
            }
            fn validate_crs_rad(self) -> Option<Self> {
                if self >= 0.0 && self <= std::f64::consts::PI as $t {
                    Some(self)
                } else {
                    None
                }
            }
        }
    };
}

impl_course_validation!(f32);
impl_course_validation!(f64);
