/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2015 Saso Kiselkov. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <acfutils/assert.h>
#include <acfutils/math.h>
#include <acfutils/log.h>
#include <acfutils/helpers.h>
#include <acfutils/perf.h>
#include <acfutils/safe_alloc.h>

#define	SECS_PER_HR	3600		/* Number of seconds in an hour */

#define	STEP_DEBUG

#define	ACFT_PERF_MIN_VERSION	1
#define	ACFT_PERF_MAX_VERSION	1
#define	MAX_LINE_COMPS		2

/*
 * Simulation step for accelclb2dist in seconds. 15 seems to be a good
 * compromise between performance and accuracy (~1% error vs running 1-second
 * steps, but 15x faster).
 */
#define	SECS_PER_STEP	15.0
#define	ALT_THRESH	1
#define	KCAS_THRESH	0.1
#define	MIN_ACCEL	0.0075	/* In m/s^2, equals approx 0.05 KT/s */

/*
 * Parses a set of bezier curve points from the input CSV file. Used to parse
 * curve points for performance curves.
 *
 * @param fp FILE pointer from which to read lines.
 * @param curvep Pointer that will be filled with the parsed curve.
 * @param numpoints Number of points to fill in the curve (and input lines).
 * @param line_num Current file line number. Used to pass to
 *	parse_get_next_line to track file parsing progress.
 *
 * @return B_TRUE on successful parse, B_FALSE on failure.
 */
static bool_t
parse_curves(FILE *fp, bezier_t **curvep, size_t numpoints, size_t *line_num)
{
	bezier_t	*curve;
	char		*line = NULL;
	size_t		line_cap = 0;
	ssize_t		line_len = 0;

	ASSERT(*curvep == NULL);
	curve = bezier_alloc(numpoints);

	for (size_t i = 0; i < numpoints; i++) {
		char *comps[2];

		line_len = parser_get_next_line(fp, &line, &line_cap, line_num);
		if (line_len <= 0)
			goto errout;
		if (explode_line(line, ',', comps, 2) != 2)
			goto errout;
		curve->pts[i] = VECT2(atof(comps[0]), atof(comps[1]));
		if (i > 0 && curve->pts[i - 1].x >= curve->pts[i].x)
			goto errout;
	}

	*curvep = curve;
	free(line);

	return (B_TRUE);
errout:
	free(line);
	bezier_free(curve);
	return (B_FALSE);
}

#define	PARSE_SCALAR(name, var) \
	if (strcmp(comps[0], name) == 0) { \
		if (ncomps != 2 || (var) != 0.0) { \
			logMsg("Error parsing acft perf file %s:%lu: " \
			    "malformed or duplicate " name " line.", \
			    filename, (unsigned long)line_num); \
			goto errout; \
		} \
		(var) = atof(comps[1]); \
		if ((var) <= 0.0) { \
			logMsg("Error parsing acft perf file %s:%lu: " \
			    "invalid value for " name, filename, \
			    (unsigned long)line_num); \
			goto errout; \
		} \
	}

/*
 * Checks that comps[0] contains `name' and if it does, parses comps[1] number
 * of bezier curve points into `var'.
 */
#define	PARSE_CURVE(name, var) \
	if (strcmp(comps[0], name) == 0) { \
		if (ncomps != 2 || atoi(comps[1]) < 2 || (var) != NULL) { \
			logMsg("Error parsing acft perf file %s:%lu: " \
			    "malformed or duplicate " name " line.", \
			    filename, (unsigned long)line_num); \
			goto errout; \
		} \
		if (!parse_curves(fp, &(var), atoi(comps[1]), &line_num)) { \
			logMsg("Error parsing acft perf file %s:%lu: " \
			    "malformed or missing lines.", filename, \
			    (unsigned long)line_num); \
			goto errout; \
		} \
	}

