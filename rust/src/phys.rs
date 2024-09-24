/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#![allow(non_snake_case)]

pub mod units {
	use crate::phys::conv::*;
	use serde::{Serialize, Deserialize};

	macro_rules! impl_units_ops {
	    ($type: ty, $field:ident) => {
		impl std::ops::Add for $type {
			type Output = $type;
			fn add(self, rhs: $type) -> Self::Output {
				Self::Output {
				    $field: self.$field + rhs.$field
				}
			}
		}
		impl std::ops::Sub for $type {
			type Output = $type;
			fn sub(self, rhs: $type) -> Self::Output {
				Self::Output {
				    $field: self.$field - rhs.$field
				}
			}
		}
		impl std::ops::Mul for $type {
			type Output = $type;
			fn mul(self, rhs: $type) -> Self::Output {
				Self::Output {
				    $field: self.$field * rhs.$field
				}
			}
		}
		impl std::ops::Div for $type {
			type Output = $type;
			fn div(self, rhs: $type) -> Self::Output {
				Self::Output {
				    $field: self.$field / rhs.$field
				}
			}
		}
	    }
	}
	macro_rules! impl_units_ops_check {
	    ($type: ty, $field:ident, $op:ident) => {
		impl std::ops::Add for $type {
			type Output = $type;
			fn add(self, rhs: $type) -> Self::Output {
				assert!((self.$field + rhs.$field).$op(&0.0));
				Self::Output {
				    $field: self.$field + rhs.$field
				}
			}
		}
		impl std::ops::Sub for $type {
			type Output = $type;
			fn sub(self, rhs: $type) -> Self::Output {
				assert!((self.$field - rhs.$field).$op(&0.0));
				Self::Output {
				    $field: self.$field - rhs.$field
				}
			}
		}
		impl std::ops::Mul for $type {
			type Output = $type;
			fn mul(self, rhs: $type) -> Self::Output {
				assert!((self.$field * rhs.$field).$op(&0.0));
				Self::Output {
				    $field: self.$field * rhs.$field
				}
			}
		}
		impl std::ops::Div for $type {
			type Output = $type;
			fn div(self, rhs: $type) -> Self::Output {
				assert!((self.$field / rhs.$field).$op(&0.0));
				Self::Output {
				    $field: self.$field / rhs.$field
				}
			}
		}
	    }
	}
	macro_rules! impl_units_ops_non_neg {
	    ($type: ty, $field: ident) => {
		impl_units_ops_check!($type, $field, ge);
	    }
	}
	macro_rules! impl_units_scalar_ops_one {
	    ($type: ty, $field: ident, $trait: ident, $op: ident,
		$scalar_type: ty) => {
		impl std::ops::$trait<$scalar_type> for $type {
			type Output = $type;
			fn $op(self, rhs: $scalar_type) -> Self::Output {
				assert!(rhs.is_finite());
				Self::Output {
				    $field: self.$field.$op(rhs as f64)
				}
			}
		}
	    };
	}
	macro_rules! impl_units_scalar_ops {
	    ($type: ty, $field: ident) => {
		impl_units_scalar_ops_one!($type, $field, Mul, mul, f32);
		impl_units_scalar_ops_one!($type, $field, Mul, mul, f64);
		impl_units_scalar_ops_one!($type, $field, Div, div, f32);
		impl_units_scalar_ops_one!($type, $field, Div, div, f64);
	    };
	}

