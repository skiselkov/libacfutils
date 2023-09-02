/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

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