acft_perf_t *
acft_perf_parse(const char *filename)
{
	acft_perf_t	*acft = safe_calloc(sizeof (*acft), 1);
	FILE		*fp = fopen(filename, "r");
	char		*line = NULL;
	size_t		line_cap = 0, line_num = 0;
	ssize_t		line_len = 0;
	char		*comps[MAX_LINE_COMPS];
	bool_t		version_check_completed = B_FALSE;

	if (fp == NULL)
		goto errout;
	while ((line_len = parser_get_next_line(fp, &line, &line_cap,
	    &line_num)) != -1) {
		ssize_t ncomps;

		if (line_len == 0)
			continue;
		ncomps = explode_line(line, ',', comps, MAX_LINE_COMPS);
		if (ncomps < 0) {
			logMsg("Error parsing acft perf file %s:%lu: "
			    "malformed line, too many line components.",
			    filename, (unsigned long)line_num);
			goto errout;
		}
		ASSERT(ncomps > 0);
		if (strcmp(comps[0], "VERSION") == 0) {
			int vers;

			if (version_check_completed) {
				logMsg("Error parsing acft perf file %s:%lu: "
				    "duplicate VERSION line.", filename,
				    (unsigned long)line_num);
				goto errout;
			}
			if (ncomps != 2) {
				logMsg("Error parsing acft perf file %s:%lu: "
				    "malformed VERSION line.", filename,
				    (unsigned long)line_num);
				goto errout;
			}
			vers = atoi(comps[1]);
			if (vers < ACFT_PERF_MIN_VERSION ||
			    vers > ACFT_PERF_MAX_VERSION) {
				logMsg("Error parsing acft perf file %s:%lu: "
				    "unsupported file version %d.", filename,
				    (unsigned long)line_num, vers);
				goto errout;
			}
			version_check_completed = B_TRUE;
			continue;
		}
		if (!version_check_completed) {
			logMsg("Error parsing acft perf file %s:%lu: first "
			    "line was not VERSION.", filename,
			    (unsigned long)line_num);
			goto errout;
		}
		if (strcmp(comps[0], "ACFTTYPE") == 0) {
			if (ncomps != 2 || acft->acft_type != NULL) {
				logMsg("Error parsing acft perf file %s:%lu: "
				    "malformed or duplicate ACFTTYPE line.",
				    filename, (unsigned long)line_num);
				goto errout;
			}
			acft->acft_type = strdup(comps[1]);
		} else if (strcmp(comps[0], "ENGTYPE") == 0) {
			if (ncomps != 2 || acft->eng_type != NULL) {
				logMsg("Error parsing acft perf file %s:%lu: "
				    "malformed or duplicate ENGTYPE line.",
				    filename, (unsigned long)line_num);
				goto errout;
			}
			acft->eng_type = strdup(comps[1]);
		}
		else PARSE_SCALAR("MAXTHR", acft->eng_max_thr)
		else PARSE_SCALAR("MINTHR", acft->eng_min_thr)
		else PARSE_SCALAR("REFZFW", acft->ref.zfw)
		else PARSE_SCALAR("REFFUEL", acft->ref.fuel)
		else PARSE_SCALAR("REFCRZLVL", acft->ref.crz_lvl)
		else PARSE_SCALAR("REFCLBIAS", acft->ref.clb_ias)
		else PARSE_SCALAR("REFCLBMACH", acft->ref.clb_mach)
		else PARSE_SCALAR("REFCRZIAS", acft->ref.crz_ias)
		else PARSE_SCALAR("REFCRZMACH", acft->ref.crz_mach)
		else PARSE_SCALAR("REFDESIAS", acft->ref.des_ias)
		else PARSE_SCALAR("REFDESMACH", acft->ref.des_mach)
		else PARSE_SCALAR("REFTOFLAP", acft->ref.to_flap)
		else PARSE_SCALAR("REFACCELHT", acft->ref.accel_height)
		else PARSE_SCALAR("REFSPDLIM", acft->ref.spd_lim)
		else PARSE_SCALAR("REFSPDLIMALT", acft->ref.spd_lim_alt)
		else PARSE_SCALAR("MAXFUEL", acft->max_fuel)
		else PARSE_SCALAR("MAXGW", acft->max_gw)
		else PARSE_SCALAR("WINGAREA", acft->wing_area)
		else PARSE_SCALAR("CLMAX", acft->cl_max_aoa)
		else PARSE_SCALAR("CLFLAPMAX", acft->cl_flap_max_aoa)
		else PARSE_CURVE("THRDENS", acft->thr_dens_curve)
		else PARSE_CURVE("THRISA", acft->thr_isa_curve)
		else PARSE_CURVE("SFCTHR", acft->sfc_thr_curve)
		else PARSE_CURVE("SFCDENS", acft->sfc_dens_curve)
		else PARSE_CURVE("SFCISA", acft->sfc_isa_curve)
		else PARSE_CURVE("CL", acft->cl_curve)
		else PARSE_CURVE("CLFLAP", acft->cl_flap_curve)
		else PARSE_CURVE("CD", acft->cd_curve)
		else PARSE_CURVE("CDFLAP", acft->cd_flap_curve)
		else {
			logMsg("Error parsing acft perf file %s:%lu: unknown "
			    "line", filename, (unsigned long)line_num);
			goto errout;
		}
	}

	if (acft->acft_type == NULL || acft->ref.zfw <= 0 ||
	    acft->ref.fuel <= 0 || acft->ref.crz_lvl <= 0 ||
	    acft->ref.clb_ias <= 0 || acft->ref.clb_mach <= 0 ||
	    acft->ref.crz_ias <= 0 || acft->ref.crz_mach <= 0 ||
	    acft->ref.des_ias <= 0 || acft->ref.des_mach <= 0 ||
	    acft->ref.to_flap <= 0 || acft->ref.accel_height <= 0 ||
	    acft->ref.spd_lim <= 0 || acft->ref.spd_lim_alt <= 0 ||
	    acft->max_fuel <= 0 || acft->max_gw <= 0 ||
	    acft->eng_type == NULL || acft->eng_max_thr <= 0 ||
	    acft->eng_min_thr <= 0 || acft->thr_dens_curve == NULL ||
	    acft->thr_isa_curve == NULL || acft->sfc_thr_curve == NULL ||
	    acft->sfc_dens_curve == NULL || acft->sfc_isa_curve == NULL ||
	    acft->cl_curve == NULL || acft->cl_flap_curve == NULL ||
	    acft->cd_curve == NULL || acft->cd_flap_curve == NULL ||
	    acft->wing_area == 0) {
		logMsg("Error parsing acft perf file %s: missing or corrupt "
		    "data fields.", filename);
		goto errout;
	}

	fclose(fp);
	free(line);

	acft->ref.thr_derate = 1;

	return (acft);
errout:
	if (fp)
		fclose(fp);
	if (acft)
		acft_perf_destroy(acft);
	free(line);
	return (NULL);
}

void
acft_perf_destroy(acft_perf_t *acft)
{
	if (acft->acft_type)
		free(acft->acft_type);
	if (acft->eng_type)
		free(acft->eng_type);
	if (acft->thr_dens_curve)
		bezier_free(acft->thr_dens_curve);
	if (acft->thr_isa_curve)
		bezier_free(acft->thr_isa_curve);
	if (acft->sfc_thr_curve)
		bezier_free(acft->sfc_thr_curve);
	if (acft->sfc_dens_curve)
		bezier_free(acft->sfc_dens_curve);
	if (acft->sfc_isa_curve)
		bezier_free(acft->sfc_isa_curve);
	if (acft->cl_curve)
		bezier_free(acft->cl_curve);
	if (acft->cl_flap_curve)
		bezier_free(acft->cl_flap_curve);
	if (acft->cd_curve)
		bezier_free(acft->cd_curve);
	if (acft->cd_flap_curve)
		bezier_free(acft->cd_flap_curve);
	free(acft);
}

flt_perf_t *
flt_perf_new(const acft_perf_t *acft)
{
	flt_perf_t *flt = safe_calloc(sizeof (*flt), 1);
	memcpy(flt, &acft->ref, sizeof (*flt));
	return (flt);
}

void
flt_perf_destroy(flt_perf_t *flt)
{
	free(flt);
}

/*
 * Estimates maximum available engine thrust in a given flight situation.
 * This takes into account atmospheric conditions as well as any currently
 * effective engine derates.
 *
 * @param flt Flight performance configuration.
 * @param acft Aircraft performance tables.
 * @param alt Altitude in feet.
 * @param ktas True air speed in knots.
 * @param qnh Barometric altimeter setting in hPa.
 * @param isadev ISA temperature deviation in degrees C.
 * @param tp_alt Altitude of the tropopause in feet.
 *
 * @return Maximum available engine thrust in kN.
 */
