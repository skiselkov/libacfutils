/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_ODB_H_
#define	_ACF_UTILS_ODB_H_

#include <time.h>

#include "geom.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	OBST_BLDG,	/* building */
	OBST_TOWER,	/* tower */
	OBST_STACK,	/* smoke stack */
	OBST_RIG,	/* elevated rig */
	OBST_POLE,	/* (utility) pole */
	OBST_OTHER	/* some other kind of obstacle */
} obst_type_t;

typedef enum {
	OBST_LIGHT_UNK,			/* lighting status unknown */
	OBST_LIGHT_NONE,		/* not lighted */
	OBST_LIGHT_LIGHTED,		/* lighted by unknown type of light */
	OBST_LIGHT_RED,			/* continuous red */
	OBST_LIGHT_STROBE_WR_MED,	/* med intensity white & red strobe */
	OBST_LIGHT_STROBE_WR_HI,	/* high intensity white & red strobe */
	OBST_LIGHT_STROBE_W_MED,	/* medium intensity white strobe */
	OBST_LIGHT_STROBE_W_HI,		/* high intensity white strobe */
	OBST_LIGHT_FLOOD,		/* flood light */
	OBST_LIGHT_DUAL_MED_CAT,	/* dual medium catenary */
	OBST_LIGHT_SYNC_RED		/* synchronized red */
} obst_light_t;

typedef struct odb_s odb_t;
typedef void (*add_obst_cb_t)(obst_type_t type, geo_pos3_t pos,
    float agl, obst_light_t light, unsigned quant, void *userinfo);

API_EXPORT odb_t *odb_init(const char *xpdir, const char *cainfo);
API_EXPORT void odb_fini(odb_t *odb);

API_EXPORT void odb_set_unload_delay(odb_t *odb, unsigned seconds);
API_EXPORT time_t odb_get_cc_refresh_date(odb_t *odb, const char *cc);
API_EXPORT bool_t odb_refresh_cc(odb_t *odb, const char *cc);
API_EXPORT bool_t odb_get_obstacles(odb_t *odb, int lat, int lon,
    add_obst_cb_t cb, void *userinfo);

API_EXPORT void odb_set_proxy(odb_t *odb, const char *proxy);
API_EXPORT size_t odb_get_proxy(odb_t *odb, char *proxy, size_t cap);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_ODB_H_ */
