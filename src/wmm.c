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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>

#include "GeomagnetismLibrary.h"

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/perf.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/wmm.h>

struct wmm_s {
	/* time-modified model according to year passed to wmm_open */
	MAGtype_MagneticModel	*timed_model;
	MAGtype_MagneticModel	*fixed_model;
	MAGtype_Ellipsoid	ellip;
};

/*
 * Opens a World Magnetic Model coefficient file and time-adjusts it.
 *
 * @param filename Filename containing the world magnetic model (usually
 *	named something like 'WMM.COF').
 * @param year Fractional year for which to time-adjust the model. For
 *	example, Apr 19 2016 is the 109th day of the year 2016, so it is
 *	fractional year 2016.3 (rounding to 1 decimal digit is sufficient).
 *	N.B. the model has limits of applicability. Be sure to check them
 *	using wmm_get_start and wmm_get_end before trusting the values
 *	returned by the model.
 *
 * @return If successful, the initialized model suitable for passing to
 *	wmm_mag2true. If a failure occurs, returns NULL instead.
 */
wmm_t *
wmm_open(const char *filename, double year)
{
	wmm_t			*wmm;
	int			n_max, n_terms;
	MAGtype_Date		date = { .DecimalYear = year };

	wmm = safe_calloc(1, sizeof (*wmm));
	if (!MAG_robustReadMagModels(filename, &wmm->fixed_model)) {
		free(wmm);
		return (NULL);
	}
	n_max = MAX(wmm->fixed_model->nMax, 0);
	n_terms = ((n_max + 1) * (n_max + 2) / 2);
	wmm->timed_model = MAG_AllocateModelMemory(n_terms);
	ASSERT(wmm->timed_model != NULL);
	MAG_TimelyModifyMagneticModel(date, wmm->fixed_model, wmm->timed_model);
	wmm->ellip = (MAGtype_Ellipsoid){
		.a = wgs84.a,
		.b = wgs84.b,
		.fla = wgs84.f,
		.eps = wgs84.ecc,
		.epssq = wgs84.ecc2,
		.re = wgs84.r
	};

	return (wmm);
}

void
wmm_reopen(wmm_t *wmm, double year)
{
	MAGtype_Date date = { .DecimalYear = year };

	ASSERT(wmm != NULL);
	ASSERT(wmm->fixed_model != NULL);
	ASSERT(wmm->timed_model != NULL);

	MAG_TimelyModifyMagneticModel(date, wmm->fixed_model, wmm->timed_model);
}

/*
 * Closes a model returned by wmm_open and releases all its resources.
 */
void
wmm_close(wmm_t *wmm)
{
	ASSERT(wmm != NULL);
	MAG_FreeMagneticModelMemory(wmm->fixed_model);
	MAG_FreeMagneticModelMemory(wmm->timed_model);
	free(wmm);
}

/*
 * Returns the earliest fractional year at which the provided model is valid.
 */
double
wmm_get_start(const wmm_t *wmm)
{
	return (wmm->timed_model->epoch);
}

/*
 * Returns the latest fractional year at which the provided model is valid.
 */
double
wmm_get_end(const wmm_t *wmm)
{
	return (wmm->timed_model->CoefficientFileEndDate);
}

/*
 * Returns the magnetic declination (variation) in degrees at a given point.
 * East declination is positive, west negative.
 *
 * @param wmm Magnetic model to use. See wmm_open.
 * @param p Geodetic position on the WGS84 spheroid for which to determine
 *	the magnetic declination.
 */
double
wmm_get_decl(const wmm_t *wmm, geo_pos3_t p)
{
	MAGtype_CoordSpherical		coord_sph;
	MAGtype_CoordGeodetic		coord_geo = {
		.lambda = p.lon, .phi = p.lat,
		.HeightAboveEllipsoid = FEET2MET(p.elev)
	};
	MAGtype_GeoMagneticElements	gme;

	MAG_GeodeticToSpherical(wmm->ellip, coord_geo, &coord_sph);
	MAG_Geomag(wmm->ellip, coord_sph, coord_geo, wmm->timed_model, &gme);

	return (gme.Decl);
}

/*
 * Converts a magnetic heading to true according to the world magnetic model.
 *
 * @param wmm Magnetic model to use. See wmm_open.
 * @param m Magnetic heading in degrees to convert.
 * @param p Geodetic position on the WGS84 spheroid for which to determine
 *	the conversion.
 *
 * @return The converted true heading in degrees.
 */
double
wmm_mag2true(const wmm_t *wmm, double m, geo_pos3_t p)
{
	ASSERT(is_valid_hdg(m));
	return (normalize_hdg(m + wmm_get_decl(wmm, p)));
}

/*
 * Converts a true heading to magnetic according to the world magnetic model.
 *
 * @param wmm Magnetic model to use. See wmm_open.
 * @param t True heading in degrees to convert.
 * @param p Geodetic position on the WGS-84 spheroid for which to determine
 *	the conversion.
 *
 * @return The converted magnetic heading in degrees.
 */
double
wmm_true2mag(const wmm_t *wmm, double t, geo_pos3_t p)
{
	ASSERT(is_valid_hdg(t));
	return (normalize_hdg(t - wmm_get_decl(wmm, p)));
}