double
eng_max_thr(const flt_perf_t *flt, const acft_perf_t *acft, double alt,
    double ktas, double qnh, double isadev, double tp_alt)
{
	double Ps, Pd, D, dmod, tmod;

	Ps = alt2press(alt, qnh);
	Pd = dyn_press(ktas, Ps, isadev2sat(alt2fl(alt < tp_alt ? alt : tp_alt,
	    qnh), isadev));
	D = air_density(Ps + Pd, isadev);
	dmod = quad_bezier_func(D, acft->thr_dens_curve);
	tmod = quad_bezier_func(isadev, acft->thr_isa_curve);

	return (acft->eng_max_thr * dmod * tmod * flt->thr_derate);
}

/*
 * Returns the maximum average thrust that the engines can attain between
 * two altitudes during a climb.
 *
 * @param flt Flight performance limits.
 * @param acft Aircraft performance limit curves.
 * @param alt1 First (lower) altitude in feet.
 * @param temp1 Estimated TAT in C at altitude alt1.
 * @param alt1 Second (higher) altitude in feet.
 * @param temp1 Estimated TAT in C at altitude alt2.
 * @param tp_alt Altitude of the tropopause in feet.
 *
 * @return The maximum average engine thrust (in Kilonewtons) attainable
 *	between alt1 and alt2 while keeping the flight and aircraft limits.
 */
double
eng_max_thr_avg(const flt_perf_t *flt, const acft_perf_t *acft, double alt1,
    double alt2, double ktas, double qnh, double isadev, double tp_alt)
{
	double Ps, Pd, D, avg_temp, thr;
	double avg_alt = AVG(alt1, alt2);
	/* convert altitudes to flight levels to calculate avg temp */
	double alt1_fl = alt2fl(alt1, qnh);
	double alt2_fl = alt2fl(alt2, qnh);
	double tp_fl = alt2fl(tp_alt, qnh);

	/*
	 * FIXME: correctly weight the temp average when tp_alt < alt2.
	 */
	avg_temp = AVG(isadev2sat(alt1_fl, isadev),
	    isadev2sat(alt2_fl < tp_fl ? alt2_fl : tp_fl, isadev));
	/*
	 * Ps is the average static air pressure between alt1 and alt2. Next
	 * calculate dynamic pressure rise to get total effective air pressure.
	 */
	Ps = alt2press(avg_alt, qnh);
	Pd = dyn_press(ktas, Ps, avg_temp);
	/*
	 * Finally grab effective air density.
	 */
	isadev = isadev2sat(alt2fl(avg_alt, qnh), avg_temp);
	D = air_density(Ps + Pd, isadev);
	/*
	 * Derive engine performance.
	 */
	thr = quad_bezier_func(D, acft->thr_dens_curve) *
	    quad_bezier_func(isadev, acft->thr_isa_curve) *
	    flt->thr_derate;

	return (thr);
}

/*
 * Given a curve mapping angle-of-attack (AoA) to an aircraft's coefficient of
 * lift (Cl) and a target Cl, we attempt to find the lowest AoA on the curve
 * where the required Cl is produced. If no candidate can be found, we return
 * DEFAULT_AOA.
 *
 * @param Cl Required coefficient of lift.
 * @param curve Bezier curve mapping AoA to Cl.
 *
 * @return The angle of attack (in degrees) at which the Cl is produced.
 */
static double
cl_curve_get_aoa(double Cl, const bezier_t *curve)
{
	double aoa = 5, *candidates;
	size_t n;

	candidates = quad_bezier_func_inv(Cl, curve, &n);
	if (n == 0 || n == SIZE_MAX)
		return (NAN);

	aoa = candidates[0];
	for (size_t i = 1; i < n; i++) {
		if (aoa > candidates[i])
			aoa = candidates[i];
	}
	free(candidates);

	return (aoa);
}

/*
 * Calculates total (kinetic + potential) energy of a moving object.
 * This simply computes: E = m.g.h + (1/2).m.v^2
 *
 * @param mass Mass in kg.
 * @param altm Altitude above sea level in meters.
 * @param tas True airspeed in m/s.
 *
 * @return The object's total energy in Joules.
 */
static inline double
calc_total_E(double mass, double altm, double tas)
{
	return (mass * EARTH_GRAVITY * altm + 0.5 * mass * POW2(tas));
}

/*
 * Calculates the altitude above sea level an object needs to be at to have
 * a given total (kinetic + potential) energy. This simply computes:
 * h = (E - (1/2).m.v^2) / (m.g)
 *
 * @param E Total energy in Joules.
 * @param m Mass in kg.
 * @param tas True airspeed in m/s.
 *
 * @return The object's required elevation above sea level in meters.
 */
static inline double
total_E_to_alt(double E, double m, double tas)
{
	return ((E - (0.5 * m * POW2(tas))) / (m * EARTH_GRAVITY));
}

/*
 * Calculates the angle of attack required to maintain level flight.
 *
 * @param Pd Dynamic pressure on the aircraft in Pa (see dyn_press()).
 * @param mass Aircrat mass in kg.
 * @param flap_ratio Active flat setting between 0 and 1 inclusive
 *	(0 - flaps up, 1 - flaps fully deployed).
 * @param acft Performance tables of the aircraft.
 *
 * @return Angle of attack to airstream in degrees required to produce
 *	lift equivalent to the weight of `mass' on Earth. If the aircraft
 *	is unable to produce sufficient lift at any angle of attack to
 *	sustain flight, returns NAN instead.
 */
static double
get_aoa(double Pd, double mass, double flap_ratio, const acft_perf_t *acft)
{
	double lift, Cl;

	lift = MASS2GFORCE(mass);
	Cl = lift / (Pd * acft->wing_area);
	if (flap_ratio == 0)
		return (cl_curve_get_aoa(Cl, acft->cl_curve));
	else
		return (wavg(cl_curve_get_aoa(Cl, acft->cl_curve),
		    cl_curve_get_aoa(Cl, acft->cl_flap_curve),
		    flap_ratio));
}

