/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use crate::crc64;
use serde::{Deserialize, Serialize};
use std::cmp::PartialEq;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

#[derive(Serialize, Deserialize)]
#[serde(from = "SerializedDelayLine<T>", into = "SerializedDelayLine<T>")]
pub struct DelayLine<T: PartialEq + Clone> {
    values: Vec<DelayDataValue<T>>,
    delay_cur: Duration,
    delay_base: Duration,
    delay_rand: f64,
    now_closure: DelayTimer,
}

struct DelayTimer(Box<dyn Fn() -> f64>);

#[derive(Clone, Serialize, Deserialize)]
struct DelayDataValue<T: PartialEq + Clone> {
    value: T,
    entered: f64,
}

impl<T> DelayLine<T>
where
    T: PartialEq + Clone,
{
    pub fn new(startval: T, delay: Duration) -> Self {
        DelayLine::new_custom(startval, delay, DelayTimer::default().0)
    }
    pub fn new_custom<F: Fn() -> f64 + 'static>(
        start_value: T,
        delay: Duration,
        now_closure: F,
    ) -> Self {
        let curval = DelayDataValue {
            value: start_value,
            entered: now_closure(),
        };
        Self {
            values: vec![curval],
            now_closure: DelayTimer(Box::new(now_closure)),
            delay_cur: delay,
            delay_base: delay,
            delay_rand: 0.0,
        }
    }
    pub fn set_time_func<F: Fn() -> f64 + 'static>(&mut self, func: F) {
        let old_time_base = (self.now_closure.0)();
        self.now_closure = DelayTimer(Box::new(func));
        let new_time_base = (self.now_closure.0)();
        // Recompute all data entered times in terms of the new closure
        self.values = std::mem::take(&mut self.values)
            .into_iter()
            .map(|value| DelayDataValue {
                value: value.value,
                entered: value.entered - old_time_base + new_time_base,
            })
            .collect();
    }
    pub fn push(&mut self, newval: T) -> T {
        assert!(!self.values.is_empty());
        let latest = &self.values[self.values.len() - 1];
        if latest.value != newval {
            self.values.push(DelayDataValue {
                value: newval,
                entered: (self.now_closure.0)(),
            });
        }
        self.pull()
    }
    pub fn push_imm(&mut self, newval: T) -> T {
        self.values = vec![DelayDataValue {
            value: newval.clone(),
            entered: (self.now_closure.0)(),
        }];
        newval
    }
    pub fn pull(&mut self) -> T {
        let now = (self.now_closure.0)();
        assert!(!self.values.is_empty());
        /*
         * While we have more than 1 value in the value stack, check
         * to see if the newer values have become active yet.
         */
        while self.values.len() > 1
            && now - self.values[1].entered >= self.delay_cur.as_secs_f64()
        {
            // value in slot [1] has become the new current value,
            // so shift it over by removing value in slot [0]
            self.values.remove(0);
            self.delay_cur =
                Self::recomp_delay(self.delay_base, self.delay_rand);
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
            self.delay_cur =
                Self::recomp_delay(self.delay_base, self.delay_rand);
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
        self.delay_cur = Self::recomp_delay(self.delay_base, self.delay_rand);
    }
    pub fn get_rand(&self) -> f64 {
        self.delay_rand
    }
    pub fn get_time_since_change(&self) -> Duration {
        assert!(!self.values.is_empty());
        let latest = &self.values[self.values.len() - 1];
        Duration::from_secs_f64((self.now_closure.0)() - latest.entered)
    }
    fn recomp_delay(delay: Duration, rand_fact: f64) -> Duration {
        if rand_fact != 0.0 {
            assert!((0.0..=1.0).contains(&rand_fact));
            let rand_us = rand_fact * delay.as_micros() as f64;
            let delay_us = delay.as_micros() as f64
                + ((crc64::rand_fract() - 0.5) * rand_us).round();
            assert!(delay_us >= 0.0);
            Duration::from_micros(delay_us as u64)
        } else {
            delay
        }
    }
}

impl<T: PartialEq + Clone> std::fmt::Debug for DelayLine<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DelayLine")
            .field("delay_cur", &self.delay_cur)
            .field("delay_base", &self.delay_base)
            .field("delay_rand", &self.delay_rand)
            .finish()
    }
}

impl Default for DelayTimer {
    fn default() -> Self {
        DelayTimer(Box::new(|| {
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("System time earlier than Jan 1 1970?")
                .as_secs_f64()
        }))
    }
}

#[derive(Serialize, Deserialize, Clone)]
struct SerializedDelayLine<T: PartialEq + Clone> {
    values: Vec<DelayDataValue<T>>,
    delay_cur: Duration,
    delay_base: Duration,
    delay_rand: f64,
}

impl<T> Clone for DelayLine<T>
where
    T: PartialEq + Clone,
{
    fn clone(&self) -> Self {
        let now_closure = DelayTimer::default();
        let old_time_base = (self.now_closure.0)();
        let new_time_base = (now_closure.0)();
        let values = self
            .values
            .iter()
            .map(|x| DelayDataValue {
                value: x.value.clone(),
                entered: x.entered - old_time_base + new_time_base,
            })
            .collect();
        Self {
            values,
            delay_cur: self.delay_cur,
            delay_base: self.delay_base,
            delay_rand: self.delay_rand,
            now_closure,
        }
    }
}

impl<T> From<DelayLine<T>> for SerializedDelayLine<T>
where
    T: PartialEq + Clone,
{
    fn from(val: DelayLine<T>) -> Self {
        let time_base = (val.now_closure.0)();
        let values = val
            .values
            .into_iter()
            .map(|v| DelayDataValue {
                value: v.value,
                entered: v.entered - time_base,
            })
            .collect();
        SerializedDelayLine {
            values,
            delay_cur: val.delay_cur,
            delay_base: val.delay_base,
            delay_rand: val.delay_rand,
        }
    }
}

impl<T> From<SerializedDelayLine<T>> for DelayLine<T>
where
    T: PartialEq + Clone,
{
    fn from(ser: SerializedDelayLine<T>) -> Self {
        let now_closure = DelayTimer::default();
        let time_base = (now_closure.0)();
        let values = ser
            .values
            .into_iter()
            .map(|v| DelayDataValue {
                value: v.value,
                entered: v.entered + time_base,
            })
            .collect();
        Self {
            values,
            delay_cur: ser.delay_cur,
            delay_base: ser.delay_base,
            delay_rand: ser.delay_rand,
            now_closure,
        }
    }
}

mod tests {
    #[test]
    fn delayline_test() {
        use crate::delay::DelayLine;
        use std::time::Duration;

        let mut dl = DelayLine::new(0, Duration::from_millis(100));
        for i in 1..=5 {
            println!("pushing {}, cur: {}", i, dl.push(i));
            std::thread::sleep(Duration::from_millis(50));
        }
        assert_ne!(dl.peek(), 0);

        let serialized =
            serde_json::to_string(&dl).expect("Serialization failed");
        println!("Serialized JSON: {}", serialized);
        let mut dl_deserd = serde_json::from_str::<DelayLine<i32>>(&serialized)
            .expect("Deserialization failed");
        // Wait for 500ms to make sure the delay line has
        // fully progressed through its value set
        std::thread::sleep(Duration::from_millis(500));
        assert_eq!(dl_deserd.pull(), 5);
    }
}
