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

#ifndef	_ACF_UTILS_PERF_H_
#define	_ACF_UTILS_PERF_H_

#include "geom.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Temperature unit conversions.
 */
#define	KELVIN2C(k)	((k) - 273.15)
#define	C2KELVIN(c)	((c) + 273.15)
#define	FAH2C(f)	(((f) - 32) * 0.555555)
#define	C2FAH(c)	(((c) * 1.8) + 32)
#define	FAH2KELVIN(f)	(((f) + 459.67) * 0.5555555555)
#define	KELVIN2FAH(k)	(((k) * 1.8) - 459.67)

/*
 * Length and velocity unit conversions.
 */
#define	FEET2MET(x)	((x) * 0.3048)		/* feet to meters */
#define	MET2FEET(x)	((x) * 3.2808398950131)	/* meters to feet */
#define	NM2MET(x)	((x) * 1852)		/* nautical miles to meters */
#define	MET2NM(x)	((x) / 1852.0)		/* meters to nautical miles */
#define	KT2MPS(k)	(NM2MET(k) / 3600)	/* knots to m/s */
#define	MPS2KT(k)	(MET2NM(k) * 3600)	/* m/s to knots */
#define	FPM2MPS(f)	FEET2MET((f) / 60.0)	/* ft.min^-1 to m.s^-1 */
#define	MPS2FPM(m)	MET2FEET((m) * 60.0)	/* m.s^-1 to ft.min^-1 */

/*
 * Other unit conversions
 */
#define	HPA2PA(x)	((x) / 100)
#define	PA2HPA(x)	((x) * 100)

/*
 * ISA (International Standard Atmosphere) parameters.
 */
#define	ISA_SL_TEMP_C		15.0	/* Sea level temperature in degrees C */
#define	ISA_SL_TEMP_K		288.15	/* Sea level temperature in Kelvin */
#define	ISA_SL_PRESS		101325	/* Sea level pressure in Pa */
#define	ISA_SL_DENS		1.225	/* Sea level density in kg/m^3 */
#define	ISA_TLR_PER_1000FT	1.98	/* Temperature lapse rate per 1000ft */
#define	ISA_TLR_PER_1M		0.0065	/* Temperature lapse rate per 1 meter */
#define	ISA_SPEED_SOUND		340.3	/* Speed of sound at sea level */
#define	ISA_TP_ALT		36089	/* Tropopause altitude in feet */

typedef struct {
	int	spd;
	double	Cd;
} drag_coeff_t;

typedef struct {
	double	zfw;
	double	fuel;
	double	clb_ias;
	double	clb_mach;
	double	crz_ias;
	double	crz_mach;
	double	crz_lvl;
	double	des_ias;
	double	des_mach;
	double	to_flap;
	double	accel_height;
	double	spd_lim;
	double	spd_lim_alt;

	double	thr_derate;
} flt_perf_t;

typedef struct {
	char		*acft_type;

	flt_perf_t	ref;	/* Reference performance values */

	double		max_fuel;
	double		max_gw;

	char		*eng_type;

	/* Base max thrust in Newtons @ ISA conditions */
	double		eng_max_thr;
	/* Base min thrust in Newtons @ ISA conditions */
	double		eng_min_thr;
	/*
	 * eng_max_thr fraction as a function of air density (in kg/m^3).
	 */
	bezier_t	*thr_dens_curve;
	/*
	 * eng_max_thr fraction as a function of ISA temperature deviation
	 * in degrees C.
	 */
	bezier_t	*thr_isa_curve;
	/*
	 * Engine specific fuel consumption in kg/hr as a function of
	 * thrust in Kilonewtons.
	 */
	bezier_t	*sfc_thr_curve;
	/*
	 * Engine specific fuel consumption modifier (0 - 1) as a function
	 * of air density (in kg/m^3).
	 */
	bezier_t	*sfc_dens_curve;
	/*
	 * Engine specific fuel consumption modifier (0 - 1) as a function
	 * of ISA temperature deviation in degrees C.
	 */
	bezier_t	*sfc_isa_curve;

	bezier_t	*cl_curve;
	bezier_t	*cl_flap_curve;
	double		cl_max_aoa;
	bezier_t	*cd_curve;
	bezier_t	*cd_flap_curve;
	double		cl_flap_max_aoa;
	double		wing_area;
} acft_perf_t;

/* Type of acceleration-climb */
typedef enum {
	ACCEL_THEN_CLB,	/* First accelerate, then climb */
	ACCEL_AND_CLB,	/* Accel & climb simultaneously (50/50 energy split) */
	ACCEL_TAKEOFF	/* Accel to target speed first without needing lift */
} accelclb_t;

acft_perf_t *acft_perf_parse(const char *filename);
void acft_perf_destroy(acft_perf_t *perf);

flt_perf_t *flt_perf_new(const acft_perf_t *acft);
void flt_perf_destroy(flt_perf_t *flt);

double eng_max_thr_avg(const flt_perf_t *flt, const acft_perf_t *acft,
    double alt1, double alt2, double ktas, double qnh, double isadev,
    double tp_alt);

double accelclb2dist(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double tp_alt, double fuel, vect2_t dir,
    double alt1, double kcas1, vect2_t wind1,
    double alt2, double kcas2, vect2_t wind2,
    double flap_ratio, double mach_lim, accelclb_t type, double *burnp);
double dist2accelclb(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double tp_alt, double fuel, vect2_t dir,
    double flap_ratio, double *alt, double *kcas, vect2_t wind,
    double alt_tgt, double kcas_tgt, double mach_lim, double dist_tgt,
    accelclb_t type, double *burnp);

double dist2deceldes(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double tp_alt, double fuel, vect2_t dir,
    double flap_ratio, double *alt, double *kcas, vect2_t wind,
    double alt_tgt, double kcas_tgt, double mach_lim, double dist_tgt,
    accelclb_t type, double *burnp);

double perf_TO_spd(const flt_perf_t *flt, const acft_perf_t *acft);

double acft_get_sfc(const acft_perf_t *acft, double thr, double dens,
    double isadev);

double alt2press(double alt, double qnh);
double press2alt(double press, double qnh);

double alt2fl(double alt, double qnh);
double fl2alt(double alt, double qnh);

double ktas2mach(double ktas, double oat);
double mach2ktas(double mach, double oat);

double ktas2kcas(double ktas, double pressure, double oat);
double kcas2ktas(double kcas, double pressure, double oat);

double mach2keas(double mach, double press);
double keas2mach(double keas, double press);

double sat2tat(double sat, double mach);
double tat2sat(double tat, double mach);

double sat2isadev(double fl, double sat);
double isadev2sat(double fl, double isadev);

double speed_sound(double oat);
double air_density(double pressure, double oat);
double impact_press(double mach, double pressure);
double dyn_press(double ktas, double press, double oat);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_PERF_H_ */