	#[derive(Clone, Copy, Debug, PartialEq, PartialOrd, Serialize,
	    Deserialize)]
	pub struct Temperature {
		T: f64,		/* Kelvin */
	}
	impl Temperature {
		pub fn from_K(kelvin: f64) -> Self {
			assert!(kelvin.is_finite());
			assert!(kelvin > 0.0);
			Self { T: kelvin }
		}
		pub fn from_C(celsius: f64) -> Self {
			assert!(celsius >= -273.15);
			Self { T: c2kelvin(celsius) }
		}
		pub fn from_F(fahrenheit: f64) -> Self {
			assert!(fahrenheit > -459.67);
			Self { T: fah2kelvin(fahrenheit) }
		}
		pub fn as_K(&self) -> f64 {
			self.T
		}
		pub fn as_C(&self) -> f64 {
			kelvin2c(self.T)
		}
		pub fn as_F(&self) -> f64 {
			kelvin2fah(self.T)
		}
		pub const fn new_const(kelvin: f64) -> Self {
			Self { T: kelvin }
		}
	}
	impl std::ops::Add<TemperatureRelative> for Temperature {
		type Output = Temperature;
		fn add(self, rhs: TemperatureRelative) -> Self::Output {
			assert!(self.T + rhs.dT > 0.0);
			Self::Output { T: self.T + rhs.dT }
		}
	}
	impl std::ops::Sub for Temperature {
		type Output = TemperatureRelative;
		fn sub(self, rhs: Temperature) -> Self::Output {
			Self::Output { dT: self.T - rhs.T }
		}
	}
	impl std::ops::Mul for Temperature {
		type Output = Temperature;
		fn mul(self, rhs: Temperature) -> Self::Output {
			Self::Output { T: self.T * rhs.T }
		}
	}
	impl_units_scalar_ops!(Temperature, T);
	impl std::fmt::Display for Temperature {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.1}\u{00B0}C", self.as_C())
		}
	}
	impl Default for Temperature {
		fn default() -> Self {
			crate::phys::consts::ISA_SL_TEMP
		}
	}

	#[derive(Clone, Copy, Debug, PartialEq, PartialOrd, Serialize,
	    Deserialize)]
	pub struct TemperatureRelative {
		dT: f64,	/* Kelvin */
	}
	impl TemperatureRelative {
		pub fn from_K(kelvin: f64) -> Self {
			assert!(kelvin.is_finite());
			Self { dT: kelvin }
		}
		pub fn from_C(celsius: f64) -> Self {
			Self { dT: c2kelvin(celsius) }
		}
		pub fn from_F(fahrenheit: f64) -> Self {
			Self { dT: fah2kelvin(fahrenheit) }
		}
		pub fn as_K(&self) -> f64 {
			self.dT
		}
		pub fn as_C(&self) -> f64 {
			kelvin2c(self.dT)
		}
		pub fn as_F(&self) -> f64 {
			kelvin2fah(self.dT)
		}
		pub const fn new_const(kelvin: f64) -> Self {
			Self { dT: kelvin }
		}
	}
	impl_units_ops!(TemperatureRelative, dT);
	impl_units_scalar_ops!(TemperatureRelative, dT);
	impl std::fmt::Display for TemperatureRelative {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.1}\u{00B0}dC", self.as_C())
		}
	}

	#[derive(Clone, Copy, Debug, PartialEq, PartialOrd, Serialize,
	    Deserialize)]
	pub struct Pressure {
		p: f64,		/* Pascals, CANNOT be negative */
	}
	impl Pressure {
		pub fn from_pa(pa: f64) -> Self {
			assert!(pa.is_finite());
			assert!(pa >= 0.0);
			assert!(pa < 1e12);
			Self { p: pa }
		}
		pub fn from_hpa(hpa: f64) -> Self {
			Self::from_pa(hpa / 100.0)
		}
		pub fn from_psi(psi: f64) -> Self {
			Self::from_pa(psi2pa(psi))
		}
		pub fn as_pa(&self) -> f64 {
			self.p
		}
		pub fn as_hpa(&self) -> f64 {
			self.p / 100.0
		}
		pub fn as_psi(&self) -> f64 {
			pa2psi(self.p)
		}
		pub const fn new_const(pa: f64) -> Self {
			Self { p: pa }
		}
	}
	impl std::fmt::Display for Pressure {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.2} PSI", self.as_psi())
		}
	}
	impl Default for Pressure {
		fn default() -> Self {
			crate::phys::consts::ISA_SL_PRESS
		}
	}
	impl std::ops::Add for Pressure {
		type Output = Pressure;
		fn add(self, rhs: Pressure) -> Self::Output {
			Self::Output { p: self.p + rhs.p }
		}
	}
	impl std::ops::Sub for Pressure {
		type Output = PressureRelative;
		fn sub(self, rhs: Pressure) -> Self::Output {
			Self::Output { dP: self.p - rhs.p }
		}
	}
	impl std::ops::Mul for Pressure {
		type Output = Pressure;
		fn mul(self, rhs: Pressure) -> Self::Output {
			Self::Output { p: self.p * rhs.p }
		}
	}
	impl_units_scalar_ops!(Pressure, p);

	#[derive(Clone, Copy, Debug, PartialEq, PartialOrd, Serialize,
	    Deserialize)]
	pub struct PressureRelative {
		dP: f64,	/* Pascals, CAN be negative */
	}
	impl PressureRelative {
		pub fn from_pressure(p: Pressure) -> Self {
			Self { dP: p.as_pa() }
		}
		pub fn from_pa(pa: f64) -> Self {
			assert!(pa.is_finite());
			assert!(pa.abs() < 1e12);
			Self { dP: pa }
		}
		pub fn from_hpa(hpa: f64) -> Self {
			Self::from_pa(hpa / 100.0)
		}
		pub fn from_psi(psi: f64) -> Self {
			Self::from_pa(psi2pa(psi))
		}
		pub fn as_pa(&self) -> f64 {
			self.dP
		}
		pub fn as_hpa(&self) -> f64 {
			self.dP / 100.0
		}
		pub fn as_psi(&self) -> f64 {
			pa2psi(self.dP)
		}
		pub const fn new_const(pa: f64) -> Self {
			Self { dP: pa }
		}
	}
	impl_units_ops!(PressureRelative, dP);
	impl_units_scalar_ops!(PressureRelative, dP);
	impl std::fmt::Display for PressureRelative {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.2} PSID", self.as_psi())
		}
	}

	#[derive(Clone, Copy, Debug, Default, PartialEq, PartialOrd,
	    Serialize, Deserialize)]
	pub struct Distance {
		d: f64,		/* meters */
	}
	impl Distance {
		pub fn from_m(m: f64) -> Self {
			assert!(m.is_finite());
			assert!(m >= 0.0);
			Self { d: m }
		}
		pub fn from_km(km: f64) -> Self {
			Self { d: km * 1000.0 }
		}
		pub fn from_ft(ft: f64) -> Self {
			Self { d: feet2met(ft) }
		}
		pub fn from_yrd(yrd: f64) -> Self {
			Self { d: feet2met(yrd * 3.0) }
		}
		pub fn from_nm(nm: f64) -> Self {
			Self { d: nm2met(nm) }
		}
		pub fn from_sm(sm: f64) -> Self {
			Self { d: sm2met(sm) }
		}
		pub fn as_m(&self) -> f64 {
			self.d
		}
		pub fn as_km(&self) -> f64 {
			self.d / 1000.0
		}
		pub fn as_ft(&self) -> f64 {
			met2feet(self.d)
		}
		pub fn as_yrd(&self) -> f64 {
			met2feet(self.d) / 3.0
		}
		pub fn as_nm(&self) -> f64 {
			met2nm(self.d)
		}
		pub fn as_sm(&self) -> f64 {
			met2sm(self.d)
		}
		pub const fn new_const(m: f64) -> Self {
			Self { d: m }
		}
	}
	impl std::fmt::Display for Distance {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.2} nm", self.as_nm())
		}
	}
	impl_units_ops_non_neg!(Distance, d);
	impl_units_scalar_ops!(Distance, d);

	#[derive(Clone, Copy, Debug, Default, PartialEq, PartialOrd,
	    Serialize, Deserialize)]
	pub struct Mass {
		m: f64,		/* kg */
	}
	impl Mass {
		pub fn from_kg(kg: f64) -> Self {
			assert!(kg >= 0.0 && kg < 1e12);
			Self { m: kg }
		}
		pub fn from_mt(mt: f64) -> Self {
			assert!(mt >= 0.0 && mt < 1e9);
			Self { m: mt * 1000.0 }
		}
		pub fn from_lbs(lbs: f64) -> Self {
			assert!(lbs >= 0.0 && lbs < 1e12);
			Self { m: lbs2kg(lbs) }
		}
		pub fn as_kg(&self) -> f64 {
			self.m
		}
		pub fn as_mt(&self) -> f64 {
			self.m / 1000.0
		}
		pub fn as_lbs(&self) -> f64 {
			kg2lbs(self.m)
		}
	}
	impl std::fmt::Display for Mass {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.1} kg", self.as_kg())
		}
	}
	impl_units_ops_non_neg!(Mass, m);
	impl_units_scalar_ops!(Mass, m);

	#[derive(Clone, Copy, Debug, Default, PartialEq, PartialOrd,
	    Serialize, Deserialize)]
	pub struct Angvel {
		r: f64,		/* rad/sec */
	}
	impl Angvel {
		pub fn from_radsec(radsec: f64) -> Self {
			assert!(radsec.is_finite());
			assert!(radsec.abs() < 1e12);
			Self { r: radsec }
		}
		pub fn from_degsec(degsec: f64) -> Self {
			Self { r: crate::geom::conv::deg2rad(degsec) }
		}
		pub fn from_rps(rps: f64) -> Self {
			assert!(rps.is_finite());
			assert!(rps.abs() < 1e12);
			Self { r: rps * 2.0 * std::f64::consts::PI }
		}
		pub fn from_rpm(rpm: f64) -> Self {
			Self { r: rpm2radsec(rpm) }
		}
		pub fn as_radsec(&self) -> f64 {
			self.r
		}
		pub fn as_degsec(&self) -> f64 {
			crate::geom::conv::rad2deg(self.r)
		}
		pub fn as_rps(&self) -> f64 {
			self.r / (2.0 * std::f64::consts::PI)
		}
		pub fn as_rpm(&self) -> f64 {
			radsec2rpm(self.r)
		}
		pub const fn new_const(radsec: f64) -> Self {
			Self { r: radsec }
		}
	}
	impl std::fmt::Display for Angvel {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.1} rpm", self.as_rpm())
		}
	}
	impl_units_ops!(Angvel, r);
	impl_units_scalar_ops!(Angvel, r);

	#[derive(Clone, Copy, Debug, Default, PartialEq, PartialOrd,
	    Serialize, Deserialize)]
	pub struct Speed {
		s: f64,		/* m/s */
	}
	impl Speed {
		pub fn from_mps(mps: f64) -> Self {
			assert!(mps.is_finite());
			assert!(mps.abs() < 1e6);
			Self { s: mps }
		}
		pub fn from_kph(kph: f64) -> Self {
			Self { s: kph / 3.6 }
		}
		pub fn from_mph(kph: f64) -> Self {
			Self { s: mph2mps(kph) }
		}
		pub fn from_kt(kt: f64) -> Self {
			Self { s: kt2mps(kt) }
		}
		pub fn from_fpm(fpm: f64) -> Self {
			Self { s: fpm2mps(fpm) }
		}
		pub fn as_mps(&self) -> f64 {
			self.s
		}
		pub fn as_kph(&self) -> f64 {
			self.s * 3.6
		}
		pub fn as_mph(&self) -> f64 {
			mps2mph(self.s)
		}
		pub fn as_kt(&self) -> f64 {
			mps2kt(self.s)
		}
		pub fn as_fpm(&self) -> f64 {
			mps2fpm(self.s)
		}
		pub const fn new_const(mps: f64) -> Self {
			Self { s: mps }
		}
	}
	impl std::fmt::Display for Speed {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.1} KT", self.as_kt())
		}
	}
	impl_units_ops!(Speed, s);
	impl_units_scalar_ops!(Speed, s);

	#[derive(Clone, Copy, Debug, Default, PartialEq, PartialOrd,
	    Serialize, Deserialize)]
	pub struct MassRate {
		mr: f64,	/* kg/s */
	}
	impl MassRate {
		pub fn from_kgs(kgs: f64) -> Self {
			assert!(kgs.is_finite());
			assert!(kgs.abs() < 1e12);
			Self { mr: kgs }
		}
		pub fn from_kgh(kgh: f64) -> Self {
			assert!(kgh.is_finite());
			assert!(kgh.abs() < 1e9);
			Self { mr: kgh / 3600.0 }
		}
		pub fn from_pph(pph: f64) -> Self {
			assert!(pph.is_finite());
			assert!(pph.abs() < 1e9);
			Self { mr: lbs2kg(pph) / 3600.0 }
		}
		pub fn as_kgs(&self) -> f64 {
			self.mr
		}
		pub fn as_kgh(&self) -> f64 {
			self.mr * 3600.0
		}
		pub fn as_pph(&self) -> f64 {
			kg2lbs(self.mr * 3600.0)
		}
		pub const fn new_const(kgs: f64) -> Self {
			Self { mr: kgs }
		}
	}
	impl std::fmt::Display for MassRate {
		fn fmt(&self, f: &mut std::fmt::Formatter<'_>) ->
		    std::fmt::Result {
			write!(f, "{:.1} kg/s", self.mr)
		}
	}
	impl_units_ops!(MassRate, mr);
	impl_units_scalar_ops!(MassRate, mr);
}

