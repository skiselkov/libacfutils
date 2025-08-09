/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

use std::marker::Send;
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};

#[derive(Debug)]
pub struct Worker<T: Clone + Send + 'static> {
    thread: Option<std::thread::JoinHandle<()>>,
    data: WorkerData<T>,
}

type WorkerData<T> = Arc<(Mutex<WorkerConfig<T>>, Condvar)>;

#[derive(Debug)]
struct WorkerConfig<T: Clone> {
    intval: Duration,
    shutdown: bool,
    init_func: Option<fn(T)>,
    worker_func: fn(T),
    fini_func: Option<fn(T)>,
    arg: T,
}

fn worker_run<T: Clone + Send + 'static>(wk: WorkerData<T>) {
    /* Don't hold the lock while calling init_func */
    let (init_func_opt, init_arg) = {
        let data = wk.0.lock().expect("mutex is in a panicked state");
        (data.init_func, data.arg.clone())
    };
    if let Some(init_func) = init_func_opt {
        (init_func)(init_arg);
    }
    let mut last = Instant::now();
    loop {
        /*
         * Grab the lock to check whether we should exit. If not,
         * copy out the function & argument and drop the lock.
         */
        let (worker_func, arg) = {
            let data = wk.0.lock().expect("mutex is in a panicked state");
            if data.shutdown {
                break;
            }
            (data.worker_func, data.arg.clone())
        };
        /* Don't hold the worker lock while in the user callback */
        (worker_func)(arg);
        {
            /* Reacquire the lock and wait on the condvar */
            let data = wk.0.lock().expect("mutex is in a panicked state");
            let next = last + data.intval;
            let now = Instant::now();
            /*
             * If execution is fast enough that there is time
             * remaining until the next period, go to sleep and
             * advance at fixed steps from the initial starting
             * point. Otherwise, simply start the next loop
             * iteration right away and jump `last` up to our
             * present time immediately, to avoid playing
             * catch-up with real time - we will simply run as
             * fast as we can.
             */
            if next > now {
                _ = wk.1.wait_timeout(data, next - now);
                last = next;
            } else {
                last = now;
            }
        }
    }
    /* Don't hold the lock while calling fini_func */
    let (fini_func_opt, fini_arg) = {
        let data = wk.0.lock().expect("mutex is in a panicked state");
        (data.fini_func, data.arg.clone())
    };
    if let Some(fini_func) = fini_func_opt {
        (fini_func)(fini_arg);
    }
}

#[derive(Debug, Clone)]
pub struct WorkerAlreadyStartedError {}
impl std::fmt::Display for WorkerAlreadyStartedError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "worker already started")
    }
}

impl<T: Clone + Send + 'static> Worker<T> {
    pub fn new(
        intval: Duration,
        init_func: Option<fn(T)>,
        worker_func: fn(T),
        fini_func: Option<fn(T)>,
        arg: T,
    ) -> Self {
        let wk = Arc::new((
            Mutex::new(WorkerConfig {
                intval,
                init_func,
                worker_func,
                fini_func,
                arg,
                shutdown: false,
            }),
            Condvar::new(),
        ));
        Worker {
            data: wk,
            thread: None,
        }
    }
    pub fn start(&mut self) -> Result<(), WorkerAlreadyStartedError> {
        match self.thread {
            None => {
                let wk_data = Arc::clone(&self.data);
                self.thread = Some(std::thread::spawn(|| worker_run(wk_data)));
                Ok(())
            }
            Some(_) => Err(WorkerAlreadyStartedError {}),
        }
    }
    pub fn is_started(&self) -> bool {
        self.thread.is_some()
    }
    pub fn get_interval(&self) -> Duration {
        self.data
            .0
            .lock()
            .expect("mutex is in a panicked state")
            .intval
    }
    pub fn set_interval(&mut self, intval: Duration) {
        let mut wk = self.data.0.lock().expect("mutex is in a panicked state");
        wk.intval = intval;
        self.data.1.notify_one();
    }
    pub fn set_interval_nowake(&mut self, intval: Duration) {
        self.data
            .0
            .lock()
            .expect("mutex is in a panicked state")
            .intval = intval;
    }
    pub fn wake_up(&mut self) {
        self.data.1.notify_one();
    }
}

impl<T: Clone + Send + 'static> Drop for Worker<T> {
    fn drop(&mut self) {
        {
            /* Notify the worker thread to shut down immediately */
            let mut data =
                self.data.0.lock().expect("mutex is in a panicked state");
            data.shutdown = true;
            self.data.1.notify_one();
        }
        if let Some(thread) = self.thread.take() {
            thread.join().expect("worker thread has panicked");
        }
    }
}

mod tests {
    #[test]
    fn worker_test() {
        use std::sync::{Arc, Mutex};
        use std::time::Duration;
        type Counter = Arc<Mutex<i32>>;

        let counter = Arc::new(Mutex::new(0));
        let counter_wk = Arc::clone(&counter);
        let init_closure = |_: Counter| {
            println!("Worker: started up!");
        };
        let run_closure = |ctr_in: Counter| {
            println!("Worker: run_closure called");
            let mut ctr = ctr_in.lock().unwrap();
            *ctr += 1;
        };
        let fini_closure = |_: Counter| {
            println!("Worker: shutting down!");
        };
        let mut wk = crate::worker::Worker::new(
            Duration::from_millis(200),
            Some(init_closure),
            run_closure,
            Some(fini_closure),
            counter_wk,
        );
        wk.start()
            .expect("Worker shouldn't have already been running");
        loop {
            {
                let ctr = counter.lock().unwrap();
                if *ctr >= 4 {
                    println!(
                        concat!("Control: reached ", "limit ({}), exiting"),
                        *ctr
                    );
                    break;
                }
                println!("Control: counter = {}", ctr);
            }
            std::thread::sleep(Duration::from_millis(100));
        }
    }
}
