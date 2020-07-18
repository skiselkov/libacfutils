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
 *
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_AIRPORTDB_H_
#define	_AIRPORTDB_H_

#include <acfutils/avl.h>
#include <acfutils/geom.h>
#include <acfutils/list.h>
#include <acfutils/helpers.h>
#include <acfutils/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct airportdb {
	bool_t		inited;
	bool_t		ifr_only;
	char		*xpdir;
	char		*cachedir;
	int		xp_airac_cycle;
	double		load_limit;

	mutex_t		lock;

	avl_tree_t	apt_dat;
	avl_tree_t	geo_table;
	avl_tree_t	arpt_index;
} airportdb_t;

typedef struct airport airport_t;
typedef struct runway runway_t;
typedef struct runway_end runway_end_t;

typedef enum {
	RWY_SURF_ASPHALT = 1,
	RWY_SURF_CONCRETE = 2,
	RWY_SURF_GRASS = 3,
	RWY_SURF_DIRT = 4,
	RWY_SURF_GRAVEL = 5,
	RWY_SURF_DRY_LAKEBED = 12,
	RWY_SURF_WATER = 13,
	RWY_SURF_SNOWICE = 14,
	RWY_SURF_TRANSPARENT = 15
} rwy_surf_t;

struct runway_end {
	char		id[4];		/* runway ID, nul-terminated */
	geo_pos3_t	thr;		/* threshold position (elev in FEET!) */
	double		displ;		/* threshold displacement in meters */
	double		blast;		/* stopway/blastpad length in meters */
	double		gpa;		/* glidepath angle in degrees */
	double		tch;		/* threshold clearing height in feet */

	/* computed on load_airport */
	vect2_t		thr_v;		/* threshold vector coord */
	vect2_t		dthr_v;		/* displaced threshold vector coord */
	double		hdg;		/* true heading */
	vect2_t		*apch_bbox;	/* in-air approach bbox */
	double		land_len;	/* length avail for landing in meters */
};

struct runway {
	airport_t	*arpt;
	double		width;
	runway_end_t	ends[2];
	char		joint_id[8];
	char		rev_joint_id[8];
	rwy_surf_t	surf;

	/* computed on load_airport */
	double		length;		/* meters */
	vect2_t		*prox_bbox;	/* on-ground approach bbox */
	vect2_t		*rwy_bbox;	/* above runway for landing */
	vect2_t		*tora_bbox;	/* on-runway on ground (for tkoff) */
	vect2_t		*asda_bbox;	/* on-runway on ground (for stopping) */

	avl_node_t	node;
};

typedef enum {
	FREQ_TYPE_REC,	/* pre-recorded message ATIS, AWOS or ASOS */
	FREQ_TYPE_CTAF,
	FREQ_TYPE_CLNC,
	FREQ_TYPE_GND,
	FREQ_TYPE_TWR,
	FREQ_TYPE_APP,
	FREQ_TYPE_DEP
} freq_type_t;

typedef struct {
	freq_type_t	type;
	uint64_t	freq;	/* Hz */
	char		name[32];
	list_node_t	node;
} freq_info_t;

typedef struct {
	char		icao[8];
	geo_pos2_t	pos;
	avl_node_t	node;
} arpt_index_t;

struct airport {
	char		icao[5];	/* 4-letter ID, nul terminated */
	char		cc[4];		/* 2-letter ICAO country/region code */
	char		name[24];	/* Airport name, nul terminated */
	geo_pos3_t	refpt;		/* airport reference point location */
					/* (^^^ elev in FEET!) */
	bool_t		geo_linked;	/* airport is in geo_table */
	double		TA;		/* transition altitude in feet */
	double		TL;		/* transition level in feet */
	avl_tree_t	rwys;
	list_t		freqs;

	bool_t		load_complete;	/* true if we've done load_airport */
	vect3_t		ecef;		/* refpt ECEF coordinates */
	fpp_t		fpp;		/* ortho fpp centered on refpt */
	bool_t		in_navdb;	/* used by recreate_apt_dat_cache */

	avl_node_t	apt_dat_node;	/* apt_dat tree */
	list_node_t	cur_arpts_node;	/* cur_arpts list */
	avl_node_t	tile_node;	/* tiles in the airport_geo_tree */
};

API_EXPORT void airportdb_create(airportdb_t *db, const char *xpdir,
    const char *cachedir);
API_EXPORT void airportdb_destroy(airportdb_t *db);

API_EXPORT void airportdb_lock(airportdb_t *db);
API_EXPORT void airportdb_unlock(airportdb_t *db);

#define	recreate_cache			ACFSYM(recreate_cache)
API_EXPORT bool_t recreate_cache(airportdb_t *db);

#define	find_nearest_airports		ACFSYM(find_nearest_airports)
API_EXPORT list_t *find_nearest_airports(airportdb_t *db, geo_pos2_t my_pos);

#define	free_nearest_airport_list	ACFSYM(free_nearest_airport_list)
API_EXPORT void free_nearest_airport_list(list_t *l);

#define	set_airport_load_limit		ACFSYM(set_airport_load_limit)
API_EXPORT void set_airport_load_limit(airportdb_t *db, double limit);

#define	load_nearest_airport_tiles	ACFSYM(load_nearest_airport_tiles)
API_EXPORT void load_nearest_airport_tiles(airportdb_t *db, geo_pos2_t my_pos);

#define	unload_distant_airport_tiles	ACFSYM(unload_distant_airport_tiles)
API_EXPORT void unload_distant_airport_tiles(airportdb_t *db,
    geo_pos2_t my_pos);

#define	airport_lookup	ACFSYM(airport_lookup)
API_EXPORT airport_t *airport_lookup(airportdb_t *db, const char *icao,
    geo_pos2_t pos);

#define	airport_lookup_global	ACFSYM(airport_lookup_global)
API_EXPORT airport_t *airport_lookup_global(airportdb_t *db, const char *icao);

#define	airport_find_runway	ACFSYM(airport_find_runway)
API_EXPORT bool_t airport_find_runway(airport_t *arpt, const char *rwy_id,
    runway_t **rwy_p, unsigned *end_p);

#define	matching_airport_in_tile_with_TATL \
    ACFSYM(matching_airport_in_tile_with_TATL)
API_EXPORT airport_t *matching_airport_in_tile_with_TATL(airportdb_t *db,
    geo_pos2_t pos, const char *search_icao);

#define	airportdb_xp11_airac_cycle	ACFSYM(airportdb_xp11_airac_cycle)
API_EXPORT bool_t airportdb_xp11_airac_cycle(const char *xpdir, int *cycle);

#ifdef	__cplusplus
}
#endif

#endif	/* _AIRPORTDB_H_ */