pub mod util {
	use crate::phys::{units::*, consts::*};
	/*
	 * Returns the speed of sound in m/s in dry air.
	 */
	pub fn speed_sound_dry_air(T: Temperature) -> Speed
	{
		speed_sound(T, GAMMA_DRY_AIR, R_SPEC_DRY_AIR)
	}
	/*
	 * Returns the speed of sound in any specific gas.
	 * @param T Temperature of the gas.
	 * @param gamma Ratio of specific heats for the gas. For dry air,
	 *	this is `crate::phys::consts::GAMMA_DRY_AIR`.
	 * @param R Specific gas constant for the gas. For dry air,
	 *	this is `crate::phys::consts::R_SPEC_DRY_AIR`.
	 * @return Speed of sound in the specified gas.
	 */
	pub fn speed_sound(T: Temperature, gamma: f64, R: f64) -> Speed
	{
		Speed::from_mps((gamma * R * T.as_K()).sqrt())
	}
	pub fn baro_alt2press(alt_m: f64, p0: Pressure, T0: Temperature,
	    g_mss: f64) -> f64 {
		/*
		 * Standard barometric formula:
		 *                       g.M
		 *           /    L.h \^ ----
		 * p = p0 * ( 1 - ---  ) R0.L
		 *           \     T0 /
		 * Where:
		 * p    current outside pressure [Pa]
		 * p0   reference sea level pressure [Pa] (NOT QNH!)
		 * L    temperature lapse rate [K/m]
		 * h    height above mean sea level [m]
		 * T0   reference sea level temperature [K]
		 * g    gravitational acceleration [m/s^2]
		 * M    molar mass of dry air [kg/mol]
		 * R0   universal gas constant [J/(mol.K)]
		 */
		p0.as_pa() *
		    (1.0 - ((ISA_TLR_PER_1M * alt_m) / T0.as_K()))
		    .powf((g_mss * DRY_AIR_MOL) /
		    (R_UNIV * ISA_TLR_PER_1M))
	}
	pub fn press2baro_alt(p: Pressure, p0: Pressure, T0: Temperature,
	    g_mss: f64) -> f64 {
		/*
		 * This is the barometric formula, solved for 'h':
		 *                          R0.L
		 *           /      / p  \^ ---- \
		 *     T0 * (  1 - ( ---- ) g.M   )
		 *           \      \ p0 /       /
		 * h = ----------------------------
		 *                 L
		 * Where:
		 * h    height above mean sea level [m]
		 * p    current outside pressure [Pa]
		 * p0   reference sea level pressure [Pa] (NOT QNH!)
		 * R0   universal gas constant [J/(mol.K)]
		 * L    temperature lapse rate [K/m]
		 * g    gravitational acceleration [m/s^2]
		 * M    molar mass of dry air [kg/mol]
		 * T0   reference sea level temperature [K]
		 */
		(T0.as_K() *
		    (1.0 - (p.as_pa() / p0.as_pa())
		    .powf((R_UNIV * ISA_TLR_PER_1M) / (g_mss * DRY_AIR_MOL)))) /
		    ISA_TLR_PER_1M
	}
}