/*
 * Calculates the amount of drag experienced by an aircraft.
 *
 * @param Pd Dynamic pressure on the airframe in Pa (see dyn_press()).
 * @param aoa Current angle of attack to the airstream in degrees.
 * @param flap_ratio Active flat setting between 0 and 1 inclusive
 *	(0 - flaps up, 1 - flaps fully deployed).
 * @param acft Performance tables of the aircraft.
 *
 * @return Drag force on the aircraft's airframe in N.
 */
static inline double
get_drag(double Pd, double aoa, double flap_ratio, const acft_perf_t *acft)
{
	if (flap_ratio == 0)
		return (quad_bezier_func(aoa, acft->cd_curve) *
		    Pd * acft->wing_area);
	else
		return (wavg(quad_bezier_func(aoa, acft->cd_curve),
		    quad_bezier_func(aoa, acft->cd_flap_curve),
		    flap_ratio) * Pd * acft->wing_area);
}

/*
 * Performs a level acceleration simulation step.
 *
 * @param isadev ISA temperature deviation in degrees C.
 * @param tp_alt Altitude of the tropopause.
 * @param qnh Barometric altimeter setting in Pa.
 * @param gnd Flag indicating whether the aircraft is currently positioned
 *	on the ground (and hence no lift generation is required).
 * @param alt Current aircraft altitude above mean sea level in feet.
 * @param kcasp Input/output argument containing the aircraft's current
 *	calibrated airspeed in knots. This will be updated with the new
 *	airspeed after the function returns with success.
 * @param kcas_targp Input/output argument containing the acceleration
 *	target calibrated airspeed in knots. If the airspeed exceeds
 *	mach_lim, it will be down-adjusted, otherwise it remains unchanged.
 * @param mach_lim Limiting Mach number. If either the current airspeed
 *	or target airspeeds exceed this value at the provided altitude,
 *	we down-adjust both. This allows for continuously accelerating while
 *	climbing until reaching the Mach limit and then slowly bleeding off
 *	speed to maintain the Mach number.
 * @param wind_mps Wind component along flight path in m/s.
 * @param mass Aircraft total mass in kg.
 * @param flap_ratio Active flat setting between 0 and 1 inclusive
 *	(0 - flaps up, 1 - flaps fully deployed).
 * @param acft Performance tables of the aircraft.
 * @param flt Flight performance settings.
 * @param distp Return parameter that will be incremented with the distance
 *	covered during the acceleration phase. As such, it must be
 *	pre-initialized.
 * @param timep Input/output argument that initially contains the number of
 *	seconds that the acceleration step can be allowed to happen. This is
 *	then adjusted with the actual amount of time that the acceleration
 *	was happening. If the target airspeed is greater than what can be
 *	achieved during the acceleration step, timep is left unmodified
 *	(all of the available time was used for acceleration). If the
 *	target speed is achievable, timep is modified to the proportion of
 *	the time taken to accelerate to that speed. If the target speed is
 *	less than the current speed, timep is set proportionally negative
 *	(and kcasp adjusted to it), indicating to the caller that the energy
 *	can be used in a climb.
 * @param burnp Return parameter that will be filled with the amount of
 *	fuel consumed during the acceleration phase. If no acceleration was
 *	performed or speed was even given up for reuse for climbing, no
 *	adjustment to this value will be made.
 *
 * @return B_TRUE on success, B_FALSE on failure (insufficient thrust
 *	to overcome drag and accelerate).
 */
static bool_t
spd_chg_step(bool_t accel, double isadev, double tp_alt, double qnh, bool_t gnd,
    double alt, double *kcasp, double kcas_targ, double wind_mps, double mass,
    double flap_ratio, const acft_perf_t *acft, const flt_perf_t *flt,
    double *distp, double *timep, double *burnp)
{
	double aoa, drag, delta_v, E_now, E_lim, E_targ, tas_lim;
	double fl = alt2fl(alt, qnh);
	double Ps = alt2press(alt, qnh);
	double oat = isadev2sat(fl, isadev);
	double ktas_now = kcas2ktas(*kcasp, Ps, oat);
	double tas_now = KT2MPS(ktas_now);
	double tas_targ = KT2MPS(kcas2ktas(kcas_targ, Ps, oat));
	double Pd = dyn_press(ktas_now, Ps, oat);
	double D = air_density(Ps + Pd, oat);
	double thr = accel ? eng_max_thr(flt, acft, alt, ktas_now, qnh,
	    isadev, tp_alt) : acft->eng_min_thr;
	double burn = *burnp;
	double t = *timep;
	double altm = FEET2MET(alt);

	if (gnd) {
		aoa = 0;
	} else {
		aoa = get_aoa(Pd, mass, flap_ratio, acft);
		if (isnan(aoa))
			return (B_FALSE);
	}
	drag = get_drag(Pd, aoa, flap_ratio, acft);
	delta_v = (thr - drag) / mass;
	if (accel && delta_v < MIN_ACCEL)
		return (B_FALSE);

	tas_lim = tas_now + delta_v * t;
	E_now = calc_total_E(mass, altm, tas_now);
	E_lim = calc_total_E(mass, altm, tas_lim);
	E_targ = calc_total_E(mass, altm, tas_targ);

	if (accel ? E_targ > E_lim : E_targ < E_lim) {
		*kcasp = ktas2kcas(MPS2KT(tas_lim), Ps, oat);
	} else {
		t *= ((E_targ - E_now) / (E_lim - E_now));
		*kcasp = ktas2kcas(MPS2KT(tas_targ), Ps, oat);
		*timep = t;
	}

	if (t > 0)
		burn += acft_get_sfc(acft, thr, D, isadev) * (t / SECS_PER_HR);

	*burnp = burn;
	(*distp) += MET2NM(tas_now * t + 0.5 * delta_v * POW2(t) +
	    wind_mps * t);

	return (B_TRUE);
}

