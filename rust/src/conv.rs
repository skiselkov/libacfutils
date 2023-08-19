/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

pub fn radsec2rpm(radsec: f64) -> f64 {
	(radsec / (2.0 * std::f64::consts::PI)) * 60.0
}

pub fn rpm2radsec(rpm: f64) -> f64 {
	(rpm / 60.0) * 2.0 * std::f64::consts::PI
}