pub mod consts {
	use crate::phys::units::*;
	use std::time::Duration;
	/*
	 * ISA (International Standard Atmosphere) parameters.
	 */
	/* Sea level temperature */
	pub const ISA_SL_TEMP: Temperature = Temperature::new_const(288.15);
	 /* Sea level pressure */
	pub const ISA_SL_PRESS: Pressure = Pressure::new_const(101325.0);
	/* Sea level density in kg/m^3 */
	pub const ISA_SL_DENS: f64 = 1.225;
	/* Temperature lapse rate per 1000ft */
	pub const ISA_TLR_PER_1000FT: f64 = 1.98;
	/* Temperature lapse rate per 1 meter */
	pub const ISA_TLR_PER_1M: f64 = 0.0065;
	/* Speed of sound at sea level */
	pub const ISA_SPEED_SOUND: Speed = Speed::new_const(340.3);
	/* Tropopause altitude */
	pub const ISA_TP_ALT: Distance = Distance::new_const(36089.0 * 0.3048);
	/*
	 * Physical constants.
	 */
	/* Earth surface grav. acceleration in m/s/s */
	pub const EARTH_GRAVITY: f64 = 9.80665;
	/* Sidereal day on Earth in seconds */
	pub const EARTH_SID_DAY: Duration = Duration::new(86164, 905_000_000);
	pub const EARTH_ROT_RATE: f64 = 360.0 / 86164.0905;	/* deg/sec */
	pub const DRY_AIR_MOL: f64 = 0.02896968;/* Molar mass of dry air */
	pub const GAMMA_DRY_AIR: f64 = 1.4;	/* Spec heat ratio of dry air */
	pub const R_UNIV: f64 = 8.314462618;	/* Universal gas constant */
	pub const R_SPEC_DRY_AIR: f64 = 287.058;/* Spec gas const of dry air */
	/* Stefan-Boltzmann constant */
	pub const BOLTZMANN_CONST: f64 = 5.67e-8;
}