/*
 * Performs a climb simulation step.
 *
 * @param isadev Same as `isadev' in spd_chg_step.
 * @param tp_alt Same as `tp_alt' in spd_chg_step.
 * @param qnh Same as `qnh' in spd_chg_step.
 * @param altp Input/output argument containing the aircraft's current
 *	altitude in feet. It will be adjusted with the altitude achieved
 *	during the climb.
 * @param kcasp Input/output argument containing the aircraft's current
 *	calibrated airspeed in knots. After climb, we recalculate the
 *	effective calibrated airspeed at the new altitude and update kcasp.
 * @param alt_targ Climb target altitude in feet.
 * @param wind_mps Same as `wind_mps' in spd_chg_step.
 * @param mass Same as `mass' in spd_chg_step.
 * @param flap_ratio Same as `flap_ratio' in spd_chg_step.
 * @param acft Same as `acft' in spd_chg_step.
 * @param flt Same as `flt' in spd_chg_step.
 * @param distp Same as `distp' in spd_chg_step.
 * @param timep Same as `timep' in spd_chg_step.
 * @param burnp Same as `burnp' in spd_chg_step.
 *
 * @return B_TRUE on success, B_FALSE on failure (insufficient speed to
 *	sustain flight or thrust to maintain speed).
 */
static bool_t
alt_chg_step(bool_t clb, double isadev, double tp_alt, double qnh,
    double *altp, double *kcasp, double alt_targ, double wind_mps, double mass,
    double flap_ratio, const acft_perf_t *acft, const flt_perf_t *flt,
    double *distp, double *timep, double *burnp)
{
	double aoa, drag, E_now, E_lim, E_targ, D2;
	double alt = *altp;
	double fl = alt2fl(alt, qnh);
	double Ps = alt2press(alt, qnh);
	double oat = isadev2sat(fl, isadev);
	double ktas_now = kcas2ktas(*kcasp, Ps, oat);
	double tas_now = KT2MPS(ktas_now);
	double Pd = dyn_press(ktas_now, Ps, oat);
	double D = air_density(Ps + Pd, oat);
	double thr = clb ? eng_max_thr(flt, acft, alt, ktas_now, qnh, isadev,
	    tp_alt) : acft->eng_min_thr;
	double burn = *burnp;
	double t = *timep;
	double altm = FEET2MET(alt);

	aoa = get_aoa(Pd, mass, flap_ratio, acft);
	if (isnan(aoa))
		return (B_FALSE);
	drag = get_drag(Pd, aoa, flap_ratio, acft);
	if ((clb && thr < drag) || (!clb && thr > drag))
		return (B_FALSE);

	E_now = calc_total_E(mass, altm, tas_now);
	E_lim = E_now + (thr - drag) * tas_now * t;
	E_targ = calc_total_E(mass, FEET2MET(alt_targ), tas_now);

	if (clb ? E_targ > E_lim : E_targ < E_lim) {
		*altp = MET2FEET(total_E_to_alt(E_lim, mass, tas_now));
	} else {
		t *= ((E_targ - E_now) / (E_lim - E_now));
		*altp = alt_targ;
		*timep = t;
	}

	/* adjust kcas to new altitude */
	Ps = alt2press(*altp, qnh);
	fl = alt2fl(*altp, qnh);
	oat = isadev2sat(fl, isadev);
	D2 = air_density(Ps + dyn_press(ktas_now, Ps, oat), oat);
	*kcasp = ktas2kcas(ktas_now, Ps, oat);

	/* use average air density to use in burn estimation */
	burn += acft_get_sfc(acft, thr, AVG(D, D2), isadev) * (t / SECS_PER_HR);

	*burnp = burn;
	(*distp) += MET2NM(sqrt(POW2(tas_now * t) +
	    FEET2MET(POW2((*altp) - alt))) + wind_mps * t);

	return (B_TRUE);
}

/*
 * Calculates the linear distance covered by an aircraft in wings-level
 * flight attempting to climb and accelerate. This is used in climb distance
 * performance estimates, especially when constructing altitude-terminated
 * procedure legs. This function assumes the engines will be running at
 * maximum thrust during the climb/acceleration phase (subject to
 * environmental limitations and configured performance derates).
 *
 * @param flt Flight performance settings.
 * @param acft Aircraft performance tables.
 * @param isadev Estimated average ISA deviation during climb phase.
 * @param qnh Estimated average QNH during climb phase.
 * @param tp_alt Altitude of the tropopause in feet AMSL.
 * @param fuel Current fuel state in kg.
 * @param dir Unit vector pointing in the flight direction of the aircraft.
 * @param alt1 Climb/acceleration starting altitude in feet AMSL.
 * @param kcas1 Climb/acceleration starting calibrated airspeed in knots.
 * @param wind1 Wind vector at start of climb/acceleration phase. The
 *	vector's direction expresses the wind direction and its magnitude
 *	the wind's true speed in knots.
 * @param alt2 Climb/acceleration target altitude AMSL in feet. Must be
 *	greater than or equal to alt1.
 * @param kcas2 Climb/acceleration target calibrated airspeed in knots.
 * @param wind2 Wind vector at end of climb/acceleration phase. The wind is
 *	assumed to vary smoothly between alt1/wind1 and alt2/wind2.
 * @param flap_ratio Average flap setting between 0 and 1 (inclusive) during
 *	climb/acceleration phase. This expresses how muct lift and drag is
 *	produced by the wings as a fraction between CL/CLFLAP and CD/CDFLAP.
 *	A setting of 0 means flaps up, whereas 1 is flaps fully extended.
 * @param type Type of acceleration/climb procedure to execute.
 * @param burnp Return parameter which if not NULL will be filled with the
 *	amount of fuel consumed during the acceleration/climb phase (in kg).
 *
 * @return Distance over the ground covered during acceleration/climb
 *	maneuver in NM.
 */
