/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2024 Saso Kiselkov. All rights reserved.
 */

use std::time::Duration;

pub struct PidCtl {
    e_prev: Option<f64>,
    v_prev: Option<f64>,
    integ: Option<f64>,
    deriv: Option<f64>,

    pub k_p_gain: f64,
    pub k_p: f64,
    pub k_i_gain: f64,
    pub k_i: f64,
    pub k_d_gain: f64,
    pub k_d: f64,
    pub r_d: Duration,
}

impl PidCtl {
    pub fn new(k_p: f64, k_i: f64, k_d: f64, r_d: Duration) -> PidCtl {
        PidCtl {
            e_prev: None,
            v_prev: None,
            integ: None,
            deriv: None,
            k_p_gain: 1.0,
            k_p,
            k_i_gain: 1.0,
            k_i,
            k_d_gain: 1.0,
            k_d,
            r_d,
        }
    }
    pub fn update(&mut self, e: Option<f64>, d_t: Duration) {
        self.update_dv(e, e, d_t);
    }
    pub fn update_dv(&mut self, e: Option<f64>, v: Option<f64>, d_t: Duration) {
        match (e, v) {
            (Some(e), Some(v)) => self.update_with_values(e, v, d_t),
            _ => self.reset(),
        }
    }
    fn update_with_values(&mut self, e: f64, v: f64, d_t: Duration) {
        use crate::math::FilterIn;
        let integ = self.integ.unwrap_or(0.0);
        self.integ = Some(integ + e * d_t.as_secs_f64());
        if let Some(v_prev) = self.v_prev {
            let delta_v = (v - v_prev) / d_t.as_secs_f64();
            if let Some(deriv) = self.deriv {
                self.deriv = Some(deriv.filter_in(delta_v, d_t, self.r_d));
            } else {
                self.deriv = Some(delta_v);
            }
        }
        self.e_prev = Some(e);
        self.v_prev = Some(v);
    }
    pub fn get(&self) -> Option<f64> {
        if let (Some(e_prev), Some(integ), Some(deriv)) =
            (self.e_prev, self.integ, self.deriv)
        {
            Some(
                self.k_p_gain * self.k_p * e_prev
                    + self.k_i_gain * self.k_i * integ
                    + self.k_d_gain * self.k_d * deriv,
            )
        } else {
            None
        }
    }
    pub fn reset(&mut self) {
        self.e_prev = None;
        self.v_prev = None;
        self.integ = None;
        self.deriv = None;
    }
}
