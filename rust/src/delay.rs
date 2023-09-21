/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::time::{Duration, Instant};
use std::cmp::PartialEq;
use crate::crc64;

#[derive(Clone, Debug)]
pub struct DelayLine<T: PartialEq + Clone> {
	values: Vec<DelayValue<T>>,
	now_func: fn() -> Instant,
	delay_cur: Duration,
	delay_base: Duration,
	delay_rand: f64,
}

#[derive(Clone, Debug)]
struct DelayValue<T: PartialEq + Clone> {
	value: T,
	entered: Instant,
}

impl<T> DelayLine<T>
where
    T: PartialEq + Clone,
{
	pub fn new(startval: T, delay: Duration) -> Self {
		Self::new_custom(startval, delay, Instant::now)
	}
	pub fn new_custom(start_value: T, delay: Duration,
	    now_func: fn() -> Instant) -> Self {
		let curval = DelayValue {
		    value: start_value,
		    entered: (now_func)(),
		};
		Self {
		    values: vec![curval],
		    now_func,
		    delay_cur: delay,
		    delay_base: delay,
		    delay_rand: 0.0,
		}
	}
	pub fn push(&mut self, newval: T) -> T {
		assert!(!self.values.is_empty());
		let latest = &self.values[self.values.len() - 1];
		if latest.value != newval {
			self.values.push(DelayValue {
			    value: newval,
			    entered: (self.now_func)(),
			});
		}
		self.pull()
	}
	pub fn push_imm(&mut self, newval: T) -> T {
		self.values = vec![DelayValue {
		    value: newval.clone(),
		    entered: (self.now_func)(),
		}];
		newval
	}
	pub fn pull(&mut self) -> T {
		let now = (self.now_func)();
		assert!(!self.values.is_empty());
		/*
		 * While we have more than 1 value in the value stack, check
		 * to see if the newer values have become active yet.
		 */
		while self.values.len() > 1 && now.duration_since(
		    self.values[1].entered) >= self.delay_cur {
			// value in slot [1] has become the new current value,
			// so shift it over by removing value in slot [0]
			self.values.remove(0);
			self.delay_cur = Self::recomp_delay(self.delay_base,
			    self.delay_rand);
		}
		self.values[0].value.clone()
	}
	pub fn peek(&self) -> T {
		assert!(!self.values.is_empty());
		self.values[0].value.clone()
	}
	pub fn set_delay(&mut self, delay: Duration) {
		if self.delay_base != delay {
			self.delay_base = delay;
			self.delay_cur = Self::recomp_delay(self.delay_base,
			    self.delay_rand);
		}
	}
	pub fn get_delay(&self) -> Duration {
		self.delay_base
	}
	pub fn get_delay_cur(&self) -> Duration {
		self.delay_cur
	}
	pub fn set_rand(&mut self, delay_rand: f64) {
		self.delay_rand = delay_rand;
		self.delay_cur = Self::recomp_delay(self.delay_base,
		    self.delay_rand);
	}
	pub fn get_rand(&self) -> f64 {
		self.delay_rand
	}
	pub fn get_time_since_change(&self) -> Duration {
		assert!(!self.values.is_empty());
		let latest = &self.values[self.values.len() - 1];
		(self.now_func)().duration_since(latest.entered)
	}
	fn recomp_delay(delay: Duration, rand_fact: f64) -> Duration {
		if rand_fact != 0.0 {
			assert!(rand_fact >= 0.0 && rand_fact <= 1.0);
			let rand_us = rand_fact * delay.as_micros() as f64;
			let delay_us = delay.as_micros() as f64 +
			    ((crc64::rand_fract() - 0.5) * rand_us).round();
			assert!(delay_us >= 0.0);
			Duration::from_micros(delay_us as u64)
		} else {
			delay
		}
	}
}

mod tests {
	#[test]
	fn delayline_test() {
		use std::time::Duration;
		use crate::delay::DelayLine;

		let mut dl: DelayLine<i32> = DelayLine::new(0,
		    Duration::from_millis(100));
		for i in 1..=5 {
			println!("pushing {}, cur: {}", i, dl.push(i));
			std::thread::sleep(Duration::from_millis(50));
		}
		assert_ne!(dl.peek(), 0);
	}
}