double
accelclb2dist(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double tp_alt, double fuel, vect2_t dir,
    double alt1, double kcas1, vect2_t wind1,
    double alt2, double kcas2, vect2_t wind2,
    double flap_ratio, double mach_lim, accelclb_t type, double *burnp)
{
	double alt = alt1, kcas = kcas1, burn = 0, dist = 0;

	ASSERT(alt1 <= alt2);
	ASSERT(fuel >= 0);
	dir = vect2_unit(dir, NULL);

	/* Iterate in steps of SECS_PER_STEP. */
	while (alt2 - alt > ALT_THRESH || kcas2 - kcas > KCAS_THRESH) {
		double wind_mps, alt_fract, accel_t, clb_t, ktas_lim_mach,
		    kcas_lim_mach, oat, kcas_lim;
		double Ps;
		vect2_t wind;

		oat = isadev2sat(alt2fl(alt, qnh), isadev);
		Ps = alt2press(alt, qnh);
		ktas_lim_mach = mach2ktas(mach_lim, oat);
		kcas_lim_mach = ktas2kcas(ktas_lim_mach, Ps, oat);

		kcas_lim = kcas2;
		if (alt < flt->spd_lim_alt && kcas_lim > flt->spd_lim)
			kcas_lim = flt->spd_lim;
		if (kcas_lim > kcas_lim_mach)
			kcas_lim = kcas_lim_mach;
		if (alt2 - alt < ALT_THRESH && kcas_lim < kcas2)
			kcas2 = kcas_lim;

		/*
		 * Calculate the directional wind component. This will be
		 * factored into the distance traveled estimation below.
		 */
		alt_fract = (alt - alt1) / (alt2 - alt1);
		wind = VECT2(wavg(wind1.x, wind2.x, alt_fract),
		    wavg(wind1.y, wind2.y, alt_fract));
		wind_mps = KT2MPS(vect2_dotprod(wind, dir));

#ifdef	STEP_DEBUG
		double old_alt = alt;
		double old_kcas = kcas;
#endif	/* STEP_DEBUG */

		/*
		 * ACCEL_THEN_CLB and ACCEL_TAKEOFF first accelerate to kcas2
		 * and then climb. ACCEL_AND_CLB does a 50/50 time split.
		 */
		if (type == ACCEL_THEN_CLB || type == ACCEL_TAKEOFF)
			accel_t = SECS_PER_STEP;
		else
			accel_t = SECS_PER_STEP / 2;

		if (!spd_chg_step(B_TRUE, isadev, tp_alt, qnh,
		    type == ACCEL_TAKEOFF && alt == alt1, alt, &kcas, kcas_lim,
		    wind_mps, flt->zfw + fuel - burn, flap_ratio, acft, flt,
		    &dist, &accel_t, &burn)) {
			return (NAN);
		}

		clb_t = SECS_PER_STEP - accel_t;
		if (clb_t > 0 && alt2 - alt > ALT_THRESH &&
		    !alt_chg_step(B_TRUE, isadev, tp_alt, qnh, &alt, &kcas,
		    alt2, wind_mps, flt->zfw + fuel - burn, flap_ratio, acft,
		    flt, &dist, &clb_t, &burn)) {
			return (NAN);
		}

#ifdef	STEP_DEBUG
		double total_t;

		total_t = accel_t + clb_t;
		oat = isadev2sat(alt2fl(alt, qnh), isadev);

		printf("V:%5.01lf  +V:%5.02lf  H:%5.0lf  fpm:%4.0lf  "
		    "s:%6.0lf  M:%5.03lf\n", kcas, (kcas - old_kcas) / total_t,
		    alt, ((alt - old_alt) / total_t) * 60, NM2MET(dist),
		    ktas2mach(kcas2ktas(kcas, alt2press(alt, qnh), oat), oat));
#endif	/* STEP_DEBUG */
	}
	if (burnp != NULL)
		*burnp = burn;

	return (dist);
}

double
dist2accelclb(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double tp_alt, double fuel, vect2_t dir,
    double flap_ratio, double *alt, double *kcas, vect2_t wind,
    double alt_tgt, double kcas_tgt, double mach_lim, double dist_tgt,
    accelclb_t type, double *burnp)
{
	double alt1 = *alt;
	double dist = 0, burn = 0;
	double wind_mps = KT2MPS(vect2_dotprod(wind, dir));

	ASSERT(*alt <= alt_tgt);
	ASSERT(*kcas <= kcas_tgt);

	while (dist < dist_tgt && (alt_tgt - (*alt) > ALT_THRESH ||
	    kcas_tgt - (*kcas) > KCAS_THRESH)) {
		double tas_mps = KT2MPS(kcas2ktas(*kcas, alt2press(*alt, qnh),
		    isadev2sat(alt2fl(*alt, qnh), isadev)));
		double rmng = NM2MET(dist_tgt - dist);
		double t_rmng = MIN(rmng / tas_mps, SECS_PER_STEP);
		double accel_t, clb_t, oat, Ps, ktas_lim_mach, kcas_lim_mach,
		    kcas_lim;

		oat = isadev2sat(alt2fl(*alt, qnh), isadev);
		Ps = alt2press(*alt, qnh);
		ktas_lim_mach = mach2ktas(mach_lim, oat);
		kcas_lim_mach = ktas2kcas(ktas_lim_mach, Ps, oat);

		kcas_lim = kcas_tgt;
		if (*alt < flt->spd_lim_alt && kcas_lim > flt->spd_lim)
			kcas_lim = flt->spd_lim;
		if (kcas_lim > kcas_lim_mach)
			kcas_lim = kcas_lim_mach;
		if (alt_tgt - (*alt) < ALT_THRESH && kcas_lim < kcas_tgt)
			kcas_tgt = kcas_lim;

		/*
		 * ACCEL_THEN_CLB and ACCEL_TAKEOFF first accelerate to kcas2
		 * and then climb. ACCEL_AND_CLB does a 50/50 time split.
		 */
		if (type == ACCEL_THEN_CLB || type == ACCEL_TAKEOFF)
			accel_t = t_rmng;
		else
			accel_t = t_rmng / 2;

#ifdef	STEP_DEBUG
		double old_alt = *alt;
		double old_kcas = *kcas;
#endif	/* STEP_DEBUG */

		if (!spd_chg_step(B_TRUE, isadev, tp_alt, qnh,
		    type == ACCEL_TAKEOFF && (*alt) == alt1, *alt, kcas,
		    kcas_lim, wind_mps, flt->zfw + fuel - burn, flap_ratio,
		    acft, flt, &dist, &accel_t, &burn)) {
			return (NAN);
		}

		clb_t = t_rmng - accel_t;
		if (clb_t > 0 && alt_tgt - (*alt) > ALT_THRESH &&
		    !alt_chg_step(B_TRUE, isadev, tp_alt, qnh, alt, kcas,
		    alt_tgt, wind_mps, flt->zfw + fuel - burn, flap_ratio,
		    acft, flt, &dist, &clb_t, &burn)) {
			return (NAN);
		}

#ifdef	STEP_DEBUG
		double total_t;

		total_t = accel_t + clb_t;
		oat = isadev2sat(alt2fl(*alt, qnh), isadev);

		printf("V:%5.01lf  +V:%5.02lf  H:%5.0lf  fpm:%4.0lf  "
		    "s:%6.0lf  M:%5.03lf\n", *kcas, ((*kcas) - old_kcas) /
		    total_t, *alt, (((*alt) - old_alt) / total_t) * 60,
		    NM2MET(dist), ktas2mach(kcas2ktas(*kcas, alt2press(*alt,
		    qnh), oat), oat));
#endif	/* STEP_DEBUG */
	}
	if (burnp != NULL)
		*burnp = burn;

	return (dist);
}