pub mod conv {
	/*
	 * Temperature conversions
	 */
	/* celsius -> fahrenheit */
	pub fn c2fah(celsius: f64) -> f64 {
		assert!(celsius.is_finite());
		(celsius * 1.8) + 32.0
	}
	/* fahrenheit -> celsius */
	pub fn fah2c(fah: f64) -> f64 {
		assert!(fah.is_finite());
		(fah - 32.0) / 1.8
	}
	/* kelvin -> celsius */
	pub fn kelvin2c(kelvin: f64) -> f64 {
		assert!(kelvin.is_finite());
		kelvin - 273.15
	}
	/* celsius -> kelvin */
	pub fn c2kelvin(celsius: f64) -> f64 {
		assert!(celsius.is_finite());
		celsius + 273.15
	}
	/* fahrenheit -> kelvin */
	pub fn fah2kelvin(fah: f64) -> f64 {
		assert!(fah.is_finite());
		(fah + 459.67) / 1.8
	}
	/* kelvin -> fahrenheit */
	pub fn kelvin2fah(kelvin: f64) -> f64 {
		assert!(kelvin.is_finite());
		assert!(kelvin > 0.0);
		(kelvin * 1.8) - 459.67
	}
	/*
	 * Distance conversions
	 */
	/* feet -> meters */
	pub fn feet2met(ft: f64) -> f64 {
		assert!(ft.is_finite());
		ft * 0.3048
	}
	/* meters -> feet */
	pub fn met2feet(m: f64) -> f64 {
		assert!(m.is_finite());
		m * 3.2808398950131
	}
	/* nautical miles -> meters */
	pub fn nm2met(nm: f64) -> f64 {
		assert!(nm.is_finite());
		assert!(nm >= 0.0);
		nm * 1852.0
	}
	/* meters -> nautical miles */
	pub fn met2nm(m: f64) -> f64 {
		assert!(m.is_finite());
		assert!(m >= 0.0);
		m / 1852.0
	}
	/* statute miles -> meters */
	pub fn sm2met(sm: f64) -> f64 {
		assert!(sm.is_finite());
		assert!(sm >= 0.0);
		(sm * 5280.0) * 0.3048
	}
	/* meters -> statute miles */
	pub fn met2sm(m: f64) -> f64 {
		assert!(m.is_finite());
		assert!(m >= 0.0);
		(m / 0.3048) / 5280.0
	}
	/*
	 * Pressure conversions
	 */
	/* Pascals -> Hectopascals */
	pub fn hpa2pa(hpa: f64) -> f64 {
		assert!(hpa.is_finite());
		hpa * 100.0
	}
	#[macro_export]
	macro_rules! hpa2pa {
	    ($hpa: expr) => { $hpa * 100.0 };
	}
	/* Hectopascals -> Pascals */
	pub fn pa2hpa(pa: f64) -> f64 {
		assert!(pa.is_finite());
		pa / 100.0
	}
	#[macro_export]
	macro_rules! pa2hpa {
	    ($pa: expr) => { $pa / 100.0 };
	}
	/* psi -> Pascals */
	pub fn psi2pa(psi: f64) -> f64 {
		assert!(psi.is_finite());
		psi * 6894.73326075122482308111
	}
	#[macro_export]
	macro_rules! psi2pa {
	    ($psi: expr) => { $psi * 6894.73326075122482308111 };
	}
	/* Pascals -> psi */
	pub fn pa2psi(pa: f64) -> f64 {
		assert!(pa.is_finite());
		pa / 6894.73326075122482308111
	}
	#[macro_export]
	macro_rules! pa2psi {
	    ($pa: expr) => { $pa / 6894.73326075122482308111 };
	}
	/* In.Hg -> pa */
	pub fn inhg2pa(inhg: f64) -> f64 {
		assert!(inhg.is_finite());
		inhg * (101325.0 / 29.92)
	}
	#[macro_export]
	macro_rules! inhg2pa {
	    ($inhg: expr) => { $inhg * (101325.0 / 29.92) };
	}
	/* pa -> In.Hg */
	pub fn pa2inhg(pa: f64) -> f64 {
		assert!(pa.is_finite());
		pa * (29.92 / 101325.0)
	}
	#[macro_export]
	macro_rules! pa2inhg {
	    ($pa: expr) => { $pa * (29.92 / 101325.0) };
	}
	/*
	 * Speed conversions
	 */
	/* m/s -> km/h */
	pub fn mps2kph(mps: f64) -> f64 {
		assert!(mps.is_finite());
		mps * 3.6
	}
	/* km/h -> m/s */
	pub fn kph2mps(kph: f64) -> f64 {
		assert!(kph.is_finite());
		kph / 3.6
	}
	/* m/s -> miles/h */
	pub fn mps2mph(mps: f64) -> f64 {
		assert!(mps.is_finite());
		mps / 0.44704
	}
	/* miles/h -> m/s */
	pub fn mph2mps(mps: f64) -> f64 {
		assert!(mps.is_finite());
		mps * 0.44704
	}
	/* ft/min -> m/s */
	pub fn fpm2mps(fpm: f64) -> f64 {
		assert!(fpm.is_finite());
		(fpm / 0.3048) * 60.0
	}
	/* m/s -> ft/min */
	pub fn mps2fpm(mps: f64) -> f64 {
		assert!(mps.is_finite());
		(mps / 0.3048) / 60.0
	}
	/* knots -> m/s */
	pub fn kt2mps(kt: f64) -> f64 {
		assert!(kt.is_finite());
		(kt * 1852.0) / 3600.0
	}
	/* m/s -> knots */
	pub fn mps2kt(mps: f64) -> f64 {
		assert!(mps.is_finite());
		(mps * 3600.0) / 1852.0
	}
	/*
	 * Mass conversions
	 */
	pub fn lbs2kg(lbs: f64) -> f64 {
		assert!(lbs.is_finite());
		assert!(lbs.abs() < 1e12);
		lbs * 0.45359237
	}
	pub fn kg2lbs(kg: f64) -> f64 {
		assert!(kg.is_finite());
		assert!(kg.abs() < 1e12);
		kg / 0.45359237
	}
	/*
	 * Power conversions
	 */
	/* Watts to horsepower */
	pub fn watt2hp(w: f64) -> f64 {
		assert!(w.is_finite());
		assert!(w.abs() < 1e12);
		w * 0.001341022
	}
	/* horsepower to Watts */
	pub fn hp2watt(hp: f64) -> f64 {
		assert!(hp.is_finite());
		assert!(hp.abs() < 1e12);
		hp / 0.001341022
	}
	/*
	 * Angle and angular velocity conversions
	 */
	pub fn radsec2rpm(radsec: f64) -> f64 {
		(radsec / (2.0 * std::f64::consts::PI)) * 60.0
	}
	pub fn rpm2radsec(rpm: f64) -> f64 {
		(rpm / 60.0) * 2.0 * std::f64::consts::PI
	}
}