/* TODO:
double
dist2deceldes(const flt_perf_t *flt, const acft_perf_t *acft, double isadev,
    double qnh, double tp_alt, double fuel, vect2_t dir, double flap_ratio,
    double *alt, double *kcas, vect2_t wind, double alt_tgt, double kcas_tgt,
    double mach_lim, double dist_tgt, accelclb_t type, double *burnp)
{
}
*/

double
perf_TO_spd(const flt_perf_t *flt, const acft_perf_t *acft)
{
	double mass = flt->zfw + flt->fuel;
	double lift = MASS2GFORCE(mass);
	double Cl = wavg(quad_bezier_func(acft->cl_max_aoa, acft->cl_curve),
	    quad_bezier_func(acft->cl_flap_max_aoa, acft->cl_flap_curve),
	    flt->to_flap);
	double Pd = lift / (Cl * acft->wing_area);
	double tas = sqrt((2 * Pd) / ISA_SL_DENS);
	return (MPS2KT(tas));
}

/*
 * Calculates the specific fuel consumption of the aircraft engines in
 * a given instant.
 *
 * @param acft Aircraft performance specification structure.
 * @param thr Thrust setting on the engine in kN.
 * @param dens Air density in kg/m^3.
 * @param isadev ISA temperature deviation in degrees C.
 *
 * @return The aircraft's engine's specific fuel consumption at the specified
 *	conditions in kg/hr.
 */
double
acft_get_sfc(const acft_perf_t *acft, double thr, double dens, double isadev)
{
	return (quad_bezier_func(thr, acft->sfc_thr_curve) *
	    quad_bezier_func(dens, acft->sfc_dens_curve) *
	    quad_bezier_func(isadev, acft->sfc_isa_curve));
}

/*
 * Converts a true airspeed to Mach number.
 *
 * @param ktas True airspeed in knots.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Mach number.
 */
double
ktas2mach(double ktas, double oat)
{
	return (KT2MPS(ktas) / speed_sound(oat));
}

/*
 * Converts Mach number to true airspeed.
 *
 * @param ktas Mach number.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return True airspeed in knots.
 */
double
mach2ktas(double mach, double oat)
{
	return (MPS2KT(mach * speed_sound(oat)));
}

/*
 * Converts true airspeed to calibrated airspeed.
 *
 * @param ktas True airspeed in knots.
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Calibrated airspeed in knots.
 */
double
ktas2kcas(double ktas, double pressure, double oat)
{
	return (impact_press2kcas(impact_press(ktas2mach(ktas, oat),
	    pressure)));
}

/*
 * Converts impact pressure to calibrated airspeed.
 *
 * @param impact_pressure Impact air pressure in Pa.
 *
 * @return Calibrated airspeed in knots.
 */
double
impact_press2kcas(double impact_pressure)
{
	return (MPS2KT(ISA_SPEED_SOUND * sqrt(5 *
	    (pow(impact_pressure / ISA_SL_PRESS + 1, 0.2857142857) - 1))));
}

/*
 * Converts calibrated airspeed to true airspeed.
 *
 * @param ktas Calibrated airspeed in knots.
 * @param pressure Static air pressure in hPa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return True airspeed in knots.
 */
double
kcas2ktas(double kcas, double pressure, double oat)
{
	double	qc, mach;

	/*
	 * Take the CAS equation and solve for qc (impact pressure):
	 *
	 * qc = P0(((cas^2 / 5* a0^2) + 1)^3.5 - 1)
	 *
	 * Where P0 is pressure at sea level, cas is calibrated airspeed
	 * in m.s^-1 and a0 speed of sound at ISA temperature.
	 */
	qc = ISA_SL_PRESS * (pow((POW2(KT2MPS(kcas)) / (5 *
	    POW2(ISA_SPEED_SOUND))) + 1, 3.5) - 1);

	/*
	 * Next take the impact pressure equation and solve for Mach number:
	 *
	 * M = sqrt(5 * (((qc / P) + 1)^(2/7) - 1))
	 *
	 * Where qc is impact pressure and P is local static pressure.
	 */
	mach = sqrt(5 * (pow((qc / pressure) + 1, 0.2857142857142) - 1));

	/*
	 * Finally convert Mach number to true airspeed at local temperature.
	 */
	return (mach2ktas(mach, oat));
}

/*
 * Converts Mach number to equivalent airspeed (calibrated airspeed corrected
 * for compressibility).
 *
 * @param mach Mach number.
 * @param oat Static outside static air pressure in Pa.
 *
 * @return Equivalent airspeed in knots.
 */
double
mach2keas(double mach, double press)
{
	return (MPS2KT(ISA_SPEED_SOUND * mach * sqrt(press / ISA_SL_PRESS)));
}

/*
 * Converts equivalent airspeed (calibrated airspeed corrected for
 * compressibility) to Mach number.
 *
 * @param mach Equivalent airspeed in knots.
 * @param oat Static outside static air pressure in Pa.
 *
 * @return Mach number.
 */
double
keas2mach(double keas, double press)
{
	/*
	 * Take the mach-to-EAS equation and solve for Mach number:
	 *
	 * M = Ve / (a0 * sqrt(P / P0))
	 *
	 * Where Ve is equivalent airspeed in m.s^-1, P is local static
	 * pressure and P0 is ISA sea level pressure (in Pa).
	 */
	return (KT2MPS(keas) / (ISA_SPEED_SOUND * sqrt(press / ISA_SL_PRESS)));
}

/*
 * Calculates static air pressure from pressure altitude.
 *
 * @param alt Pressure altitude in feet.
 * @param qnh Local QNH in Pa.
 *
 * @return Air pressure in Pa.
 */
double
alt2press(double alt, double qnh)
{
	return (qnh * pow(1 - (ISA_TLR_PER_1M * FEET2MET(alt)) /
	    ISA_SL_TEMP_K, (EARTH_GRAVITY * DRY_AIR_MOL) /
	    (R_univ * ISA_TLR_PER_1M)));
}

/*
 * Calculates pressure altitude from static air pressure.
 *
 * @param alt Static air pressure in Pa.
 * @param qnh Local QNH in Pa.
 *
 * @return Pressure altitude in feet.
 */
double
press2alt(double press, double qnh)
{
	return (MET2FEET((ISA_SL_TEMP_K * (1 - pow(press / qnh,
	    (R_univ * ISA_TLR_PER_1M) / (EARTH_GRAVITY * DRY_AIR_MOL)))) /
	    ISA_TLR_PER_1M));
}

/*
 * Converts pressure altitude to flight level.
 *
 * @param alt Pressure altitude in feet.
 * @param qnh Local QNH in hPa.
 *
 * @return Flight level number.
 */
double
alt2fl(double alt, double qnh)
{
	return (press2alt(alt2press(alt, qnh), ISA_SL_PRESS) / 100);
}

/*
 * Converts flight level to pressure altitude.
 *
 * @param fl Flight level number.
 * @param qnh Local QNH in hPa.
 *
 * @return Pressure altitude in feet.
 */
double
fl2alt(double fl, double qnh)
{
	return (press2alt(alt2press(fl * 100, ISA_SL_PRESS), qnh));
}

/*
 * Converts static air temperature to total air temperature.
 *
 * @param sat Static air temperature in degrees C.
 * @param mach Flight mach number.
 *
 * @return Total air temperature in degrees C.
 */
double
sat2tat(double sat, double mach)
{
	return (KELVIN2C(C2KELVIN(sat) * (1 + ((GAMMA - 1) / 2) * POW2(mach))));
}

/*
 * Converts total air temperature to static air temperature.
 *
 * @param tat Total air temperature in degrees C.
 * @param mach Flight mach number.
 *
 * @return Static air temperature in degrees C.
 */
double
tat2sat(double tat, double mach)
{
	return (KELVIN2C(C2KELVIN(tat) / (1 + ((GAMMA - 1) / 2) * POW2(mach))));
}

/*
 * Converts static air temperature to ISA deviation.
 *
 * @param fl Flight level (barometric altitude at QNE in 100s of ft).
 * @param sat Static air temperature in degrees C.
 *
 * @return ISA deviation in degress C.
 */
double
sat2isadev(double fl, double sat)
{
	return (sat - (ISA_SL_TEMP_C - ((fl / 10) * ISA_TLR_PER_1000FT)));
}

/*
 * Converts ISA deviation to static air temperature.
 *
 * @param fl Flight level (barometric altitude at QNE in 100s of ft).
 * @param isadev ISA deviation in degrees C.
 *
 * @return Local static air temperature.
 */
double
isadev2sat(double fl, double isadev)
{
	return (isadev + ISA_SL_TEMP_C - ((fl / 10) * ISA_TLR_PER_1000FT));
}

/*
 * Returns the speed of sound in m/s in dry air at `oat' degrees C (static).
 */
double
speed_sound(double oat)
{
	/*
	 * This is an approximation that for common flight temperatures
	 * (-65 to +65) is less than 0.1% off.
	 */
	return (20.05 * sqrt(C2KELVIN(oat)));
}

/*
 * Calculates air density.
 *
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Local air density in kg.m^-3.
 */
double
air_density(double pressure, double oat)
{
	/*
	 * Density of dry air is:
	 *
	 * rho = p / (R_spec * T)
	 *
	 * Where p is local static air pressure, R_spec is the specific gas
	 * constant for dry air and T is absolute temperature.
	 */
	return (pressure / (R_spec * C2KELVIN(oat)));
}

/*
 * Calculates impact pressure. This is dynamic pressure with air
 * compressibility considered.
 *
 * @param pressure Static air pressure in Pa.
 * @param mach Flight mach number.
 *
 * @return Impact pressure in Pa.
 */
double
impact_press(double mach, double pressure)
{
	/*
	 * In isentropic flow, impact pressure for air (gamma = 1.4) is:
	 *
	 * qc = P((1 + 0.2 * M^2)^(7/2) - 1)
	 *
	 * Where P is local static air pressure and M is flight mach number.
	 */
	return (pressure * (pow(1 + 0.2 * POW2(mach), 3.5) - 1));
}

/*
 * Calculates dynamic pressure.
 *
 * @param pressure True airspeed in knots.
 * @param press Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Dynamic pressure in Pa.
 */
double
dyn_press(double ktas, double press, double oat)
{
	return (0.5 * air_density(press, oat) * POW2(KT2MPS(ktas)));
}

/*
 * Computes the adiabatic heating experienced by air when compressed in a
 * turbine engine's compressor. The P-T relation for adiabatic heating is:
 *
 *	P1^(1 - gamma) T1^(gamma) = P2^(1 - gamma) T2^(gamma)
 *
 * Solving for T2:
 *
 *	T2 = ((P1^(1 - Gamma) T1^(Gamma)) / P2^(1 - Gamma))^(1 / Gamma)
 *
 * Since P2 / P1 is the compressor pressure ratio, we can cancel out P1
 * to '1' and simply replace P2 by the pressure ratio P_r:
 *
 *	T2 = (T1^(Gamma) / P_r^(1 - Gamma))^(1 / Gamma)
 *
 * @param press_ratio Pressure ratio of the compressor (dimensionless).
 * @param start_temp Starting gas temperature (in degrees Celsius).
 *
 * @return Compressed gas temperature (in degrees Celsius).
 */
double
adiabatic_heating(double press_ratio, double start_temp)
{
	return (KELVIN2C(pow(pow(C2KELVIN(start_temp), GAMMA) /
	    pow(press_ratio, 1 - GAMMA), 1 / GAMMA)));
}
