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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#include <errno.h>
#include <iconv.h>
#include <stddef.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#if	IBM
#include <windows.h>
#include <strsafe.h>
#else	/* !IBM */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif	/* !IBM */

#include "acfutils/airportdb.h"
#include "acfutils/assert.h"
#include "acfutils/avl.h"
#include "acfutils/conf.h"
#include "acfutils/geom.h"
#include "acfutils/helpers.h"
#include "acfutils/list.h"
#include "acfutils/math.h"
#include "acfutils/optional.h"
#include "acfutils/perf.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/types.h"

#define	RWY_PROXIMITY_LAT_FRACT		3
#define	RWY_PROXIMITY_LON_DISPL		609.57	/* meters, 2000 ft */

#define	RWY_APCH_PROXIMITY_LAT_ANGLE	3.3	/* degrees */
#define	RWY_APCH_PROXIMITY_LON_DISPL	5500	/* meters */
/* precomputed, since it doesn't change */
#define	RWY_APCH_PROXIMITY_LAT_DISPL	(RWY_APCH_PROXIMITY_LON_DISPL * \
	__builtin_tan(DEG2RAD(RWY_APCH_PROXIMITY_LAT_ANGLE)))
#define	ARPTDB_CACHE_VERSION		21

#define	VGSI_LAT_DISPL_FACT		2	/* rwy width multiplier */
#define	VGSI_HDG_MATCH_THRESH		5	/* degrees */
#define	ILS_HDG_MATCH_THRESH		2	/* degrees */
/*
 * GS emitters don't originate their beam at the ground, so we add a bit of
 * a fudge factor to account for antenna height to our TCH computation.
 */
#define	ILS_GS_GND_OFFSET		5	/* meters */

#define	MIN_RWY_LEN			10	/* meters */
#define	RWY_GPA_LIMIT			10	/* degrees */
#define	RWY_TCH_LIMIT			200	/* feet */
#define	TCH_IS_VALID(tch)		(tch > 0 && tch < RWY_TCH_LIMIT)

#define	TILE_NAME_FMT			"%+03.0f%+04.0f"

#define	ARPT_LOAD_LIMIT			NM2MET(8)	/* meters */

/*
 * Visual Glide Slope Indicator type (PAPI, VASI, etc.).
 * Type codes used in apt.dat (XP-APT1000-Spec.pdf at data.x-plane.com).
 */
typedef enum {
	VGSI_VASI =		1,
	VGSI_PAPI_4L =		2,
	VGSI_PAPI_4R =		3,
	VGSI_PAPI_20DEG =	4,
	VGSI_PAPI_3C =		5
} vgsi_t;

typedef struct tile_s {
	geo_pos2_t	pos;	/* tile position (see `geo_pos2tile_pos') */
	avl_tree_t	arpts;	/* airport_t's sorted by `airport_compar' */
	avl_node_t	node;
} tile_t;

typedef struct {
	list_node_t	node;
	char		*fname;
} apt_dats_entry_t;

static struct {
	const char	*code;
	const char	*name;
} iso3166_codes[] = {
    { "AFG",	"Afghanistan" },
    { "ALA",	"Åland Islands" },
    { "ALB",	"Albania" },
    { "DZA",	"Algeria" },
    { "ASM",	"American Samoa" },
    { "AND",	"Andorra" },
    { "AGO",	"Angola" },
    { "AIA",	"Anguilla" },
    { "ATA",	"Antarctica" },
    { "ATG",	"Antigua and Barbuda" },
    { "ARG",	"Argentina" },
    { "ARM",	"Armenia" },
    { "ABW",	"Aruba" },
    { "AUS",	"Australia" },
    { "AUT",	"Austria" },
    { "AZE",	"Azerbaijan" },
    { "BHS",	"Bahamas" },
    { "BHR",	"Bahrain" },
    { "BGD",	"Bangladesh" },
    { "BRB",	"Barbados" },
    { "BLR",	"Belarus" },
    { "BEL",	"Belgium" },
    { "BLZ",	"Belize" },
    { "BEN",	"Benin" },
    { "BMU",	"Bermuda" },
    { "BTN",	"Bhutan" },
    { "BOL",	"Bolivia" },
    { "BES",	"Bonaire, Sint Eustatius and Saba" },
    { "BIH",	"Bosnia and Herzegovina" },
    { "BWA",	"Botswana" },
    { "BVT",	"Bouvet Island" },
    { "BRA",	"Brazil" },
    { "IOT",	"British Indian Ocean Territory" },
    { "BRN",	"Brunei Darussalam" },
    { "BGR",	"Bulgaria" },
    { "BFA",	"Burkina Faso" },
    { "BDI",	"Burundi" },
    { "CPV",	"Cabo Verde" },
    { "KHM",	"Cambodia" },
    { "CMR",	"Cameroon" },
    { "CAN",	"Canada" },
    { "CYM",	"Cayman Islands" },
    { "CAF",	"Central African Republic" },
    { "TCD",	"Chad" },
    { "CHL",	"Chile" },
    { "CHN",	"China" },
    { "CXR",	"Christmas Island" },
    { "CCK",	"Cocos Islands" },
    { "COL",	"Colombia" },
    { "COM",	"Comoros" },
    { "COD",	"Democratic Republic of the Congo" },
    { "COG",	"Congo" },
    { "COK",	"Cook Islands" },
    { "CRI",	"Costa Rica" },
    { "CIV",	"Côte d'Ivoire" },
    { "HRV",	"Croatia" },
    { "CUB",	"Cuba" },
    { "CUW",	"Curaçao" },
    { "CYP",	"Cyprus" },
    { "CZE",	"Czechia" },
    { "DNK",	"Denmark" },
    { "DJI",	"Djibouti" },
    { "DMA",	"Dominica" },
    { "DOM",	"Dominican Republic" },
    { "ECU",	"Ecuador" },
    { "EGY",	"Egypt" },
    { "SLV",	"El Salvador" },
    { "GNQ",	"Equatorial Guinea" },
    { "ERI",	"Eritrea" },
    { "EST",	"Estonia" },
    { "SWZ",	"Eswatini" },
    { "ETH",	"Ethiopia" },
    { "FLK",	"Falkland Islands" },
    { "FRO",	"Faroe Islands" },
    { "FJI",	"Fiji" },
    { "FIN",	"Finland" },
    { "FRA",	"France" },
    { "GUF",	"French Guiana" },
    { "PYF",	"French Polynesia" },
    { "ATF",	"French Southern Territories" },
    { "GAB",	"Gabon" },
    { "GMB",	"Gambia" },
    { "GEO",	"Georgia" },
    { "DEU",	"Germany" },
    { "GHA",	"Ghana" },
    { "GIB",	"Gibraltar" },
    { "GRC",	"Greece" },
    { "GRL",	"Greenland" },
    { "GRD",	"Grenada" },
    { "GLP",	"Guadeloupe" },
    { "GUM",	"Guam" },
    { "GTM",	"Guatemala" },
    { "GGY",	"Guernsey" },
    { "GIN",	"Guinea" },
    { "GNB",	"Guinea-Bissau" },
    { "GUY",	"Guyana" },
    { "HTI",	"Haiti" },
    { "HMD",	"Heard Island and McDonald Islands" },
    { "VAT",	"Holy See" },
    { "HND",	"Honduras" },
    { "HKG",	"Hong Kong" },
    { "HUN",	"Hungary" },
    { "ISL",	"Iceland" },
    { "IND",	"India" },
    { "IDN",	"Indonesia" },
    { "IRN",	"Iran" },
    { "IRQ",	"Iraq" },
    { "IRL",	"Ireland" },
    { "IMN",	"Isle of Man" },
    { "ISR",	"Israel" },
    { "ITA",	"Italy" },
    { "JAM",	"Jamaica" },
    { "JPN",	"Japan" },
    { "JEY",	"Jersey" },
    { "JOR",	"Jordan" },
    { "KAZ",	"Kazakhstan" },
    { "KEN",	"Kenya" },
    { "KIR",	"Kiribati" },
    { "PRK",	"Democratic People's Republic of Korea" },
    { "KOR",	"Republic of Korea" },
    { "KWT",	"Kuwait" },
    { "KGZ",	"Kyrgyzstan" },
    { "LAO",	"Laos" },
    { "LVA",	"Latvia" },
    { "LBN",	"Lebanon" },
    { "LSO",	"Lesotho" },
    { "LBR",	"Liberia" },
    { "LBY",	"Libya" },
    { "LIE",	"Liechtenstein" },
    { "LTU",	"Lithuania" },
    { "LUX",	"Luxembourg" },
    { "MAC",	"Macao" },
    { "MKD",	"Republic of North Macedonia" },
    { "MDG",	"Madagascar" },
    { "MWI",	"Malawi" },
    { "MYS",	"Malaysia" },
    { "MDV",	"Maldives" },
    { "MLI",	"Mali" },
    { "MLT",	"Malta" },
    { "MHL",	"Marshall Islands" },
    { "MTQ",	"Martinique" },
    { "MRT",	"Mauritania" },
    { "MUS",	"Mauritius" },
    { "MYT",	"Mayotte" },
    { "MEX",	"Mexico" },
    { "FSM",	"Micronesia" },
    { "MDA",	"Moldova" },
    { "MCO",	"Monaco" },
    { "MNG",	"Mongolia" },
    { "MNE",	"Montenegro" },
    { "MSR",	"Montserrat" },
    { "MAR",	"Morocco" },
    { "MOZ",	"Mozambique" },
    { "MMR",	"Myanmar" },
    { "NAM",	"Namibia" },
    { "NRU",	"Nauru" },
    { "NPL",	"Nepal" },
    { "NLD",	"Netherlands" },
    { "NCL",	"New Caledonia" },
    { "NZL",	"New Zealand" },
    { "NIC",	"Nicaragua" },
    { "NER",	"Niger" },
    { "NGA",	"Nigeria" },
    { "NIU",	"Niue" },
    { "NFK",	"Norfolk Island" },
    { "MNP",	"Northern Mariana Islands" },
    { "NOR",	"Norway" },
    { "OMN",	"Oman" },
    { "PAK",	"Pakistan" },
    { "PLW",	"Palau" },
    { "PSE",	"Palestine, State of" },
    { "PAN",	"Panama" },
    { "PNG",	"Papua New Guinea" },
    { "PRY",	"Paraguay" },
    { "PER",	"Peru" },
    { "PHL",	"Philippines" },
    { "PCN",	"Pitcairn" },
    { "POL",	"Poland" },
    { "PRT",	"Portugal" },
    { "PRI",	"Puerto Rico" },
    { "QAT",	"Qatar" },
    { "REU",	"Réunion" },
    { "ROU",	"Romania" },
    { "RUS",	"Russian Federation" },
    { "RWA",	"Rwanda" },
    { "BLM",	"Saint Barthélemy" },
    { "SHN",	"Saint Helena" },
    { "KNA",	"Saint Kitts and Nevis" },
    { "LCA",	"Saint Lucia" },
    { "MAF",	"Saint Martin" },
    { "SPM",	"Saint Pierre and Miquelon" },
    { "VCT",	"Saint Vincent and the Grenadines" },
    { "WSM",	"Samoa" },
    { "SMR",	"San Marino" },
    { "STP",	"Sao Tome and Principe" },
    { "SAU",	"Saudi Arabia" },
    { "SEN",	"Senegal" },
    { "SRB",	"Serbia" },
    { "SYC",	"Seychelles" },
    { "SLE",	"Sierra Leone" },
    { "SGP",	"Singapore" },
    { "SXM",	"Sint Maarten" },
    { "SVK",	"Slovakia" },
    { "SVN",	"Slovenia" },
    { "SLB",	"Solomon Islands" },
    { "SOM",	"Somalia" },
    { "ZAF",	"South Africa" },
    { "SGS",	"South Georgia and the South Sandwich Islands" },
    { "SSD",	"South Sudan" },
    { "ESP",	"Spain" },
    { "LKA",	"Sri Lanka" },
    { "SDN",	"Sudan" },
    { "SUR",	"Suriname" },
    { "SJM",	"Svalbard and Jan Mayen" },
    { "SWE",	"Sweden" },
    { "CHE",	"Switzerland" },
    { "SYR",	"Syrian Arab Republic" },
    { "TWN",	"Taiwan" },
    { "TJK",	"Tajikistan" },
    { "TZA",	"Tanzania" },
    { "THA",	"Thailand" },
    { "TLS",	"Timor-Leste" },
    { "TGO",	"Togo" },
    { "TKL",	"Tokelau" },
    { "TON",	"Tonga" },
    { "TTO",	"Trinidad and Tobago" },
    { "TUN",	"Tunisia" },
    { "TUR",	"Turkey" },
    { "TKM",	"Turkmenistan" },
    { "TCA",	"Turks and Caicos Islands" },
    { "TUV",	"Tuvalu" },
    { "UGA",	"Uganda" },
    { "UKR",	"Ukraine" },
    { "ARE",	"United Arab Emirates" },
    { "GBR",	"UK" },
    { "UMI",	"United States Minor Outlying Islands" },
    { "USA",	"United States of America" },
    { "URY",	"Uruguay" },
    { "UZB",	"Uzbekistan" },
    { "VUT",	"Vanuatu" },
    { "VEN",	"Venezuela" },
    { "VNM",	"Viet Nam" },
    { "VGB",	"British Virgin Islands" },
    { "VIR",	"U.S. Virgin Islands" },
    { "WLF",	"Wallis and Futuna" },
    { "ESH",	"Western Sahara" },
    { "YEM",	"Yemen" },
    { "ZMB",	"Zambia" },
    { "ZWE",	"Zimbabwe" }
};

static airport_t *apt_dat_lookup(airportdb_t *db, const char *ident);
static void apt_dat_insert(airportdb_t *db, airport_t *arpt);
static void free_airport(airport_t *arpt);

static bool_t load_airport(airport_t *arpt);
static void load_rwy_info(runway_t *rwy);

static arpt_index_t *create_arpt_index(airportdb_t *db, const airport_t *arpt);

static void
recreate_icao_iata_tables(airportdb_t *db, unsigned cap)
{
	ASSERT(db != NULL);

	htbl2_empty(&db->icao_index, sizeof (arpt_index_t), NULL, NULL);
	htbl2_destroy(&db->icao_index);
	htbl2_empty(&db->iata_index, sizeof (arpt_index_t), NULL, NULL);
	htbl2_destroy(&db->iata_index);

	htbl2_create(&db->icao_index, MAX(P2ROUNDUP(cap), 16),
	    AIRPORTDB_ICAO_LEN, sizeof (arpt_index_t), B_TRUE);
	htbl2_create(&db->iata_index, MAX(P2ROUNDUP(cap), 16),
	    AIRPORTDB_IATA_LEN, sizeof (arpt_index_t), B_TRUE);
}

/*
 * Given an arbitrary geographical position, returns the geo_table tile
 * coordinate which the input position corresponds to. If div_by_10 is
 * true, the coordinate is not in whole 1-degree resolution, but in 10-degree
 * resolution. This is used in the data cache to select the subdirectory.
 */
static geo_pos2_t
geo_pos2tile_pos(geo_pos2_t pos, bool_t div_by_10)
{
	if (div_by_10)
		return (GEO_POS2(floor(pos.lat / 10) * 10,
		    floor(pos.lon / 10) * 10));
	else
		return (GEO_POS2(floor(pos.lat), floor(pos.lon)));
}

/*
 * AVL tree comparator for airports based on their unique ident code.
 */
static int
airport_compar(const void *a, const void *b)
{
	const airport_t *aa = a, *ab = b;
	int res = strcmp(aa->ident, ab->ident);
	if (res < 0)
		return (-1);
	if (res > 0)
		return (1);
	return (0);
}

/*
 * AVL tree comparator for tile_t's based on latitude and longitude.
 */
static int
tile_compar(const void *a, const void *b)
{
	const tile_t *ta = a, *tb = b;

	if (ta->pos.lat < tb->pos.lat) {
		return (-1);
	} else if (ta->pos.lat == tb->pos.lat) {
		if (ta->pos.lon < tb->pos.lon)
			return (-1);
		else if (ta->pos.lon == tb->pos.lon)
			return (0);
		else
			return (1);
	} else {
		return (1);
	}
}

/*
 * AVL tree comparator for runway_t's based on the joint runway ID.
 */
static int
runway_compar(const void *a, const void *b)
{
	const runway_t *ra = a, *rb = b;
	int res = strcmp(ra->joint_id, rb->joint_id);
	/* check to match runway ID reversals */
	if (res != 0 && strcmp(ra->joint_id, rb->rev_joint_id) == 0)
		return (0);
	if (res < 0)
		return (-1);
	else if (res == 0)
		return (0);
	else
		return (1);
}

static int
ramp_start_compar(const void *a, const void *b)
{
	const ramp_start_t *rs_a = a, *rs_b = b;
	int res = strcmp(rs_a->name, rs_b->name);
	if (res < 0)
		return (-1);
	if (res > 0)
		return (1);
	return (0);
}

/*
 * Retrieves the geo table tile which contains position `pos'. If create is
 * B_TRUE, if the tile doesn't exit, it will be created.
 * Returns the table tile (if it exists) and a boolean (in created_p if
 * non-NULL) informing whether the table tile was created in this call
 * (if create == B_TRUE).
 */
static tile_t *
geo_table_get_tile(airportdb_t *db, geo_pos2_t pos, bool_t create,
    bool_t *created_p)
{
	pos.lat = floor(pos.lat);
	pos.lon = floor(pos.lon);

	bool_t created = B_FALSE;
	tile_t srch = { .pos = pos };
	tile_t *tile;
	avl_index_t where;

	ASSERT(db != NULL);
	ASSERT(!IS_NULL_GEO_POS(pos));

	tile = avl_find(&db->geo_table, &srch, &where);
	if (tile == NULL && create) {
		tile = safe_malloc(sizeof (*tile));
		tile->pos = pos;
		avl_create(&tile->arpts, airport_compar, sizeof (airport_t),
		    offsetof(airport_t, tile_node));
		avl_insert(&db->geo_table, tile, where);
		created = B_TRUE;
	}
	if (created_p != NULL)
		*created_p = created;

	return (tile);
}

/*
 * Given a runway threshold vector, direction vector, width, length and
 * threshold longitudinal displacement, prepares a bounding box which
 * encompasses that runway.
 */
static vect2_t *
make_rwy_bbox(vect2_t thresh_v, vect2_t dir_v, double width, double len,
    double long_displ)
{
	vect2_t *bbox;
	vect2_t len_displ_v;

	ASSERT(!IS_NULL_VECT(thresh_v));
	ASSERT(!IS_NULL_VECT(dir_v));
	ASSERT(!isnan(width));
	ASSERT(!isnan(len));
	ASSERT(!isnan(long_displ));

	bbox = safe_malloc(sizeof (*bbox) * 5);

	/*
	 * Displace the 'a' point from the runway threshold laterally
	 * by 1/2 width to the right.
	 */
	bbox[0] = vect2_add(thresh_v, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
	    width / 2));
	/* pull it back by `long_displ' */
	bbox[0] = vect2_add(bbox[0], vect2_set_abs(vect2_neg(dir_v),
	    long_displ));

	/* do the same for the `d' point, but displace to the left */
	bbox[3] = vect2_add(thresh_v, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
	    width / 2));
	/* pull it back by `long_displ' */
	bbox[3] = vect2_add(bbox[3], vect2_set_abs(vect2_neg(dir_v),
	    long_displ));

	/*
	 * points `b' and `c' are along the runway simply as runway len +
	 * long_displ
	 */
	len_displ_v = vect2_set_abs(dir_v, len + long_displ);
	bbox[1] = vect2_add(bbox[0], len_displ_v);
	bbox[2] = vect2_add(bbox[3], len_displ_v);

	bbox[4] = NULL_VECT2;

	return (bbox);
}

/*
 * Checks if the numerical runway type `t' is a hard-surface runway.
 */
static bool_t
rwy_is_hard(rwy_surf_t surf)
{
	/*
	 * We used to only check for a few surface types here, but XP12
	 * added a bunch more undocumented hard surface types, so in
	 * anticipation of further surface types being added, we'll instead
	 * explicitly check for the known soft surface types first.
	 */
	switch (surf) {
	case RWY_SURF_GRASS:
	case RWY_SURF_DIRT:
	case RWY_SURF_GRAVEL:
	case RWY_SURF_DRY_LAKEBED:
	case RWY_SURF_WATER:
	case RWY_SURF_SNOWICE:
		return (false);
	default:
		return (true);
	}
}

/*
 * Performs a lookup for an airport based on ICAO code in an airportdb_t.
 * The lookup is case-insensitive, because some data providers sometimes
 * provided ICAO identifiers in lowercase.
 */
static airport_t *
apt_dat_lookup(airportdb_t *db, const char *ident)
{
	airport_t search, *result;

	ASSERT(db != NULL);
	ASSERT(ident != NULL);

	lacf_strlcpy(search.ident, ident, sizeof (search.ident));
	strtoupper(search.ident);
	result = avl_find(&db->apt_dat, &search, NULL);
	if (result != NULL)
		load_airport(result);

	return (result);
}

static void
apt_dat_insert(airportdb_t *db, airport_t *arpt)
{
	avl_index_t where;
	ASSERT(db != NULL);
	ASSERT(arpt != NULL);
	VERIFY(avl_find(&db->apt_dat, arpt, &where) == NULL);
	avl_insert(&db->apt_dat, arpt, where);
}

/*
 * Links an airport into the geo-tile cache. The airport must not have been
 * geo-linked before. While an airport is geo-linked, its refpt must not be
 * modified.
 */
static void
geo_link_airport(airportdb_t *db, airport_t *arpt)
{
	tile_t *tile;
	avl_index_t where;

	ASSERT(db != NULL);
	ASSERT(arpt != NULL);

	tile = geo_table_get_tile(db, GEO3_TO_GEO2(arpt->refpt), B_TRUE, NULL);
	ASSERT(!arpt->geo_linked);
	VERIFY(avl_find(&tile->arpts, arpt, &where) == NULL);
	avl_insert(&tile->arpts, arpt, where);
	arpt->geo_linked = B_TRUE;
}

/*
 * Unlinks an airport from the geo-tile cache. The airport must have been
 * geo-linked before. After geo-unlinking, the airport's refpt may be modified.
 */
static void
geo_unlink_airport(airportdb_t *db, airport_t *arpt)
{
	tile_t *tile;

	ASSERT(arpt != NULL);
	ASSERT(arpt->geo_linked);
	tile = geo_table_get_tile(db, GEO3_TO_GEO2(arpt->refpt), B_TRUE, NULL);
	ASSERT(avl_find(&tile->arpts, arpt, NULL) == arpt);
	avl_remove(&tile->arpts, arpt);
	arpt->geo_linked = B_FALSE;
}

/*
 * Some airports appear in apt.dat files, but not in the Airports.txt, but
 * apt.dat doesn't tell us their airport reference point. Thus we do the
 * next best thing and auto-compute the lat/lon as the arithmetic mean of
 * the lat/lon of the first runway's thresholds.
 */
static void
airport_auto_refpt(airport_t *arpt)
{
	runway_t *rwy;
	geo_pos3_t p1, p2;

	ASSERT(arpt != NULL);

	rwy = avl_first(&arpt->rwys);
	ASSERT(isnan(arpt->refpt.lat) && isnan(arpt->refpt.lon));
	ASSERT(!arpt->load_complete);
	ASSERT(!arpt->geo_linked);
	ASSERT(!isnan(arpt->refpt.elev));
	ASSERT(rwy != NULL);

	p1 = rwy->ends[0].thr;
	p2 = rwy->ends[1].thr;
	/* Just to make sure there are no airports on the date line. */
	ASSERT(fabs(p1.lon - p2.lon) < 90);
	arpt->refpt.lat = (p1.lat + p2.lat) / 2;
	arpt->refpt.lon = (p1.lon + p2.lon) / 2;
	arpt->refpt_m.lat = arpt->refpt.lat;
	arpt->refpt_m.lon = arpt->refpt.lon;
	ASSERT(is_valid_lat(arpt->refpt.lat) && is_valid_lon(arpt->refpt.lon));
}

static char *
apt_dat_cache_dir(const airportdb_t *db, geo_pos2_t pos, const char *suffix)
{
	char lat_lon[16];

	ASSERT(db != NULL);
	ASSERT(!IS_NULL_GEO_POS(pos));

	pos = geo_pos2tile_pos(pos, B_TRUE);
	snprintf(lat_lon, sizeof (lat_lon), TILE_NAME_FMT, pos.lat, pos.lon);

	if (suffix != NULL)
		return (mkpathname(db->cachedir, lat_lon, suffix, NULL));
	else
		return (mkpathname(db->cachedir, lat_lon, NULL));
}

/*
 * Locates all apt.dat files used by X-Plane to display scenery. It consults
 * scenery_packs.ini to determine which scenery packs are currently enabled
 * and together with the default apt.dat returns them in a list sorted
 * numerically in preference order (lowest index for highest priority).
 * If the as_keys argument is true, the returned list is instead indexed
 * by the apt.dat file name and the values are the preference order of that
 * apt.dat (starting from 1 for highest priority and increasing with lowering
 * priority).
 * The apt.dat filenames are full filesystem paths.
 */
static void
find_all_apt_dats(const airportdb_t *db, list_t *list)
{
	char *fname;
	FILE *scenery_packs_ini;
	apt_dats_entry_t *e;

	ASSERT(db != NULL);
	ASSERT(list != NULL);

	fname = mkpathname(db->xpdir, "Custom Scenery", "scenery_packs.ini",
	    NULL);
	scenery_packs_ini = fopen(fname, "r");
	free(fname);
	fname = NULL;

	if (scenery_packs_ini != NULL) {
		char *line = NULL;
		size_t linecap = 0;

		while (!feof(scenery_packs_ini)) {
			char *scn_name;

			if (getline(&line, &linecap, scenery_packs_ini) <= 0)
				continue;
			strip_space(line);
			if (strstr(line, "SCENERY_PACK ") != line)
				continue;
			scn_name = &line[13];
			strip_space(scn_name);
			fix_pathsep(scn_name);
			e = safe_malloc(sizeof (*e));
			e->fname = mkpathname(db->xpdir, scn_name,
			    "Earth nav data", "apt.dat", NULL);
			list_insert_tail(list, e);
		}
		fclose(scenery_packs_ini);
		free(line);
	}
	e = safe_malloc(sizeof (*e));
	/* append the default apt.dat in XP11 */
	e->fname = mkpathname(db->xpdir, "Resources", "default scenery",
	    "default apt dat", "Earth nav data", "apt.dat", NULL);
	if (!file_exists(e->fname, NULL)) {
		lacf_free(e->fname);
		/* Try the default apt.dat in XP12 */
		e->fname = mkpathname(db->xpdir, "Global Scenery",
		    "Global Airports", "Earth nav data", "apt.dat", NULL);
	}
	list_insert_tail(list, e);
}

/*
 * This actually performs the final insertion of an airport into the database.
 * It inserts it into the flat apt_dat and into the geo_table.
 */
static void
read_apt_dat_insert(airportdb_t *db, airport_t *arpt)
{
	ASSERT(db != NULL);

	if (arpt == NULL)
		return;
	if (avl_numnodes(&arpt->rwys) != 0) {
		ASSERT(!isnan(arpt->refpt.lat) && !isnan(arpt->refpt.lon));
		apt_dat_insert(db, arpt);
		geo_link_airport(db, arpt);
	} else {
		free_airport(arpt);
	}
}

static void
normalize_name(iconv_t *cd_p, char *str_in, char *str_out, size_t cap)
{
	ASSERT(cd_p != NULL);
	ASSERT(str_in != NULL);
	ASSERT(str_out != NULL);

	unsigned l = strlen(str_in) + 1;
	char *str_conv = safe_calloc(l, sizeof (*str_conv));
	char *conv_in = str_in, *conv_out = str_conv;
	size_t conv_in_sz = strlen(str_in);
	size_t conv_out_sz = l;

	iconv(*cd_p, &conv_in, &conv_in_sz, &conv_out, &conv_out_sz);
	for (size_t i = 0, j = 0; str_conv[i] != '\0' && j + 1 < cap; i++) {
		if (str_conv[i] != '\'' && str_conv[i] != '`' &&
		    str_conv[i] != '^' && str_conv[i] != '\\' &&
		    str_conv[i] != '"') {
			str_out[j++] = str_conv[i];
		}
	}
	free(str_conv);
}

static char *
concat_comps(char **comps, size_t count)
{
	char *str = NULL;
	size_t cap = 0;

	ASSERT(comps != NULL);

	for (size_t i = 0; i < count; i++) {
		strip_space(comps[i]);
		append_format(&str, &cap, "%s%s",
		    comps[i], i + 1 < count ? " " : "");
	}
	return (str);
}

/*
 * Parses an airport line in apt.dat. The default apt.dat spec only supplies
 * the identifier and field elevation on this line. Our extended format which
 * we use in the data cache also adds the TA, TL and reference point LAT &
 * LON to this. If the apt.dat being parsed is a standard (non-extended) one,
 * the additional info is inferred later on from other sources during the
 * airport data cache creation process.
 */
static airport_t *
parse_apt_dat_1_line(airportdb_t *db, const char *line, iconv_t *cd_p,
    airport_t **dup_arpt_p)
{
	/*
	 * pre-allocate the buffer to be large enough that most names are
	 * already gonna fit in there without too much reallocation.
	 */
	char *name = NULL;
	const char *new_ident;
	geo_pos3_t pos = NULL_GEO_POS3;
	size_t ncomps;
	char **comps = strsplit(line, " ", B_TRUE, &ncomps);
	airport_t *arpt = NULL;

	ASSERT(db != NULL);
	ASSERT(line != NULL);

	if (dup_arpt_p != NULL)
		*dup_arpt_p = NULL;

	ASSERT(strcmp(comps[0], "1") == 0);
	if (ncomps < 5)
		goto out;

	new_ident = comps[4];
	pos.elev = atof(comps[1]);
	if (!is_valid_elev(pos.elev))
		/* Small GA fields might not have valid identifiers. */
		goto out;
	name = concat_comps(&comps[5], ncomps - 5);
	if (name == NULL)
		name = safe_strdup("");
	arpt = apt_dat_lookup(db, new_ident);
	if (arpt != NULL) {
		/*
		 * This airport was already known from a previously loaded
		 * apt.dat. Avoid overwriting its data.
		 */
		if (dup_arpt_p != NULL)
			*dup_arpt_p = arpt;
		arpt = NULL;
		goto out;
	}
	arpt = safe_calloc(1, sizeof (*arpt));
	avl_create(&arpt->rwys, runway_compar, sizeof (runway_t),
	    offsetof(runway_t, node));
	list_create(&arpt->freqs, sizeof (freq_info_t),
	    offsetof(freq_info_t, node));
	lacf_strlcpy(arpt->ident, new_ident, sizeof (arpt->ident));
	strtoupper(arpt->ident);
	/*
	 * Legacy scenery doesn't include '1302' metainfo lines with
	 * the ICAO code listed separately, so for those we just assume
	 * that the code listed in the ident here is the ICAO code.
	 */
	strlcpy(arpt->icao, arpt->ident, sizeof (arpt->icao));
	const char *cc = extract_icao_country_code(arpt->icao);
	if (cc != NULL) {
		lacf_strlcpy(arpt->cc, cc, sizeof (arpt->cc));
	} else {
		lacf_strlcpy(arpt->cc, "ZZ", sizeof (arpt->cc));
	}

	avl_create(&arpt->ramp_starts, ramp_start_compar,
	    sizeof (ramp_start_t), offsetof(ramp_start_t, node));

	/*
	 * Unfortunately, X-Plane's scenery authors put all kinds of
	 * weird chars into their airport names. So we employ libiconv
	 * to hopefully transliterate that junk away as much as possible.
	 */
	if (cd_p != NULL) {
		LACF_DESTROY(arpt->name_orig);
		arpt->name_orig = safe_strdup(name);
		normalize_name(cd_p, name, arpt->name, sizeof (arpt->name));
		strtoupper(arpt->name);
	} else {
		/*
		 * iconv is NOT used when reading our own apt.dat cache.
		 * So for those cases, we can just verbatim copy the airport
		 * name directly without charset issues.
		 */
		lacf_strlcpy(arpt->name, name, sizeof (arpt->name));
	}

	arpt->refpt = pos;
	arpt->refpt_m = GEO3_FT2M(pos);
out:
	free_strlist(comps, ncomps);
	free(name);
	return (arpt);
}

/*
 * This is the matching function that attempts to determine if a VGSI
 * (row code '21' in apt.dat) belongs to a specific runway. Returns the
 * lateral displacement (in meters) from the runway centerline if the
 * VGSI matches the runway or a huge number (1e10) otherwise.
 */
static double
runway_vgsi_fuzzy_match(runway_t *rwy, int end, vgsi_t type, vect2_t pos_v,
    double true_hdg)
{
	runway_end_t *re = &rwy->ends[end], *ore = &rwy->ends[!end];
	vect2_t thr2light_v = vect2_sub(pos_v, re->thr_v);
	vect2_t thr2thr_v = vect2_sub(ore->thr_v, re->thr_v);
	vect2_t thr2thr_uv = vect2_unit(thr2thr_v, NULL);
	vect2_t thr2thr_norm_uv = vect2_norm(thr2thr_uv, B_TRUE);
	double lat_displ = vect2_dotprod(thr2light_v, thr2thr_norm_uv),
	    lon_displ = vect2_dotprod(thr2light_v, thr2thr_uv);

	ASSERT(rwy != NULL);

	/*
	 * The checks we perform are:
	 * 1) the lateral displacement from the runway centerline must be
	 *	no more than 2x the runway width (VGSI_LAT_DISPL_FACT).
	 * 2) the longitudinal displacement must be sit between the thresholds
	 * 3) the true heading of the light fixture must be within 5 degrees
	 *	of true runway heading (VGSI_HDG_MATCH_THRESH).
	 * 4) if the VGSI is a left PAPI, it must be on the left
	 * 5) if the VGSI is a right PAPI, it must be on the right
	 */
	if (fabs(lat_displ) > VGSI_LAT_DISPL_FACT * rwy->width ||
	    lon_displ < 0 || lon_displ > rwy->length ||
	    fabs(rel_hdg(re->hdg, true_hdg)) > VGSI_HDG_MATCH_THRESH ||
	    (lat_displ > 0 && type == VGSI_PAPI_4L) ||
	    (lat_displ < 0 && type == VGSI_PAPI_4R))
		return (1e10);
	return (lat_displ);
}

static void
find_nearest_runway_to_vgsi(airport_t *arpt, vgsi_t type, vect2_t pos_v,
    double true_hdg, runway_t **rwy, runway_end_t **re, runway_end_t **ore)
{
	double max_displ = 100000;

	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);
	ASSERT(re != NULL);
	ASSERT(ore != NULL);
	/*
	 * Runway unknown. Let's try to do a more fuzzy search.
	 * We will look for the closest runway from which we are
	 * displaced no more than 2x the runway's width. We also
	 * check that the sense of the displacement is kept (left
	 * PAPI on the left side of the runway and vice versa).
	 */
	for (runway_t *crwy = avl_first(&arpt->rwys); crwy != NULL;
	    crwy = AVL_NEXT(&arpt->rwys, crwy)) {
		double displ;
		if ((displ = runway_vgsi_fuzzy_match(crwy, 0,
		    type, pos_v, true_hdg)) < max_displ) {
			*rwy = crwy;
			*re = &crwy->ends[0];
			*ore = &crwy->ends[1];
			max_displ = displ;
		} else if ((displ = runway_vgsi_fuzzy_match(crwy, 1,
		    type, pos_v, true_hdg)) < max_displ) {
			*rwy = crwy;
			*re = &crwy->ends[1];
			*ore = &crwy->ends[0];
			max_displ = displ;
		}
	}
}

/*
 * Row codes `21' denote lighting objects. We detect if the object is a
 * PAPI or VASI and use it to compute the GPA and TCH.
 */
static void
parse_apt_dat_21_line(airport_t *arpt, const char *line)
{
	char **comps;
	size_t ncomps;
	vgsi_t type;
	geo_pos2_t pos;
	double gpa, tch, displ, true_hdg;
	const char *rwy_id;
	runway_t *rwy = NULL;
	runway_end_t *re = NULL, *ore = NULL;
	vect2_t pos_v, thr2light_v, thr2thr_v;

	ASSERT(arpt != NULL);
	ASSERT(line != NULL);

	/* Construct the airport fpp to compute the thresholds */
	if (!load_airport(arpt))
		return;

	comps = strsplit(line, " ", B_TRUE, &ncomps);
	ASSERT(strcmp(comps[0], "21") == 0);
	if (ncomps < 7)
		/* No need to report, sometimes the rwy_ID is missing. */
		goto out;
	type = atoi(comps[3]);
	if (type < VGSI_VASI || type > VGSI_PAPI_3C || type == VGSI_PAPI_20DEG)
		goto out;
	pos = GEO_POS2(atof(comps[1]), atof(comps[2]));
	pos_v = geo2fpp(pos, &arpt->fpp);
	true_hdg = atof(comps[4]);
	if (!is_valid_hdg(true_hdg))
		goto out;
	gpa = atof(comps[5]);
	if (isnan(gpa) || gpa <= 0.0 || gpa > RWY_GPA_LIMIT)
		goto out;
	rwy_id = comps[6];

	/*
	 * Locate the associated runway. The VGSI line should denote which
	 * runway it belongs to.
	 */
	for (rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		if (strcmp(rwy->ends[0].id, rwy_id) == 0) {
			re = &rwy->ends[0];
			ore = &rwy->ends[1];
			break;
		} else if (strcmp(rwy->ends[1].id, rwy_id) == 0) {
			ore = &rwy->ends[0];
			re = &rwy->ends[1];
			break;
		}
	}
	if (rwy == NULL) {
		find_nearest_runway_to_vgsi(arpt, type, pos_v, true_hdg,
		    &rwy, &re, &ore);
		if (rwy == NULL)
			goto out;
	}
	/*
	 * We can compute the longitudinal displacement along the associated
	 * runway of the light from the runway threshold.
	 */
	thr2light_v = vect2_sub(pos_v, re->thr_v);
	thr2thr_v = vect2_sub(ore->thr_v, re->thr_v);
	displ = vect2_dotprod(thr2light_v, vect2_unit(thr2thr_v, NULL));
	/*
	 * Check that the VGSI sits somewhere between the two thresholds
	 * and that it's aligned properly. Some scenery is broken like that!
	 * This condition will only fail if we didn't use the matching in
	 * find_nearest_runway_to_vgsi, because that function already
	 * perform these checks.
	 */
	if (displ < 0 || displ > rwy->length ||
	    fabs(rel_hdg(true_hdg, re->hdg)) > VGSI_HDG_MATCH_THRESH) {
		rwy = NULL;
		re = NULL;
		ore = NULL;
		/* Fallback check - try to match it to ANY runway */
		find_nearest_runway_to_vgsi(arpt, type, pos_v, true_hdg,
		    &rwy, &re, &ore);
		if (rwy == NULL)
			goto out;
		thr2light_v = vect2_sub(pos_v, re->thr_v);
		thr2thr_v = vect2_sub(ore->thr_v, re->thr_v);
		displ = vect2_dotprod(thr2light_v, vect2_unit(thr2thr_v,
		    NULL));
	}
	/* Finally, given the displacement and GPA, compute the TCH. */
	tch = MET2FEET(sin(DEG2RAD(gpa)) * displ);
	ASSERT(tch >= 0.0);
	if (TCH_IS_VALID(tch)) {
		re->gpa = gpa;
		re->tch = tch;
	}
out:
	free_strlist(comps, ncomps);
}

/*
 * Validates the data parsed from an apt.dat for a runway end:
 * 1) it has a valid runway identifier
 * 2) it has a valid threshold lat x lon
 * 3) its latitude is within our latitude limits
 * 4) it has a valid elevation (or no known elevation)
 * 5) it has a non-negative threshold displacement value
 * 6) it has a non-negative blastpath length value
 * 7) it has a valid (or zero) glidepath angle
 * 8) it has a valid (or zero) threshold clearing height
 */
static bool_t
validate_rwy_end(const runway_end_t *re, char error_descr[128])
{
	ASSERT(re != NULL);
	ASSERT(error_descr != NULL);
#define	VALIDATE(cond, ...) \
	do { \
		if (!(cond)) { \
			snprintf(error_descr, 128, __VA_ARGS__); \
			return (B_FALSE); \
		} \
	} while (0)
	VALIDATE(is_valid_rwy_ID(re->id), "Runway ID \"%s\" invalid", re->id);
	VALIDATE(is_valid_lat(re->thr.lat), "Latitude \"%g\" is invalid",
	    re->thr.lat);
	VALIDATE(is_valid_lon(re->thr.lon), "Longitude \"%g\" is invalid",
	    re->thr.lon);
	VALIDATE(isnan(re->thr.elev) || is_valid_elev(re->thr.elev),
	    "Threshold elevation \"%g\" is invalid", re->thr.elev);
	VALIDATE(re->displ >= 0.0, "Displacement \"%g\" is invalid", re->displ);
	VALIDATE(re->blast >= 0.0, "Blastpad \"%g\" is invalid", re->displ);
	VALIDATE(re->gpa >= 0.0 && re->gpa < RWY_GPA_LIMIT,
	    "GPA \"%g\" is invalid", re->gpa);
	VALIDATE(re->tch >= 0.0 && re->tch < RWY_TCH_LIMIT,
	    "TCH \"%g\" is invalid", re->tch);
#undef	VALIDATE
	return (B_TRUE);
}

static void
parse_apt_dat_freq_line(airport_t *arpt, char *line, bool_t use833)
{
	char **comps;
	size_t ncomps;
	freq_info_t *freq;

	ASSERT(arpt != NULL);
	ASSERT(line != NULL);
	/*
	 * Remove spurious underscores and dashes that some sceneries insist
	 * on using in frequency names. Do this before the strsplit pass, so
	 * we subdivide at those boundaries.
	 */
	for (size_t i = 0, n = strlen(line); i < n; i++) {
		if (line[i] == '_' || line[i] == '-')
			line[i] = ' ';
	}

	comps = strsplit(line, " ", B_TRUE, &ncomps);
	if (ncomps < 3)
		goto out;
	freq = calloc(1, sizeof (*freq));
	/*
	 * When `use833' is provided, the line types start at 1050 instead
	 * of 50. Also, the frequencies are specified in thousands of Hertz,
	 * not tens of thousands.
	 */
	freq->type = atoi(comps[0]) - (use833 ? 1050 : 50);
	freq->freq = atoll(comps[1]) * (use833 ? 1000 : 10000);
	for (size_t i = 2; i < ncomps; i++) {
		strtoupper(comps[i]);
		/*
		 * Some poorly written apt.dats include the airport identifier
		 * in the frequency name (e.g. "LZIB ATIS" for the ATIS
		 * frequency at airport LZIB). This is redundant and just
		 * wastes space, so remove that. And some even more stupidly
		 * contain the word "frequency" - DOH!
		 */
		if ((strcmp(comps[i], arpt->icao) == 0 ||
		    strcmp(comps[i], "FREQUENCY") == 0) && ncomps > 3) {
			continue;
		}
		if (freq->name[0] != '\0') {
			strncat(&freq->name[strlen(freq->name)], " ",
			    sizeof (freq->name) - strlen(freq->name));
		}
		strncat(&freq->name[strlen(freq->name)], comps[i],
		    sizeof (freq->name) - strlen(freq->name));
	}
	list_insert_tail(&arpt->freqs, freq);

out:
	free_strlist(comps, ncomps);
}

/*
 * Parses an apt.dat runway line. Standard apt.dat runway lines simply
 * denote the runway's surface type, width (in meters) the position of
 * each threshold (lateral only, no elevation) and displacement parameters.
 * Our data cache features three additional special fields: GPA, TCH and
 * elevation (in meters) of each end. When parsing a stock apt.dat, these
 * extra parameters are inferred from other sources in the data cache
 * creation process.
 */
static void
parse_apt_dat_100_line(airport_t *arpt, const char *line, bool_t hard_surf_only)
{
	char **comps;
	size_t ncomps;
	runway_t *rwy;
	avl_index_t where;
	char error_descr[128];

	ASSERT(arpt != NULL);
	ASSERT(line != NULL);

	comps = strsplit(line, " ", B_TRUE, &ncomps);
	ASSERT(strcmp(comps[0], "100") == 0);
	if (ncomps < 8 + 9 + 5 ||
	    (hard_surf_only && !rwy_is_hard(atoi(comps[2]))))
		goto out;

	rwy = safe_calloc(1, sizeof (*rwy));

	rwy->arpt = arpt;
	rwy->width = atof(comps[1]);
	rwy->surf = atoi(comps[2]);

	copy_rwy_ID(comps[8 + 0], rwy->ends[0].id);
	rwy->ends[0].thr = GEO_POS3(atof(comps[8 + 1]), atof(comps[8 + 2]),
	    arpt->refpt.elev);
	rwy->ends[0].thr_m = GEO3_FT2M(rwy->ends[0].thr);
	rwy->ends[0].displ = atof(comps[8 + 3]);
	rwy->ends[0].blast = atof(comps[8 + 4]);

	copy_rwy_ID(comps[8 + 9 + 0], rwy->ends[1].id);
	rwy->ends[1].thr = GEO_POS3(atof(comps[8 + 9 + 1]),
	    atof(comps[8 + 9 + 2]), arpt->refpt.elev);
	rwy->ends[1].thr_m = GEO3_FT2M(rwy->ends[1].thr);
	rwy->ends[1].displ = atof(comps[8 + 9 + 3]);
	rwy->ends[1].blast = atof(comps[8 + 9 + 4]);

	/*
	 * ARINC 424 says in field reference 5.67 that if no explicit TCH is
	 * specified, 50 feet shall be assumed. The GPA cannot be assumed
	 * this easily and unfortunately field 5.226 from ARINC 424 isn't in
	 * X-Plane 11's navdata, so we instead parse it in a later step from
	 * instrument approach procedures (X-Plane 11) or from an Airports.txt
	 * (X-Plane 10), falling back to VGSI triangulation in the scenery if
	 * those methods fail. We won't provide vertical approach monitoring
	 * unless both GPA & TCH are non-zero.
	 */
	rwy->ends[0].tch = 50;
	rwy->ends[1].tch = 50;

	snprintf(rwy->joint_id, sizeof (rwy->joint_id), "%s%s",
	    rwy->ends[0].id, rwy->ends[1].id);
	snprintf(rwy->rev_joint_id, sizeof (rwy->rev_joint_id), "%s%s",
	    rwy->ends[1].id, rwy->ends[0].id);

	/* Our extended data cache format */
	if (ncomps >= 28 && strstr(comps[22], "GPA1:") == comps[22] &&
	    strstr(comps[23], "GPA2:") == comps[23] &&
	    strstr(comps[24], "TCH1:") == comps[24] &&
	    strstr(comps[25], "TCH2:") == comps[25] &&
	    strstr(comps[26], "TELEV1:") == comps[26] &&
	    strstr(comps[27], "TELEV2:") == comps[27]) {
		rwy->ends[0].gpa = atof(&comps[22][5]);
		rwy->ends[1].gpa = atof(&comps[23][5]);
		rwy->ends[0].tch = atof(&comps[24][5]);
		rwy->ends[1].tch = atof(&comps[25][5]);
		rwy->ends[0].thr.elev = atof(&comps[26][7]);
		rwy->ends[1].thr.elev = atof(&comps[27][7]);
		rwy->ends[0].thr_m.elev = FEET2MET(rwy->ends[0].thr.elev);
		rwy->ends[1].thr_m.elev = FEET2MET(rwy->ends[1].thr.elev);
	}

	/* Validate the runway ends individually. */
	if (!validate_rwy_end(&rwy->ends[0], error_descr) ||
	    !validate_rwy_end(&rwy->ends[1], error_descr)) {
		free(rwy);
		goto out;
	}
	/*
	 * Are the runway ends sufficiently far apart? Protects against runways
	 * with overlapping thresholds, which results in a NAN runway hdg.
	 */
	if (vect3_dist(geo2ecef_ft(rwy->ends[0].thr, &wgs84),
	    geo2ecef_ft(rwy->ends[1].thr, &wgs84)) < MIN_RWY_LEN) {
		free(rwy);
		goto out;
	}
	/* Duplicate runway present? */
	if (avl_find(&arpt->rwys, rwy, &where) != NULL) {
		free(rwy);
		goto out;
	}
	avl_insert(&arpt->rwys, rwy, where);
	if (arpt->load_complete) {
		/* do a supplemental runway info load */
		load_rwy_info(rwy);
	} else if (isnan(arpt->refpt.lat) || isnan(arpt->refpt.lon)) {
		arpt->refpt.lat = NAN;
		arpt->refpt.lon = NAN;
		arpt->refpt_m.lat = NAN;
		arpt->refpt_m.lon = NAN;
		airport_auto_refpt(arpt);
	}
out:
	free_strlist(comps, ncomps);
}

static bool_t
is_normal_gate_name(const char *str)
{
	ASSERT(str != NULL);
	for (size_t i = 0, n = strlen(str); i < n; i++) {
		if ((str[i] < 'A' || str[i] > 'Z') &&
		    (str[i] < '0' || str[i] > '9')) {
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

static void
parse_apt_dat_1300_line(airport_t *arpt, const char *line,
    bool_t normalize_name)
{
	char **comps;
	size_t n_comps;
	ramp_start_t srch = {};
	ramp_start_t *rs = NULL;
	avl_index_t where;

	ASSERT(arpt != NULL);
	ASSERT(line != NULL);

	comps = strsplit(line, " ", B_TRUE, &n_comps);
	if (n_comps < 7)
		goto out;
	if (!normalize_name) {
		unsigned l = 0;
		for (size_t i = 6; i < n_comps; i++) {
			lacf_strlcpy(&srch.name[l], comps[i],
			    sizeof (srch.name) - l);
			l = MIN(l + strlen(comps[i]), sizeof (srch.name) - 1);
			if (i + 1 < n_comps) {
				lacf_strlcpy(&srch.name[l], " ",
				    sizeof (srch.name) - l);
				l = MIN(l + 1, sizeof (srch.name) - 1);
			}
		}
	} else {
		for (size_t i = 6; i < n_comps; i++) {
			if (is_normal_gate_name(comps[i])) {
				strlcpy(srch.name, comps[i],
				    sizeof (srch.name));
				break;
			}
		}
		if (srch.name[0] == '\0')
			goto out;
	}
	rs = avl_find(&arpt->ramp_starts, &srch, &where);
	if (rs != NULL)
		goto out;
	rs = safe_calloc(1, sizeof (*rs));
	lacf_strlcpy(rs->name, srch.name, sizeof (rs->name));
	rs->pos = GEO_POS2(atof(comps[1]), atof(comps[2]));
	rs->hdgt = atof(comps[3]);
	if (!is_valid_lat(rs->pos.lat) || !is_valid_lon(rs->pos.lon) ||
	    !is_valid_hdg(rs->hdgt)) {
		free(rs);
		goto out;
	}
	if (strcmp(comps[4], "gate") == 0)
		rs->type = RAMP_START_GATE;
	else if (strcmp(comps[4], "hangar") == 0)
		rs->type = RAMP_START_HANGAR;
	else if (strcmp(comps[4], "tie-down") == 0)
		rs->type = RAMP_START_TIEDOWN;
	else
		rs->type = RAMP_START_MISC;

	avl_insert(&arpt->ramp_starts, rs, where);
out:
	free_strlist(comps, n_comps);
}

static opt_float
extract_TA_TL_ft(char *const*comps, size_t n_comps)
{
	ASSERT(comps != NULL);
	ASSERT3U(n_comps, >=, 3);
	/*
	 * This field comes extremely mangled in the apt.dat data.
	 * It seems WED performs no input validation on this, so users
	 * are allowed to throw just any old junk in there.
	 */
	ASSERT(strlen(comps[2]) != 0);
	// sometimes there's a 'm' suffix as a separate word
	bool metric = false;
	if (n_comps >= 4) {
		ASSERT(strlen(comps[3]) != 0);
		metric = (tolower(comps[3][0]) == 'm');
	// sometimes the units are appended to the end of the number
	} else if (tolower(comps[2][strlen(comps[2]) - 1]) == 'm') {
		metric = true;
	}
	char *buf = NULL;
	bool is_fl = false;
	// sometimes they write 'FL 123', so read the next field
	if (lacf_strcasecmp(comps[2], "FL") == 0 && n_comps > 3) {
		buf = safe_strdup(comps[3]);
		is_fl = true;
	} else {
		buf = safe_strdup(comps[2]);
	}
	// strip a leading 'FL'
	if (lacf_strcasestr(buf, "FL") == buf) {
		memmove(buf, &buf[2], strlen(&buf[2]) + 1);
		is_fl = true;
	}
	// go through the buffer and drop any piece of non-numeric junk
	// in there, to hopefully reconstitute something parseable
	for (unsigned i = 0, n = strlen(buf); i < n; i++) {
		if (!isdigit(buf[i])) {
			memmove(&buf[i], &buf[i + 1], strlen(&buf[i + 1]) + 1);
		}
	}
	float alt = 0;
	if (sscanf(buf, "%f", &alt) == 1) {
		if (is_fl || (alt < 600 && !metric)) {
			alt *= 100;
		}
		// sometimes they write '21700 m' when they meant 21700 ft
		// in a metric country (China), so ignore the 'm' tag there
		if (metric && MET2FEET(alt) < 60000) {
			alt = MET2FEET(alt);
		}
		return (SOME(alt));
	} else {
		free(buf);
		return (NONE(float));
	}
}

/*
 * Often times payware and custom airports lack a lot of the meta info
 * that stock X-Plane airports contain. Normally we want to skip re-parsing
 * stock airports in the presence of a custom one, however, we do want the
 * extra meta info out of the stock dataset. To that end, if we hit a
 * duplicate in the stock dataset, we try to use it fill in any precending
 * custom airport.
 */
static void
fill_dup_arpt_info(airport_t *arpt, const char *line, int row_code)
{
	ASSERT(arpt != NULL);
	ASSERT(line != NULL);

	if (row_code == 1302) {
		size_t ncomps;
		char **comps = strsplit(line, " ", B_TRUE, &ncomps);

		if (ncomps < 2) {
			free_strlist(comps, ncomps);
			return;
		}

		if (strcmp(comps[1], "iata_code") == 0 && ncomps >= 3 &&
		    is_valid_iata_code(comps[2]) &&
		    !is_valid_iata_code(arpt->iata)) {
			lacf_strlcpy(arpt->iata, comps[2], sizeof (arpt->iata));
		} else if (strcmp(comps[1], "transition_alt") == 0 &&
		    ncomps >= 3 && arpt->TA == 0) {
			IF_LET(float, TA_ft, extract_TA_TL_ft(comps, ncomps))
				arpt->TA = TA_ft;
				arpt->TA_m = FEET2MET(TA_ft);
			IF_LET_END
		} else if (strcmp(comps[1], "transition_level") == 0 &&
		    ncomps >= 3 && arpt->TL == 0) {
			IF_LET(float, TL_ft, extract_TA_TL_ft(comps, ncomps))
				arpt->TL = TL_ft;
				arpt->TL_m = FEET2MET(TL_ft);
			IF_LET_END
		} else if (strcmp(comps[1], "region_code") == 0 &&
		    ncomps >= 3 && strcmp(comps[2], "-") != 0) {
			lacf_strlcpy(arpt->cc, comps[2], sizeof (arpt->cc));
		} else if (strcmp(comps[1], "country") == 0 &&
		    ncomps >= 3 && strcmp(comps[2], "-") != 0) {
			LACF_DESTROY(arpt->country);
			arpt->country = concat_comps(&comps[2], ncomps - 2);
		} else if (strcmp(comps[1], "city") == 0 &&
		    ncomps >= 3 && strcmp(comps[2], "-") != 0) {
			LACF_DESTROY(arpt->city);
			arpt->city = concat_comps(&comps[2], ncomps - 2);
		}
		free_strlist(comps, ncomps);
	}
}

static char *
iso3166_cc3_to_name(const char *cc3)
{
	ASSERT(cc3 != NULL);

	for (size_t i = 0; i < ARRAY_NUM_ELEM(iso3166_codes); i++) {
		if (strcmp(cc3, iso3166_codes[i].code) == 0)
			return (safe_strdup(iso3166_codes[i].name));
	}
	return (NULL);
}

static void
parse_attr_country(char **comps, size_t n_comps, int version, airport_t *arpt)
{
	ASSERT(comps != NULL);
	ASSERT(arpt != NULL);

	LACF_DESTROY(arpt->country);
	arpt->cc3[0] = '\0';

	if (n_comps == 0)
		return;
	if (version < 1200) {
		arpt->country = concat_comps(comps, n_comps);
	} else {
		if (strlen(comps[0]) == 3 && isupper(comps[0][0]) &&
		    isupper(comps[0][1]) && isupper(comps[0][2])) {
			arpt->country = iso3166_cc3_to_name(comps[0]);
		}
		if (arpt->country == NULL)
			arpt->country = concat_comps(comps, n_comps);
	}
}

/*
 * Parses an apt.dat (either from regular scenery or from CACHE_DIR) to
 * cache the airports contained in it.
 */
static void
read_apt_dat(airportdb_t *db, const char *apt_dat_fname, bool_t fail_ok,
    iconv_t *cd_p, bool_t fill_in_dups)
{
	FILE *apt_dat_f;
	airport_t *arpt = NULL, *dup_arpt = NULL;
	char *line = NULL;
	size_t linecap = 0;
	int line_num = 0, version = 0;
	char **comps;
	size_t ncomps;

	ASSERT(db != NULL);
	ASSERT(apt_dat_fname != NULL);

	apt_dat_f = fopen(apt_dat_fname, "r");
	if (apt_dat_f == NULL) {
		if (!fail_ok)
			logMsg("Can't open %s: %s", apt_dat_fname,
			    strerror(errno));
		return;
	}

	while (!feof(apt_dat_f)) {
		int row_code;

		line_num++;
		if (getline(&line, &linecap, apt_dat_f) <= 0)
			continue;
		strip_space(line);

		if (sscanf(line, "%d", &row_code) != 1)
			continue;
		/* Read the version header */
		if (line_num == 2) {
			version = row_code;
			continue;
		}
		/*
		 * Finish the current airport on an empty line or a new
		 * airport line.
		 */
		if (strlen(line) == 0 || row_code == 1 || row_code == 16 ||
		    row_code == 17) {
			if (arpt != NULL)
				read_apt_dat_insert(db, arpt);
			arpt = NULL;
			dup_arpt = NULL;
		}
		if (row_code == 1) {
			arpt = parse_apt_dat_1_line(db, line, cd_p,
			    fill_in_dups ? &dup_arpt : NULL);
		}
		if (arpt == NULL) {
			if (dup_arpt != NULL)
				fill_dup_arpt_info(dup_arpt, line, row_code);
			continue;
		}

		switch (row_code) {
		case 21:
			parse_apt_dat_21_line(arpt, line);
			break;
		case 50 ... 56:
			parse_apt_dat_freq_line(arpt, line, B_FALSE);
			break;
		case 100:
			parse_apt_dat_100_line(arpt, line, db->ifr_only);
			break;
		case 1050 ... 1056:
			parse_apt_dat_freq_line(arpt, line, B_TRUE);
			break;
		case 1300:
			parse_apt_dat_1300_line(arpt, line,
			    db->normalize_gate_names);
			break;
		case 1302:
			comps = strsplit(line, " ", B_TRUE, &ncomps);
			/*
			 * '1302' lines are meta-info lines introduced since
			 * X-Plane 11. This line can contain varying numbers
			 * of components, but we only care when it's 3.
			 */
			if (ncomps < 3) {
				free_strlist(comps, ncomps);
				continue;
			}
			/* Necessary check prior to modifying the refpt. */
			ASSERT(!arpt->geo_linked);
			/*
			 * X-Plane 11 introduced these to remove the need
			 * for an Airports.txt.
			 */
			if (strcmp(comps[1], "icao_code") == 0 &&
			    is_valid_icao_code(comps[2])) {
				lacf_strlcpy(arpt->icao, comps[2],
				    sizeof (arpt->icao));
			} else if (strcmp(comps[1], "iata_code") == 0 &&
			    is_valid_iata_code(comps[2])) {
				lacf_strlcpy(arpt->iata, comps[2],
				    sizeof (arpt->iata));
			} else if (strcmp(comps[1], "country") == 0) {
				parse_attr_country(&comps[2], ncomps - 2,
				    version, arpt);
			} else if (strcmp(comps[1], "city") == 0) {
				LACF_DESTROY(arpt->city);
				arpt->city = concat_comps(&comps[2],
				    ncomps - 2);
			} else if (strcmp(comps[1], "name_orig") == 0) {
				LACF_DESTROY(arpt->name_orig);
				arpt->name_orig = concat_comps(&comps[2],
				    ncomps - 2);
			} else if (strcmp(comps[1], "transition_alt") == 0) {
				IF_LET(float, TA_ft, extract_TA_TL_ft(comps,
				    ncomps))
					arpt->TA = TA_ft;
					arpt->TA_m = FEET2MET(TA_ft);
				IF_LET_END
			} else if (strcmp(comps[1], "transition_level") == 0) {
				IF_LET(float, TL_ft, extract_TA_TL_ft(comps,
				    ncomps))
					arpt->TL = TL_ft;
					arpt->TL_m = FEET2MET(TL_ft);
				IF_LET_END
			} else if (strcmp(comps[1], "datum_lat") == 0) {
				double lat = atof(comps[2]);
				if (is_valid_lat(lat)) {
					arpt->refpt.lat = lat;
					arpt->refpt_m.lat = lat;
				} else {
					free_airport(arpt);
					arpt = NULL;
				}
			} else if (strcmp(comps[1], "datum_lon") == 0) {
				double lon = atof(comps[2]);
				if (is_valid_lon(lon)) {
					arpt->refpt.lon = lon;
					arpt->refpt_m.lon = lon;
				}
			} else if (strcmp(comps[1], "region_code") == 0 &&
			    strcmp(comps[1], "-") != 0) {
				lacf_strlcpy(arpt->cc, comps[2],
				    sizeof (arpt->cc));
			}

			free_strlist(comps, ncomps);
			break;
		}
	}

	if (arpt != NULL)
		read_apt_dat_insert(db, arpt);

	free(line);
	fclose(apt_dat_f);
}

static bool_t
write_apt_dat(const airportdb_t *db, const airport_t *arpt)
{
	char lat_lon[16];
	char *fname;
	FILE *fp;
	geo_pos2_t p;
	bool_t exists;

	ASSERT(db != NULL);
	ASSERT(arpt != NULL);

	p = geo_pos2tile_pos(GEO3_TO_GEO2(arpt->refpt), B_FALSE);
	snprintf(lat_lon, sizeof (lat_lon), TILE_NAME_FMT, p.lat, p.lon);
	fname = apt_dat_cache_dir(db, GEO3_TO_GEO2(arpt->refpt), lat_lon);

	exists = file_exists(fname, NULL);
	fp = fopen(fname, "a");
	if (fp == NULL) {
		logMsg("Error writing file %s: %s", fname, strerror(errno));
		return (B_FALSE);
	}
	if (!exists) {
		fprintf(fp, "I\n"
		    "1200 libacfutils airportdb version %d\n"
		    "\n", ARPTDB_CACHE_VERSION);
	}
	ASSERT(!IS_NULL_GEO_POS(arpt->refpt));

	fprintf(fp, "1 %.0f 0 0 %s %s\n"
	    "1302 datum_lat %f\n"
	    "1302 datum_lon %f\n",
	    arpt->refpt.elev, arpt->ident, arpt->name, arpt->refpt.lat,
	    arpt->refpt.lon);
	if (arpt->name_orig != NULL)
		fprintf(fp, "1302 name_orig %s\n", arpt->name_orig);
	if (arpt->icao[0] != '\0')
		fprintf(fp, "1302 icao_code %s\n", arpt->icao);
	if (arpt->iata[0] != '\0')
		fprintf(fp, "1302 iata_code %s\n", arpt->iata);
	if (arpt->country != NULL)
		fprintf(fp, "1302 country %s\n", arpt->country);
	if (arpt->city != NULL)
		fprintf(fp, "1302 city %s\n", arpt->city);
	if (arpt->TA != 0)
		fprintf(fp, "1302 transition_alt %.0f\n", arpt->TA);
	if (arpt->TL != 0)
		fprintf(fp, "1302 transition_level %.0f\n", arpt->TL);
	if (*arpt->cc != 0)
		fprintf(fp, "1302 region_code %s\n", arpt->cc);
	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		ASSERT(!isnan(rwy->ends[0].gpa));
		ASSERT(!isnan(rwy->ends[1].gpa));
		ASSERT(!isnan(rwy->ends[0].tch));
		ASSERT(!isnan(rwy->ends[1].tch));
		ASSERT(!isnan(rwy->ends[0].thr.elev));
		ASSERT(!isnan(rwy->ends[1].thr.elev));
		fprintf(fp, "100 %.2f %d 0 0 0 0 0 "
		    "%s %f %f %.1f %.1f 0 0 0 0 "
		    "%s %f %f %.1f %.1f "
		    "GPA1:%.02f GPA2:%.02f TCH1:%.0f TCH2:%.0f "
		    "TELEV1:%.0f TELEV2:%.0f\n",
		    rwy->width, rwy->surf,
		    rwy->ends[0].id, rwy->ends[0].thr.lat,
		    rwy->ends[0].thr.lon, rwy->ends[0].displ,
		    rwy->ends[0].blast,
		    rwy->ends[1].id, rwy->ends[1].thr.lat,
		    rwy->ends[1].thr.lon, rwy->ends[1].displ,
		    rwy->ends[1].blast,
		    rwy->ends[0].gpa, rwy->ends[1].gpa,
		    rwy->ends[0].tch, rwy->ends[1].tch,
		    rwy->ends[0].thr.elev, rwy->ends[1].thr.elev);
	}
	for (const ramp_start_t *rs = avl_first(&arpt->ramp_starts);
	    rs != NULL; rs = AVL_NEXT(&arpt->ramp_starts, rs)) {
		static const char *type2name[] = {
		    [RAMP_START_GATE] = "gate",
		    [RAMP_START_HANGAR] = "hangar",
		    [RAMP_START_TIEDOWN] = "tie-down",
		    [RAMP_START_MISC] = "misc",
		};
		fprintf(fp, "1300 %f %f %.2f %s all %s\n",
		    rs->pos.lat, rs->pos.lon, rs->hdgt,
		    type2name[rs->type], rs->name);
	}
	for (const freq_info_t *freq = list_head(&arpt->freqs); freq != NULL;
	    freq = list_next(&arpt->freqs, freq)) {
		/*
		 * We always emit the frequency info using the new
		 * 8.33kHz-aware row code format.
		 */
		fprintf(fp, "%d %ld %s\n", freq->type + 1050,
		    (unsigned long)floor(freq->freq / 1000), freq->name);
	}
	fprintf(fp, "\n");
	fclose(fp);
	free(fname);

	return (B_TRUE);
}

static bool_t
load_arinc424_arpt_data(const char *filename, airport_t *arpt)
{
	char *line = NULL;
	size_t linecap = 0;
	FILE *fp;
	int line_num = 0;

	ASSERT(filename != NULL);
	ASSERT(arpt != NULL);

	/* airport already seen in previous version of the database, skip */
	if (arpt->in_navdb)
		return (B_TRUE);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		logMsg("Can't open %s: %s", filename, strerror(errno));
		return (B_FALSE);
	}

	arpt->in_navdb = B_TRUE;

	while (!feof(fp)) {
		line_num++;
		if (getline(&line, &linecap, fp) <= 0)
			continue;
		if (strstr(line, "APPCH:") == line) {
			/*
			 * Extract the runway TCH and GPA from instrument
			 * approach lines.
			 */
			char **comps;
			char rwy_id[4];
			size_t ncomps;
			runway_t *rwy;
			float gpa;

			arpt->have_iaps = B_TRUE;

			comps = strsplit(line + 6, ",", B_FALSE, &ncomps);
			if (strstr(comps[4], "RW") != comps[4] ||
			    sscanf(comps[28], "%f", &gpa) != 1 ||
			    gpa >= 0 || gpa < RWY_GPA_LIMIT * -100)
				goto out_appch;
			copy_rwy_ID(comps[4] + 2, rwy_id);
			/*
			 * The database has this in 0.01 deg steps, stored
			 * negative (i.e. "3.5 degrees" is "-350" in the DB).
			 */
			gpa /= -100.0;
			for (rwy = avl_first(&arpt->rwys); rwy != NULL;
			    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
				runway_end_t *re;

				if (strcmp(rwy->ends[0].id, rwy_id) == 0)
					re = &rwy->ends[0];
				else if (strcmp(rwy->ends[1].id, rwy_id) == 0)
					re = &rwy->ends[1];
				else
					continue;
				/*
				 * Ovewrite pre-existing data, which may have
				 * come from VGSI auto-computation. This data
				 * should be more reliable & accurate.
				 */
				re->gpa = gpa;
				break;
			}
out_appch:
			free_strlist(comps, ncomps);
		} else if (strstr(line, "RWY:") == line) {
			/*
			 * Extract runway threshold elevation from runway
			 * lines.
			 */
			char **comps;
			char rwy_id[4];
			size_t ncomps;
			runway_t *rwy;

			comps = strsplit(line + 4, ",", B_FALSE, &ncomps);
			if (ncomps != 8)
				goto out_rwy;
			for (size_t i = 0; i < ncomps; i++)
				strip_space(comps[i]);
			if (strstr(comps[0], "RW") != comps[0])
				goto out_rwy;
			copy_rwy_ID(comps[0] + 2, rwy_id);
			for (rwy = avl_first(&arpt->rwys); rwy != NULL;
			    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
				runway_end_t *re;
				int telev, tch;

				if (strcmp(rwy->ends[0].id, rwy_id) == 0)
					re = &rwy->ends[0];
				else if (strcmp(rwy->ends[1].id, rwy_id) == 0)
					re = &rwy->ends[1];
				else
					continue;
				if (sscanf(comps[3], "%d", &telev) == 1 &&
				    is_valid_elev(telev))
					re->thr.elev = telev;
				if (sscanf(comps[7], "%d", &tch) == 1 &&
				    tch > 0 && tch < RWY_TCH_LIMIT)
					re->tch = tch;
				break;
			}
out_rwy:
			free_strlist(comps, ncomps);
		}
	}

	fclose(fp);

	free(line);
	return (B_TRUE);
}

static bool_t
load_CIFP_file(airportdb_t *db, const char *dirpath, const char *filename)
{
	airport_t *arpt;
	char *filepath;
	char ident[8];
	bool_t res;

	ASSERT(db != NULL);
	ASSERT(dirpath != NULL);
	ASSERT(filename != NULL);

	/* the filename must end in ".dat" */
	if (strlen(filename) < 4 ||
	    strcmp(&filename[strlen(filename) - 4], ".dat") != 0) {
		return (B_FALSE);
	}
	lacf_strlcpy(ident, filename, sizeof (ident));
	ident[strlen(filename) - 4] = '\0';
	arpt = apt_dat_lookup(db, ident);
	if (arpt == NULL)
		return (B_FALSE);
	filepath = mkpathname(dirpath, filename, NULL);
	res = load_arinc424_arpt_data(filepath, arpt);
	free(filepath);

	return (res);
}

/*
 * Loads all ARINC424-formatted procedures files from a CIFP directory
 * in the new X-Plane 11 navdata. This has to be OS-specific, because
 * directory enumeration isn't portable.
 */
#if	IBM

static bool_t
load_CIFP_dir(airportdb_t *db, const char *dirpath)
{
	int dirpath_len = strlen(dirpath);
	TCHAR *dirnameT = safe_calloc(dirpath_len + 1, sizeof (*dirnameT));
	TCHAR *srchnameT = safe_calloc(dirpath_len + 4, sizeof (*srchnameT));
	WIN32_FIND_DATA find_data;
	HANDLE h_find;

	ASSERT(db != NULL);
	ASSERT(dirpath != NULL);

	MultiByteToWideChar(CP_UTF8, 0, dirpath, -1, dirnameT, dirpath_len + 1);
	StringCchPrintf(srchnameT, dirpath_len + 4, TEXT("%s\\*"), dirnameT);
	h_find = FindFirstFile(srchnameT, &find_data);
	if (h_find == INVALID_HANDLE_VALUE) {
		goto errout;
	}
	do {
		if (wcscmp(find_data.cFileName, TEXT(".")) == 0 ||
		    wcscmp(find_data.cFileName, TEXT("..")) == 0) {
			continue;
		}
		unsigned l = wcslen(find_data.cFileName) + 1;
		char *filename = safe_calloc(l, sizeof (*filename));
		WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1,
		    filename, l, NULL, NULL);
		(void) load_CIFP_file(db, dirpath, filename);
		free(filename);
	} while (FindNextFile(h_find, &find_data));

	FindClose(h_find);
	free(dirnameT);
	free(srchnameT);
	return (B_TRUE);
errout:
	free(dirnameT);
	free(srchnameT);
	return (B_FALSE);
}

#else	/* !IBM */

static bool_t
load_CIFP_dir(airportdb_t *db, const char *dirpath)
{
	DIR *dp = opendir(dirpath);
	struct dirent *de;

	ASSERT(db != NULL);
	ASSERT(dirpath != NULL);

	if (dp == NULL)
		return (B_FALSE);

	while ((de = readdir(dp)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		(void) load_CIFP_file(db, dirpath, de->d_name);
	}

	closedir(dp);

	return (B_TRUE);
}

#endif	/* !IBM */

/*
 * Initiates the supplemental information loading from X-Plane 11 navdata.
 * Here we try to determine, for runways which lacked that info in apt.dat,
 * the runway's threshold elevation and the GPA/TCH (based on a nearby
 * ILS GS antenna).
 */
static bool_t
load_xp11_navdata(airportdb_t *db)
{
	bool_t isdir;
	char *dirpath;
	bool_t success = B_FALSE;

	ASSERT(db != NULL);

	dirpath = mkpathname(db->xpdir, "Custom Data", "CIFP", NULL);
	if (file_exists(dirpath, &isdir) && isdir) {
		if (!load_CIFP_dir(db, dirpath))
			logMsg("%s: error parsing navdata, falling "
			    "back to default data.", dirpath);
	}
	free(dirpath);

	dirpath = mkpathname(db->xpdir, "Resources", "default data", "CIFP",
	    NULL);
	success = load_CIFP_dir(db, dirpath);
	if (!success) {
		logMsg("%s: error parsing navdata, please check your install",
		    dirpath);
	}
	free(dirpath);

	return (success);
}

/*
 * Checks to make sure our data cache is up to the newest version.
 */
static bool_t
check_cache_version(const airportdb_t *db, int app_version)
{
	char *version_str;
	int version = -1;

	ASSERT(db != NULL);

	if ((version_str = file2str(db->cachedir, "version", NULL)) != NULL) {
		version = atoi(version_str);
		free(version_str);
	}
	/*
	 * If the caller provided an app_version number, also check that.
	 * Otherwise ignore it.
	 */
	if (app_version == 0)
		version &= 0xffff;
	return (version == (ARPTDB_CACHE_VERSION | (app_version << 16)));
}

/**
 * Attempts to determine the AIRAC cycle currently in use in the navdata
 * on X-Plane 11/12. Sadly, there doesn't seem to be a nice data field for
 * this, so we need to do some fulltext searching.
 *
 * @param xpdir A path to the X-Plane installation directory.
 * @param cycle Mandatory return argument. If the search is successful,
 *	this will be filled with the AIRAC cycle number.
 *
 * @return `B_TRUE` if the determination succeeded, `B_FALSE` otherwise.
 */
bool_t
adb_airportdb_xp_airac_cycle(const char *xpdir, int *cycle)
{
	int linenum = 0;
	char *line = NULL;
	size_t linecap = 0;
	char *filename;
	FILE *fp;
	bool_t success = B_FALSE;

	ASSERT(xpdir != NULL);
	ASSERT(cycle != NULL);

	/* First try 'Custom Data', then 'default data' */
	filename = mkpathname(xpdir, "Custom Data", "earth_nav.dat", NULL);
	fp = fopen(filename, "r");
	if (fp == NULL) {
		free(filename);
		filename = mkpathname(xpdir, "Resources", "default data",
		    "earth_nav.dat", NULL);
		fp = fopen(filename, "r");
		if (fp == NULL)
			goto out;
	}

	while (!feof(fp)) {
		const char *word_start;

		/* Early abort if the header of the file was passed */
		if (linenum++ > 20)
			break;
		if (getline(&line, &linecap, fp) <= 0 ||
		    (strstr(line, "1100 ") != line &&
		    strstr(line, "1150 ") != line &&
		    strstr(line, "1200 ") != line) ||
		    (word_start = strstr(line, " data cycle ")) == NULL) {
			continue;
		}
		/* constant is length of " data cycle " string */
		success = (sscanf(word_start + 12, "%d", cycle) == 1);
		if (success)
			break;
	}
	free(line);
	fclose(fp);
out:
	free(filename);

	return (success);
}

/*
 * Grabs the AIRAC cycle from the X-Plane navdata and compares it to the
 * info we have in our cache. Returns true if the cycles match or false
 * otherwise (update to cache needed).
 */
static bool_t
check_airac_cycle(airportdb_t *db)
{
	char *cycle_str;
	int db_cycle = -1, xp_cycle = -1;

	ASSERT(db != NULL);

	if ((cycle_str = file2str(db->cachedir, "airac_cycle", NULL)) != NULL) {
		db_cycle = atoi(cycle_str);
		free(cycle_str);
	}
	if (!airportdb_xp11_airac_cycle(db->xpdir, &xp_cycle)) {
		if ((cycle_str = file2str(db->xpdir, "Custom Data", "GNS430",
		    "navdata", "cycle_info.txt", NULL)) == NULL)
			cycle_str = file2str(db->xpdir, "Resources", "GNS430",
			    "navdata", "cycle_info.txt", NULL);
		if (cycle_str != NULL) {
			char *sep = strstr(cycle_str, "AIRAC cycle");
			if (sep != NULL)
				sep = strstr(&sep[11], ": ");
			if (sep != NULL) {
				xp_cycle = atoi(&sep[2]);
			}
			free(cycle_str);
		}
	}

	db->xp_airac_cycle = xp_cycle;

	return (db_cycle == xp_cycle);
}

static bool_t
read_apt_dats_list(const airportdb_t *db, list_t *list)
{
	FILE *fp;
	char *filename;
	char *line = NULL;
	size_t cap = 0;

	ASSERT(db != NULL);
	ASSERT(list != NULL);

	filename = mkpathname(db->cachedir, "apt_dats", NULL);
	fp = fopen(filename, "r");
	free(filename);
	if (fp == NULL)
		return (B_FALSE);

	while (!feof(fp)) {
		apt_dats_entry_t *entry;

		if (getline(&line, &cap, fp) <= 0)
			continue;
		strip_space(line);
		entry = safe_malloc(sizeof (*entry));
		entry->fname = strdup(line);
		list_insert_tail(list, entry);
	}

	free(line);
	fclose(fp);

	return (B_TRUE);
}

static void
destroy_apt_dats_list(list_t *list)
{
	apt_dats_entry_t *e;
	ASSERT(list != NULL);
	while ((e = list_head(list)) != NULL) {
		list_remove(list, e);
		free(e->fname);
		free(e);
	}
	list_destroy(list);
}

static bool_t
cache_up_to_date(airportdb_t *db, list_t *xp_apt_dats, int app_version)
{
	list_t db_apt_dats;
	bool_t result = B_TRUE;
	apt_dats_entry_t *xp_e, *db_e;
	bool_t vers_ok, cycle_ok;

	ASSERT(db != NULL);
	ASSERT(xp_apt_dats != NULL);
	/*
	 * We need to call both of these functions because check_airac_cycle
	 * establishes what AIRAC cycle X-Plane uses and modifies `db', so
	 * we'll need it later on when recreating the cache.
	 */
	vers_ok = check_cache_version(db, app_version);
	cycle_ok = check_airac_cycle(db);
	if (!vers_ok || !cycle_ok)
		return (B_FALSE);

	list_create(&db_apt_dats, sizeof (apt_dats_entry_t),
	    offsetof(apt_dats_entry_t, node));
	read_apt_dats_list(db, &db_apt_dats);
	for (xp_e = list_head(xp_apt_dats), db_e = list_head(&db_apt_dats);
	    xp_e != NULL && db_e != NULL; xp_e = list_next(xp_apt_dats, xp_e),
	    db_e = list_next(&db_apt_dats, db_e)) {
		if (strcmp(xp_e->fname, db_e->fname) != 0) {
			result = B_FALSE;
			break;
		}
	}
	if (db_e != NULL || xp_e != NULL)
		result = B_FALSE;
	destroy_apt_dats_list(&db_apt_dats);

	return (result);
}

static arpt_index_t *
create_arpt_index(airportdb_t *db, const airport_t *arpt)
{
	arpt_index_t *idx = safe_calloc(1, sizeof (*idx));

	ASSERT(db != NULL);
	ASSERT(arpt != NULL);

	lacf_strlcpy(idx->ident, arpt->ident, sizeof (idx->ident));
	lacf_strlcpy(idx->icao, arpt->icao, sizeof (idx->icao));
	if (arpt->iata[0] != '\0')
		lacf_strlcpy(idx->iata, arpt->iata, sizeof (idx->iata));
	else
		lacf_strlcpy(idx->iata, "-", sizeof (idx->iata));
	if (arpt->cc[0] != '\0')
		lacf_strlcpy(idx->cc, arpt->cc, sizeof (idx->cc));
	else
		lacf_strlcpy(idx->cc, "ZZ", sizeof (idx->cc));
	idx->pos = TO_GEO3_32(arpt->refpt);
	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		if (rwy_is_hard(rwy->surf)) {
			idx->max_rwy_len = MAX(idx->max_rwy_len,
			    MET2FEET(rwy->ends[0].land_len));
			idx->max_rwy_len = MAX(idx->max_rwy_len,
			    MET2FEET(rwy->ends[1].land_len));
		}
	}
	idx->TA = arpt->TA;
	idx->TL = arpt->TL;

	avl_add(&db->arpt_index, idx);
	if (idx->icao[0] != '\0') {
		htbl2_set(&db->icao_index, idx->icao, sizeof (idx->icao),
		    idx, sizeof (*idx));
	}
	if (idx->iata[0] != '\0') {
		htbl2_set(&db->iata_index, idx->iata, sizeof (idx->iata),
		    idx, sizeof (*idx));
	}
	return (idx);
}

static bool_t
read_index_dat(airportdb_t *db)
{
	char *index_filename;
	FILE *index_file;
	char *line = NULL;
	size_t line_cap = 0;
	size_t num_lines = 0;

	ASSERT(db != NULL);
	index_filename = mkpathname(db->cachedir, "index.dat", NULL);
	index_file = fopen(index_filename, "r");

	if (index_file == NULL)
		return (B_FALSE);

	if (!db->override_settings) {
		conf_t *conf;
		char *filename = mkpathname(db->cachedir, "settings.conf",
		    NULL);

		if (file_exists(filename, NULL) &&
		    (conf = conf_read_file(filename, NULL)) != NULL) {
			conf_get_b(conf, "ifr_only", &db->ifr_only);
			conf_get_b(conf, "normalize_gate_names",
			    &db->normalize_gate_names);
			conf_free(conf);
		}
		LACF_DESTROY(filename);
	}
	while (lacf_getline(&line, &line_cap, index_file) > 0)
		num_lines++;
	rewind(index_file);

	recreate_icao_iata_tables(db, num_lines);

	while (lacf_getline(&line, &line_cap, index_file) > 0) {
		arpt_index_t *idx = safe_calloc(1, sizeof (*idx));
		avl_index_t where;

		if (sscanf(line, "%7s %7s %3s %2s %f %f %f %hu %hu %hu",
		    idx->ident, idx->icao, idx->iata, idx->cc,
		    &idx->pos.lat, &idx->pos.lon, &idx->pos.elev,
		    &idx->max_rwy_len, &idx->TA, &idx->TL) != 10) {
			free(idx);
			continue;
		}
		if (avl_find(&db->arpt_index, idx, &where) == NULL) {
			avl_insert(&db->arpt_index, idx, where);
			htbl2_set(&db->icao_index, idx->icao,
			    sizeof (idx->icao), idx, sizeof (*idx));
			if (strcmp(idx->iata, "-") != 0) {
				htbl2_set(&db->iata_index, idx->iata,
				    sizeof (idx->iata), idx, sizeof (*idx));
			}
		} else {
			logMsg("WARNING: found duplicate airport ident %s "
			    "in index. Skipping it. This shouldn't happen "
			    "unless the index is damaged.", idx->ident);
			free(idx);
		}
	}
	free(line);
	fclose(index_file);
	free(index_filename);

	return (B_TRUE);
}

static bool_t
write_index_dat(const arpt_index_t *idx, FILE *index_file)
{
	ASSERT(idx != NULL);
	ASSERT(index_file != NULL);
	return (fprintf(index_file, "%s\t%s\t%s\t%s\t%f\t%f\t%.0f\t"
	    "%hu\t%hu\t%hu\n",
	    idx->ident, idx->icao[0] != '\0' ? idx->icao : "-",
	    idx->iata[0] != '\0' ? idx->iata : "-",
	    idx->cc[0] != '\0' ? idx->cc : "-",
	    idx->pos.lat, idx->pos.lon, idx->pos.elev,
	    idx->max_rwy_len, idx->TA, idx->TL) > 0);
}

static bool_t
recreate_cache_skeleton(airportdb_t *db, list_t *apt_dat_files, int app_version)
{
	char *filename;
	FILE *fp;
	bool_t exists, isdir;

	ASSERT(db != NULL);
	ASSERT(apt_dat_files != NULL);

	exists = file_exists(db->cachedir, &isdir);
	if ((exists && ((isdir && !remove_directory(db->cachedir)) ||
	    (!isdir && !remove_file(db->cachedir, B_FALSE)))) ||
	    !create_directory_recursive(db->cachedir))
		return (B_FALSE);

	filename = mkpathname(db->cachedir, "version", NULL);
	fp = fopen(filename, "w");
	if (fp == NULL) {
		logMsg("Error writing new airport database, can't open "
		    "%s for writing: %s", filename, strerror(errno));
		free(filename);
		return (B_FALSE);
	}
	fprintf(fp, "%d", (app_version << 16) | ARPTDB_CACHE_VERSION);
	fclose(fp);
	free(filename);

	filename = mkpathname(db->cachedir, "airac_cycle", NULL);
	fp = fopen(filename, "w");
	if (fp == NULL) {
		logMsg("Error writing new airport database, can't open "
		    "%s for writing: %s", filename, strerror(errno));
		free(filename);
		return (B_FALSE);
	}
	fprintf(fp, "%d", db->xp_airac_cycle);
	fclose(fp);
	free(filename);

	filename = mkpathname(db->cachedir, "apt_dats", NULL);
	fp = fopen(filename, "w");
	if (fp == NULL) {
		logMsg("Error writing new airport database, can't open "
		    "%s for writing: %s", filename, strerror(errno));
		free(filename);
		return (B_FALSE);
	}
	for (apt_dats_entry_t *e = list_head(apt_dat_files); e != NULL;
	    e = list_next(apt_dat_files, e))
		fprintf(fp, "%s\n", e->fname);
	fclose(fp);
	free(filename);

	if (db->override_settings) {
		conf_t *conf = conf_create_empty();

		conf_set_b(conf, "ifr_only", db->ifr_only);
		conf_set_b(conf, "normalize_gate_names",
		    db->normalize_gate_names);
		filename = mkpathname(db->cachedir, "settings.conf", NULL);
		conf_write_file(conf, filename);
		conf_free(conf);
	}

	return (B_TRUE);
}

/**
 * Rebuilds all cache state from disk. This must be called only once, after
 * calling airportdb_create() to actually populate the cache. On first run,
 * the disk cache will be empty, and this function will interrogate X-Plane's
 * installed scenery to rebuild the cache. If the cache already exists, this
 * function checks for changes in X-Plane's scenery and/or navdata and if
 * necessary rebuilds the cache, or simply loads it as-is.
 *
 * Please note that on first run (or if a scenery change is detected), this
 * function can take quite a bit of time to run (tens of seconds to minutes,
 * if the machine is particularly slow and has lots of scenery). You are
 * therefore encouraged to run it only once at startup. If you need to create
 * multiple \ref airportdb instances, create them in sequence, NOT in
 * parallel! This function perform disk I/O an the cache is NOT designed for
 * concurrent writing! Once created by the first instance of \ref airportdb,
 * subsequent \ref airportdb instances will not need to rebuild and will
 * simply read the cache as-is, so this function will return nearly instantly.
 *
 * Be sure to configure the `ifr_only' flag in the \ref airportdb structure
 * before calling this function. That flag specifies whether the cache should
 * only contain airports with published instrument approaches, or if VFR-only
 * airports should also be allowed.
 *
 * @param db Database instance which was previously initialized using
 *	airportdb_create().
 * @param app_version An application-side version tag to apply to the cache.
 *	This can be used in any way you want. If the cache detects a change
 *	in the `app_version` tag, the cache will be recreated. You can use
 *	this to, for example, recreate the cache, if you changed your
 *	cache configuration between versions of your addon.
 *
 * @return `B_TRUE` if successful, `B_FALSE` if cache creation failed.
 */
bool_t
adb_recreate_cache(airportdb_t *db, int app_version)
{
	list_t apt_dat_files;
	bool_t success = B_TRUE;
	char *index_filename = NULL;
	FILE *index_file = NULL;
	iconv_t cd;
	char *prev_locale = NULL, *saved_locale = NULL;

	ASSERT(db != NULL);

	list_create(&apt_dat_files, sizeof (apt_dats_entry_t),
	    offsetof(apt_dats_entry_t, node));
	find_all_apt_dats(db, &apt_dat_files);
	if (cache_up_to_date(db, &apt_dat_files, app_version) &&
	    read_index_dat(db)) {
		goto out;
	}
	/* This is needed to get iconv transliteration to work correctly */
	prev_locale = setlocale(LC_CTYPE, NULL);
	if (prev_locale != NULL)
		saved_locale = safe_strdup(prev_locale);
	setlocale(LC_CTYPE, "");
	/* First scan all the provided apt.dat files */
	cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	for (apt_dats_entry_t *e = list_head(&apt_dat_files); e != NULL;
	    e = list_next(&apt_dat_files, e)) {
		bool_t fill_in_dups = (list_next(&apt_dat_files, e) == NULL);
		read_apt_dat(db, e->fname, B_TRUE, &cd, fill_in_dups);
	}
	iconv_close(cd);
	if (saved_locale != NULL) {
		setlocale(LC_CTYPE, saved_locale);
		free(saved_locale);
	}
	if (!load_xp11_navdata(db)) {
		success = B_FALSE;
		goto out;
	}
	if (avl_numnodes(&db->apt_dat) == 0) {
		logMsg("navdata error: it appears your simulator's "
		    "navigation database is broken, or your simulator "
		    "contains no airport scenery. Please reinstall the "
		    "database and retry.");
		success = B_FALSE;
		goto out;
	}

	if (!recreate_cache_skeleton(db, &apt_dat_files, app_version)) {
		success = B_FALSE;
		goto out;
	}
	index_filename = mkpathname(db->cachedir, "index.dat", NULL);
	index_file = fopen(index_filename, "w");
	if (index_file == NULL) {
		logMsg("Error creating airport database index file %s: %s",
		    index_filename, strerror(errno));
		success = B_FALSE;
		goto out;
	}
	recreate_icao_iata_tables(db, avl_numnodes(&db->apt_dat));
	for (airport_t *arpt = avl_first(&db->apt_dat), *next_arpt;
	    arpt != NULL; arpt = next_arpt) {
		next_arpt = AVL_NEXT(&db->apt_dat, arpt);
		ASSERT(arpt->geo_linked);
		/*
		 * If the airport isn't in Airports.txt, we want to dump the
		 * airport, because we don't have TA/TL info on them. But if
		 * we are in ifr_only=B_FALSE mode, then accept it anyway.
		 */
		if (!arpt->have_iaps && db->ifr_only) {
			geo_unlink_airport(db, arpt);
			avl_remove(&db->apt_dat, arpt);
			free_airport(arpt);
		} else {
			arpt_index_t *idx = create_arpt_index(db, arpt);
			write_index_dat(idx, index_file);
		}
	}
	for (airport_t *arpt = avl_first(&db->apt_dat); arpt != NULL;
	    arpt = AVL_NEXT(&db->apt_dat, arpt)) {
		char *dirname;

		ASSERT(arpt->geo_linked);
		ASSERT(avl_numnodes(&arpt->rwys) != 0);

		dirname = apt_dat_cache_dir(db, GEO3_TO_GEO2(arpt->refpt),
		    NULL);
		if (!create_directory(dirname) || !write_apt_dat(db, arpt)) {
			free(dirname);
			success = B_FALSE;
			goto out;
		}
		free(dirname);
	}
out:
	adb_unload_distant_airport_tiles(db, NULL_GEO_POS2);
	destroy_apt_dats_list(&apt_dat_files);
	free(index_filename);
	if (index_file != NULL)
		fclose(index_file);

	return (success);
}

/*
 * The approach proximity bounding box is constructed as follows:
 *
 *   5500 meters
 *   |<=======>|
 *   |         |
 * d +-_  (c1) |
 *   |   -._3 degrees
 *   |      -_ c
 *   |         +-------------------------------+
 *   |         | ====  ----         ----  ==== |
 * x +   thr_v-+ ==== - ------> dir_v - - ==== |
 *   |         | ====  ----         ----  ==== |
 *   |         +-------------------------------+
 *   |      _- b
 *   |   _-.
 * a +--    (b1)
 *
 * If there is another parallel runway, we make sure our bounding boxes
 * don't overlap. We do this by introducing two additional points, b1 and
 * c1, in between a and b or c and d respectively. We essentially shear
 * the overlapping excess from the bounding polygon.
 */
static vect2_t *
make_apch_prox_bbox(const runway_t *rwy, int end_i)
{
	const runway_end_t *end, *oend;
	const fpp_t *fpp;
	double limit_left = 1000000, limit_right = 1000000;
	vect2_t x, a, b, b1, c, c1, d, thr_v, othr_v, dir_v;
	vect2_t *bbox = safe_calloc(7, sizeof (vect2_t));
	size_t n_pts = 0;

	ASSERT(rwy != NULL);
	fpp = &rwy->arpt->fpp;
	ASSERT(end_i == 0 || end_i == 1);

	/*
	 * By pre-initing the whole array to null vectors, we can make the
	 * bbox either contain 4, 5 or 6 points, depending on whether
	 * shearing due to a close parallel runway needs to be applied.
	 */
	for (int i = 0; i < 7; i++)
		bbox[i] = NULL_VECT2;

	end = &rwy->ends[end_i];
	oend = &rwy->ends[!end_i];
	thr_v = end->thr_v;
	othr_v = oend->thr_v;
	dir_v = vect2_sub(othr_v, thr_v);

	x = vect2_add(thr_v, vect2_set_abs(vect2_neg(dir_v),
	    RWY_APCH_PROXIMITY_LON_DISPL));
	a = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
	    rwy->width / 2 + RWY_APCH_PROXIMITY_LAT_DISPL));
	b = vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
	    rwy->width / 2));
	c = vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
	    rwy->width / 2));
	d = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
	    rwy->width / 2 + RWY_APCH_PROXIMITY_LAT_DISPL));

	b1 = NULL_VECT2;
	c1 = NULL_VECT2;

	/*
	 * If our rwy_id designator contains a L/C/R, then we need to
	 * look for another parallel runway.
	 */
	if (strlen(end->id) >= 3) {
		int my_num_id = atoi(end->id);

		for (const runway_t *orwy = avl_first(&rwy->arpt->rwys);
		    orwy != NULL; orwy = AVL_NEXT(&rwy->arpt->rwys, orwy)) {
			const runway_end_t *orwy_end;
			vect2_t othr_v, v;
			double a, dist;

			if (orwy == rwy)
				continue;
			if (atoi(orwy->ends[0].id) == my_num_id)
				orwy_end = &orwy->ends[0];
			else if (atoi(orwy->ends[1].id) == my_num_id)
				orwy_end = &orwy->ends[1];
			else
				continue;

			/*
			 * This is a parallel runway, measure the
			 * distance to it from us.
			 */
			othr_v = geo2fpp(GEO3_TO_GEO2(orwy_end->thr), fpp);
			v = vect2_sub(othr_v, thr_v);
			if (IS_ZERO_VECT2(v)) {
				logMsg("CAUTION: your nav DB is looking very "
				    "strange: runways %s and %s at %s are on "
				    "top of each other (coords: %fx%f)",
				    end->id, orwy_end->id, rwy->arpt->icao,
				    orwy_end->thr.lat, orwy_end->thr.lon);
				continue;
			}
			a = rel_hdg(dir2hdg(dir_v), dir2hdg(v));
			dist = fabs(sin(DEG2RAD(a)) * vect2_abs(v));

			if (a < 0)
				limit_left = MIN(dist / 2, limit_left);
			else
				limit_right = MIN(dist / 2, limit_right);
		}
	}

	if (limit_left < RWY_APCH_PROXIMITY_LAT_DISPL) {
		c1 = vect2vect_isect(vect2_sub(d, c), c, vect2_neg(dir_v),
		    vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
		    limit_left)), B_FALSE);
		d = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
		    limit_left));
	}
	if (limit_right < RWY_APCH_PROXIMITY_LAT_DISPL) {
		b1 = vect2vect_isect(vect2_sub(b, a), a, vect2_neg(dir_v),
		    vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
		    limit_right)), B_FALSE);
		a = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
		    limit_right));
	}

	bbox[n_pts++] = a;
	if (!IS_NULL_VECT(b1))
		bbox[n_pts++] = b1;
	bbox[n_pts++] = b;
	bbox[n_pts++] = c;
	if (!IS_NULL_VECT(c1))
		bbox[n_pts++] = c1;
	bbox[n_pts++] = d;

	return (bbox);
}

/*
 * Prepares a runway's bounding box vector coordinates using the airport
 * coord fpp transform.
 */
static void
load_rwy_info(runway_t *rwy)
{
	ASSERT(rwy != NULL);
	ASSERT(rwy->arpt->load_complete);
	/*
	 * RAAS runway proximity entry bounding box is defined as:
	 *
	 *              1000ft                                   1000ft
	 *            |<======>|                               |<======>|
	 *            |        |                               |        |
	 *     ---- d +-------------------------------------------------+ c
	 * 1.5x  ^    |        |                               |        |
	 *  rwy  |    |        |                               |        |
	 * width |    |        +-------------------------------+        |
	 *       v    |        | ====  ----         ----  ==== |        |
	 *     -------|-thresh-x ==== - - - - - - - - - - ==== |        |
	 *       ^    |        | ====  ----         ----  ==== |        |
	 * 1.5x  |    |        +-------------------------------+        |
	 *  rwy  |    |                                                 |
	 * width v    |                                                 |
	 *     ---- a +-------------------------------------------------+ b
	 */
	vect2_t dt1v = geo2fpp(GEO3_TO_GEO2(rwy->ends[0].thr), &rwy->arpt->fpp);
	vect2_t dt2v = geo2fpp(GEO3_TO_GEO2(rwy->ends[1].thr), &rwy->arpt->fpp);
	double displ1 = rwy->ends[0].displ;
	double displ2 = rwy->ends[1].displ;
	double blast1 = rwy->ends[0].blast;
	double blast2 = rwy->ends[1].blast;

	vect2_t dir_v = vect2_sub(dt2v, dt1v);
	double dlen = vect2_abs(dir_v);
	double hdg1 = dir2hdg(dir_v);
	double hdg2 = dir2hdg(vect2_neg(dir_v));

	vect2_t t1v = vect2_add(dt1v, vect2_set_abs(dir_v, displ1));
	vect2_t t2v = vect2_add(dt2v, vect2_set_abs(vect2_neg(dir_v), displ2));
	double len = vect2_abs(vect2_sub(t2v, t1v));

	double prox_lon_bonus1 = MAX(displ1, RWY_PROXIMITY_LON_DISPL - displ1);
	double prox_lon_bonus2 = MAX(displ2, RWY_PROXIMITY_LON_DISPL - displ2);

	rwy->ends[0].thr_v = t1v;
	rwy->ends[1].thr_v = t2v;
	rwy->ends[0].dthr_v = dt1v;
	rwy->ends[1].dthr_v = dt2v;
	rwy->ends[0].hdg = hdg1;
	rwy->ends[1].hdg = hdg2;
	rwy->ends[0].land_len = vect2_abs(vect2_sub(dt2v, t1v));
	rwy->ends[1].land_len = vect2_abs(vect2_sub(dt1v, t2v));
	rwy->length = len;

	ASSERT(rwy->rwy_bbox == NULL);

	rwy->rwy_bbox = make_rwy_bbox(t1v, dir_v, rwy->width, len, 0);
	rwy->tora_bbox = make_rwy_bbox(dt1v, dir_v, rwy->width, dlen, 0);
	rwy->asda_bbox = make_rwy_bbox(dt1v, dir_v, rwy->width,
	    dlen + blast2, blast1);
	rwy->prox_bbox = make_rwy_bbox(t1v, dir_v, RWY_PROXIMITY_LAT_FRACT *
	    rwy->width, len + prox_lon_bonus2, prox_lon_bonus1);

	rwy->ends[0].apch_bbox = make_apch_prox_bbox(rwy, 0);
	rwy->ends[1].apch_bbox = make_apch_prox_bbox(rwy, 1);
}

static void
unload_rwy_info(runway_t *rwy)
{
	ASSERT(rwy != NULL);
	ASSERT(rwy->rwy_bbox != NULL);

	free(rwy->rwy_bbox);
	rwy->rwy_bbox = NULL;
	free(rwy->tora_bbox);
	rwy->tora_bbox = NULL;
	free(rwy->asda_bbox);
	rwy->asda_bbox = NULL;
	free(rwy->prox_bbox);
	rwy->prox_bbox = NULL;

	free(rwy->ends[0].apch_bbox);
	rwy->ends[0].apch_bbox = NULL;
	free(rwy->ends[1].apch_bbox);
	rwy->ends[1].apch_bbox = NULL;
}

/*
 * Given an airport, loads the information of the airport into a more readily
 * workable (but more verbose) format. This function prepares a flat plane
 * transform centered on the airport's reference point and pre-computes all
 * relevant points for the airport in that space.
 * Returns true if the operation succeeded, false otherwise. The airport needs
 * to have an airport reference point defined before this will succeed.
 */
static bool_t
load_airport(airport_t *arpt)
{
	ASSERT(arpt != NULL);

	if (arpt->load_complete)
		return (B_TRUE);

	if (isnan(arpt->refpt.lat) || isnan(arpt->refpt.lon) ||
	    isnan(arpt->refpt.elev))
		return (B_FALSE);

	/* must go ahead of load_rwy_info to not trip an assertion */
	arpt->load_complete = B_TRUE;

	arpt->fpp = ortho_fpp_init(GEO3_TO_GEO2(arpt->refpt), 0, &wgs84,
	    B_FALSE);
	arpt->ecef = geo2ecef_ft(arpt->refpt, &wgs84);

	for (runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy))
		load_rwy_info(rwy);

	return (B_TRUE);
}

static void
unload_airport(airport_t *arpt)
{
	ASSERT(arpt != NULL);
	if (!arpt->load_complete)
		return;
	for (runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy))
		unload_rwy_info(rwy);
	arpt->load_complete = B_FALSE;
}

static void
free_airport(airport_t *arpt)
{
	void *cookie;
	runway_t *rwy;
	freq_info_t *freq;
	ramp_start_t *rs;

	ASSERT(arpt != NULL);
	if (arpt->load_complete)
		unload_airport(arpt);

	cookie = NULL;
	while ((rs = avl_destroy_nodes(&arpt->ramp_starts, &cookie)) != NULL)
		free(rs);
	avl_destroy(&arpt->ramp_starts);

	cookie = NULL;
	while ((rwy = avl_destroy_nodes(&arpt->rwys, &cookie)) != NULL)
		free(rwy);
	avl_destroy(&arpt->rwys);

	while ((freq = list_remove_head(&arpt->freqs)) != NULL)
		free(freq);
	list_destroy(&arpt->freqs);
	ASSERT(!list_link_active(&arpt->cur_arpts_node));
	LACF_DESTROY(arpt->name_orig);
	LACF_DESTROY(arpt->city);
	LACF_DESTROY(arpt->country);
	ZERO_FREE(arpt);
}

/**
 * The actual worker function for adb_find_nearest_airports. Performs the
 * search in a specified geo_table tile. Position is a 3-space ECEF vector.
 */
static void
find_nearest_airports_tile(airportdb_t *db, vect3_t ecef,
    geo_pos2_t tile_coord, list_t *l)
{
	tile_t *tile;

	ASSERT(db != NULL);
	ASSERT(l != NULL);
	tile = geo_table_get_tile(db, tile_coord, B_FALSE, NULL);

	if (tile == NULL)
		return;
	for (airport_t *arpt = avl_first(&tile->arpts); arpt != NULL;
	    arpt = AVL_NEXT(&tile->arpts, arpt)) {
		vect3_t arpt_ecef = geo2ecef_ft(arpt->refpt, &wgs84);
		if (vect3_abs(vect3_sub(ecef, arpt_ecef)) < db->load_limit) {
			list_insert_tail(l, arpt);
			VERIFY(load_airport(arpt));
		}
	}
}

/**
 * Locates all airports within the load limit distance of a geographic
 * reference position. The airports are searched for in the apt_dat database
 * and this function returns its result into the list argument. To set the
 * load limit distance, use adb_set_airport_load_limit().
 *
 * @return A list of \ref airport structures. This list must be freed using
 * adb_free_nearest_airport_list(). You may also not call
 * adb_find_nearest_airports() multiple times without first freeing the
 * returned list, as the linked list reuses the same list node inside of
 * the \ref airport structures.
 *
 * @return When using an \ref airportdb_t from multiple threads, make sure to
 * lock database using airportdb_lock() before doing the lookup and working
 * with the returned list. After freeing the list, call airportdb_unlock().
 */
list_t *
adb_find_nearest_airports(airportdb_t *db, geo_pos2_t my_pos)
{
	vect3_t ecef;
	list_t *l;

	ASSERT(db != NULL);
	ASSERT(!IS_NULL_GEO_POS(my_pos));
	ecef = geo2ecef_ft(GEO_POS3(my_pos.lat, my_pos.lon, 0), &wgs84);

	l = safe_malloc(sizeof (*l));
	list_create(l, sizeof (airport_t), offsetof(airport_t, cur_arpts_node));
	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++)
			find_nearest_airports_tile(db, ecef,
			    GEO_POS2(my_pos.lat + i, my_pos.lon + j), l);
	}

	return (l);
}

/**
 * Frees the list returned from find_nearest_airports().
 *
 * You mustn't call find_nearest_airports() multiple times without first
 * freeing the returned list, as the linked list reuses the same list node
 * inside of the \ref airport structures.
 *
 * When using an \ref airportdb_t from multiple threads, make sure to lock
 * database using airportdb_lock() before doing the lookup and working with
 * the returned list. After freeing the list, call airportdb_unlock().
 */
void
adb_free_nearest_airport_list(list_t *l)
{
	ASSERT(l != NULL);
	for (airport_t *a = list_head(l); a != NULL; a = list_head(l))
		list_remove(l, a);
	ZERO_FREE(l);
}

static void
load_airports_in_tile(airportdb_t *db, geo_pos2_t tile_pos)
{
	bool_t created;
	char *cache_dir, *fname;
	char lat_lon[16];

	ASSERT(db != NULL);
	ASSERT(!IS_NULL_GEO_POS(tile_pos));

	(void) geo_table_get_tile(db, tile_pos, B_TRUE, &created);
	if (!created)
		return;

	tile_pos = geo_pos2tile_pos(tile_pos, B_FALSE);
	cache_dir = apt_dat_cache_dir(db, tile_pos, NULL);
	snprintf(lat_lon, sizeof (lat_lon), TILE_NAME_FMT,
	    tile_pos.lat, tile_pos.lon);
	fname = mkpathname(cache_dir, lat_lon, NULL);
	if (file_exists(fname, NULL))
		read_apt_dat(db, fname, B_FALSE, NULL, B_FALSE);
	free(cache_dir);
	free(fname);
}

static void
free_tile(airportdb_t *db, tile_t *tile, bool_t do_remove)
{
	void *cookie = NULL;
	airport_t *arpt;

	ASSERT(db != NULL);
	ASSERT(tile != NULL);

	while ((arpt = avl_destroy_nodes(&tile->arpts, &cookie)) != NULL) {
		avl_remove(&db->apt_dat, arpt);
		free_airport(arpt);
	}
	avl_destroy(&tile->arpts);

	if (do_remove)
		avl_remove(&db->geo_table, tile);
	ZERO_FREE(tile);
}

/**
 * Sets the distance limit within which airports are loaded from disk.
 * A loaded airport has all its information resolved, such as the ECEF
 * positions of the threshold, all bounding boxes constructed, etc.
 * The default airport load limit for a newly created airportdb is
 * 14,816 meters (8nm).
 *
 * @param db The database for which to set the load limit.
 * @param limit The distance limit in meters.
 */
void
adb_set_airport_load_limit(airportdb_t *db, double limit)
{
	ASSERT(db != NULL);
	db->load_limit = limit;
}

/**
 * Performs a load of all airports within the distance load limit of a given
 * position. The distance limit is set using adb_set_airport_load_limit().
 * A loaded airport has all its information resolved, such as the ECEF
 * positions of the threshold, all bounding boxes constructed, etc.
 *
 * @param db The database for which to perform the load.
 * @param my_pos The 2-space geographic position around which to perform
 *	the load. This MUST be a valid geographic coordinate.
 */
void
load_nearest_airport_tiles(airportdb_t *db, geo_pos2_t my_pos)
{
	ASSERT(db != NULL);
	ASSERT(!IS_NULL_GEO_POS(my_pos));

	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++)
			load_airports_in_tile(db, GEO_POS2(my_pos.lat + i,
			    my_pos.lon + j));
	}
}

static double
lon_delta(double x, double y)
{
	double u = MAX(x, y), d = MIN(x, y);

	if (u - d <= 180)
		return (fabs(u - d));
	else
		return (fabs((180 - u) - (-180 - d)));
}

static void
unload_distant_airport_tiles_i(airportdb_t *db, tile_t *tile, geo_pos2_t my_pos)
{
	ASSERT(db != NULL);
	ASSERT(tile != NULL);
	if (IS_NULL_GEO_POS(my_pos) ||
	    fabs(tile->pos.lat - floor(my_pos.lat)) > 1 ||
	    lon_delta(tile->pos.lon, floor(my_pos.lon)) > 1)
		free_tile(db, tile, B_TRUE);
}

/**
 * Unloads airports that are beyond the airport load limit distance from
 * a given position. This frees some memory allocations for the airports.
 * You should be calling this function at regular intervals as you move
 * through the world, to make sure airports don't remain loaded forever.
 * Use adb_set_airport_load_limit() to set the distance limit.
 *
 * @param db The database for which to perform the unload.
 * @param my_pos The 2-space geographic position to use as the reference.
 *	Airports beyond the load limit distance will be unloaded. You
 *	may pass a `NULL_GEO_POS2` for this argument - this will make the
 *	airportdb unload all loaded airports.
 */
void
adb_unload_distant_airport_tiles(airportdb_t *db, geo_pos2_t my_pos)
{
	tile_t *tile, *next_tile;

	ASSERT(db != NULL);
	/* my_pos can be NULL_GEO_POS2 */

	for (tile = avl_first(&db->geo_table); tile != NULL; tile = next_tile) {
		next_tile = AVL_NEXT(&db->geo_table, tile);
		unload_distant_airport_tiles_i(db, tile, my_pos);
	}

	if (IS_NULL_GEO_POS(my_pos)) {
		ASSERT(avl_numnodes(&db->geo_table) == 0);
		ASSERT(avl_numnodes(&db->apt_dat) == 0);
	}
}

static int
arpt_index_compar(const void *a, const void *b)
{
	const arpt_index_t *ia = a, *ib = b;
	int res = strcmp(ia->ident, ib->ident);

	if (res < 0)
		return (-1);
	if (res > 0)
		return (1);
	return (0);
}

/**
 * Initializes an \ref airportdb_t structure. Please note that this doesn't
 * populate the data in the database. It simply sets up its memory state to
 * be ready for operation. To load disk data into the database and actually
 * start using it, call adb_recreate_cache() after initializing the \ref
 * airportdb structure. Use airportdb_destroy() to free the resources
 * allocated by an \ref airportdb_t.
 */
void
airportdb_create(airportdb_t *db, const char *xpdir, const char *cachedir)
{
	VERIFY(db != NULL);
	VERIFY(xpdir != NULL);
	VERIFY(cachedir != NULL);

	db->inited = B_TRUE;
	db->xpdir = strdup(xpdir);
	db->cachedir = strdup(cachedir);
	db->load_limit = ARPT_LOAD_LIMIT;
	db->ifr_only = B_TRUE;
	db->normalize_gate_names = B_FALSE;

	mutex_init(&db->lock);

	avl_create(&db->apt_dat, airport_compar, sizeof (airport_t),
	    offsetof(airport_t, apt_dat_node));
	avl_create(&db->geo_table, tile_compar, sizeof (tile_t),
	    offsetof(tile_t, node));
	avl_create(&db->arpt_index, arpt_index_compar,
	    sizeof (arpt_index_t), offsetof(arpt_index_t, node));
	/*
	 * Just some defaults - we'll resize the tables later when
	 * we actually read the index file.
	 */
	htbl2_create(&db->icao_index, 16, AIRPORTDB_ICAO_LEN,
	    sizeof (arpt_index_t), B_TRUE);
	htbl2_create(&db->iata_index, 16, AIRPORTDB_IATA_LEN,
	    sizeof (arpt_index_t), B_TRUE);
}

/**
 * Destroys an \ref airportdb_t structure, after it has been initialized using
 * airportdb_create().
 */
void
airportdb_destroy(airportdb_t *db)
{
	tile_t *tile;
	void *cookie;
	arpt_index_t *idx;

	ASSERT(db != NULL);
	if (!db->inited)
		return;

	cookie = NULL;
	while ((idx = avl_destroy_nodes(&db->arpt_index, &cookie)) != NULL)
		free(idx);
	avl_destroy(&db->arpt_index);

	/* airports are freed in the free_tile function */
	cookie = NULL;
	while ((tile = avl_destroy_nodes(&db->geo_table, &cookie)) != NULL)
		free_tile(db, tile, B_FALSE);
	avl_destroy(&db->geo_table);
	avl_destroy(&db->apt_dat);

	htbl2_empty(&db->icao_index, sizeof (arpt_index_t), NULL, NULL);
	htbl2_destroy(&db->icao_index);
	htbl2_empty(&db->iata_index, sizeof (arpt_index_t), NULL, NULL);
	htbl2_destroy(&db->iata_index);

	mutex_destroy(&db->lock);

	free(db->xpdir);
	free(db->cachedir);
	memset(db, 0, sizeof (*db));
}

/**
 * Locks an \ref airportdb_t structure, for use by multiple threads. Use
 * airportdb_unlock() to release the lock. Locking is recursive, so
 * airportdb_unlock() must be called as many times as airportdb_lock()
 * to fully release the lock.
 */
void
airportdb_lock(airportdb_t *db)
{
	ASSERT(db != NULL);
	mutex_enter(&db->lock);
}

/**
 * Unlocks an \ref airportdb_t structure after it has been locked using
 * airportdb_lock(). Locking is recursive, so airportdb_unlock() must be
 * called as many times as airportdb_lock() to fully release the lock.
 */
void
airportdb_unlock(airportdb_t *db)
{
	ASSERT(db != NULL);
	mutex_exit(&db->lock);
}

/**
 * Searches for an airport in the database by unique identifier.
 *
 * @param db Database in which to perform the lookup.
 * @param ident The unique identifier of the airport. Please note that
 *	X-Plane's airport identifiers needn't be exactly the same as their
 *	ICAO codes. Some airports don't have ICAO codes, or their ident
 *	and ICAO code are inconsistent (e.g. due to the ICAO code having
 *	been changed by the relevant civil aviation authority).
 *
 * @return The \ref airport structure if the airport was found, or NULL if not.
 */
airport_t *
adb_airport_lookup_by_ident(airportdb_t *db, const char *ident)
{
	arpt_index_t *idx;
	arpt_index_t srch = {};

	ASSERT(db != NULL);
	ASSERT(ident != NULL);

	lacf_strlcpy(srch.ident, ident, sizeof (srch.ident));
	idx = avl_find(&db->arpt_index, &srch, NULL);
	if (idx == NULL)
		return (NULL);
	return (adb_airport_lookup(db, ident, TO_GEO2(idx->pos)));
}

static void
airport_lookup_htbl_multi(airportdb_t *db, const list_t *list,
    void (*found_cb)(airport_t *airport, void *userinfo), void *userinfo)
{
	ASSERT(db != NULL);
	ASSERT(list != NULL);

	for (void *mv = list_head(list); mv != NULL;
	    mv = list_next(list, mv)) {
		arpt_index_t *idx = htbl2_value_multi(mv, sizeof (*idx));

		if (found_cb != NULL) {
			airport_t *apt = adb_airport_lookup(db, idx->ident,
			    TO_GEO2(idx->pos));
			/*
			 * Although we should NEVER hit a state where this
			 * lookup fails, the function might need to perform
			 * I/O to read the tile's apt.dat, which brings the
			 * possibility of a failed read. Since users' drives
			 * can be all kinds of garbage, we can't hard-assert
			 * here due to potential I/O issues.
			 */
			if (apt != NULL) {
				found_cb(apt, userinfo);
			} else {
				logMsg("WARNING: airport database index is "
				    "damaged: index contains ICAO %s, but "
				    "the associated database tile doesn't "
				    "appear to contain this airport.",
				    idx->icao);
			}
		}
	}
}

/**
 * Lookup by ICAO ID, with support for duplicates.
 *
 * @param db Database in which to perform the lookup.
 * @param icao The 4-letter ICAO code to look for.
 * @param found_cb Callback which will be called with each airport matching
 *	the ICAO ID. The `airport` argument is filled with the \ref airport
 *	information about each airport found. The `userinfo` argument will
 *	be filled with whatever is provided in the userinfo argument to the
 *	adb_airport_index_walk() function.
 * @param userinfo Optional user info pointer, which will be passed to every
 *	call to `found_cb` in its second argument.
 *
 * @return The number of airports found.
 */
size_t
adb_airport_lookup_by_icao(airportdb_t *db, const char *icao,
    void (*found_cb)(airport_t *airport, void *userinfo), void *userinfo)
{
	const list_t *list;
	char icao_srch[AIRPORTDB_ICAO_LEN] = {};

	ASSERT(db != NULL);
	ASSERT(icao != NULL);

	strlcpy(icao_srch, icao, sizeof (icao_srch));
	list = htbl2_lookup_multi(&db->icao_index, icao_srch,
	    sizeof (icao_srch));
	if (list != NULL) {
		airport_lookup_htbl_multi(db, list, found_cb, userinfo);
		return (list_count(list));
	} else {
		return (0);
	}
}

/**
 * Lookup by IATA ID, with support for duplicates.
 *
 * Arguments and return value are the same as adb_airport_lookup_by_icao(),
 * except the lookup is done by the IATA code, rather than ICAO code.
 */
size_t
adb_airport_lookup_by_iata(airportdb_t *db, const char *iata,
    void (*found_cb)(airport_t *airport, void *userinfo), void *userinfo)
{
	const list_t *list;
	char iata_srch[AIRPORTDB_IATA_LEN] = {};

	ASSERT(db != NULL);
	ASSERT(iata != NULL);

	strlcpy(iata_srch, iata, sizeof (iata_srch));
	list = htbl2_lookup_multi(&db->iata_index, iata_srch,
	    sizeof (iata_srch));
	if (list != NULL) {
		airport_lookup_htbl_multi(db, list, found_cb, userinfo);
		return (list_count(list));
	} else {
		return (0);
	}
}

/**
 * Legacy lookup function by ICAO ID. Airports without valid ICAO IDs
 * won't show up here. The search is also restricted to a narrow zone
 * around `pos' (within a 3x3 degree square). Use adb_airport_lookup_global()
 * for a search at any arbitrary location.
 */
airport_t *
adb_airport_lookup(airportdb_t *db, const char *icao, geo_pos2_t pos)
{
	ASSERT(db != NULL);
	ASSERT(icao != NULL);
	load_airports_in_tile(db, pos);
	return (apt_dat_lookup(db, icao));
}

static void
save_arpt_cb(airport_t *airport, void *userinfo)
{
	airport_t **out_arpt = userinfo;
	ASSERT(airport != NULL);
	*out_arpt = airport;
}

/**
 * Performs an airport lookup without having to know its approximate
 * location first. This is a legacy fallback version for the
 * airport_lookup_by_icao() function. Airports without valid ICAO IDs
 * won't show up here.
 *
 * @param db The airportdb on which to perform the lookup.
 * @param icao The ICAO code of the airport. If there are duplicate airports
 *	matching the ICAO code, which airport is returned cannot be predicted.
 *
 * @return The \ref airport structure of the airport, if found, or NULL if not.
 */
airport_t *
adb_airport_lookup_global(airportdb_t *db, const char *icao)
{
	airport_t *found = NULL;
	ASSERT(db != NULL);
	ASSERT(icao != NULL);
	(void)airport_lookup_by_icao(db, icao, save_arpt_cb, &found);
	return (found);
}

/**
 * Provides a method for walking the entire airport index in the database.
 * This is an entirely in-memory operation, so is relatively fast. However,
 * keep in mind that there are typically >30,000 airports in the index, so
 * don't do this too frequently.
 *
 * @param db Database to perform the index walk on.
 * @param found_cb Callback which will be called with each airport in the
 *	index. The `idx` argument is filled with the \ref arpt_index_t
 *	information about each airport found. The `userinfo` argument will
 *	be filled with whatever is provided in the userinfo argument to the
 *	adb_airport_index_walk() function.
 * @param found_cb You may provide a NULL value for this argument, which will
 *	not call any callbacks and can be simply used to grab the return
 *	value of this function.
 * @param userinfo Custom pointer user info argument, which will be passed
 *	to the `found_cb` callback function in its second argument.
 *
 * @return The total number of airport in the index. If you passed `NULL` for
 *	the found_cb argument, this will avoid walking the index entirely,
 *	simply returning the number of airports in the index.
 */
size_t
adb_airport_index_walk(const airportdb_t *db,
    void (*found_cb)(const arpt_index_t *idx, void *userinfo), void *userinfo)
{
	ASSERT(db != NULL);

	if (found_cb != NULL) {
		for (const arpt_index_t *idx = avl_first(&db->arpt_index);
		    idx != NULL; idx = AVL_NEXT(&db->arpt_index, idx)) {
			found_cb(idx, userinfo);
		}
	}

	return (avl_numnodes(&db->arpt_index));
}

/**
 * Performs a search in an airport for a runway matching a given runway ID
 * at one of its ends.
 *
 * @param arpt The airport in which to perform to search.
 * @param rwy_id A 0-leading, NUL-terminated runway ID. This runway ID
 *	is matched against a either end of all runways at the airport.
 *	Examples: "05", "09R", "25C", "36L".
 * @param rwy_p Mandatory return argument, which will be filled with
 *	a pointer to the matching \ref runway structure, if found.
 *	If not found, this will be set to `NULL`.
 * @param end_p Mandatory return argument, which will be filled with the
 *	index (0 or 1) of the matching \ref runway_end structure, if found.
 *	If not found, this will be left unchanged. This index points into
 *	the `ends` field in the returned \ref runway structure.
 *
 * @return `B_TRUE` if the search was successful, `B_FALSE` otherwise.
 */
bool_t
adb_airport_find_runway(airport_t *arpt, const char *rwy_id, runway_t **rwy_p,
    unsigned *end_p)
{
	ASSERT(arpt != NULL);
	ASSERT(rwy_id != NULL);
	ASSERT(rwy_p != NULL);
	*rwy_p = NULL;
	ASSERT(end_p != NULL);

	for (runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		for (unsigned i = 0; i < 2; i++) {
			if (strcmp(rwy->ends[i].id, rwy_id) == 0) {
				*rwy_p = rwy;
				*end_p = i;
				return (B_TRUE);
			}
		}
	}

	return (B_FALSE);
}

airport_t *
adb_matching_airport_in_tile_with_TATL(airportdb_t *db, geo_pos2_t pos,
    const char *search_icao)
{
	tile_t *tile;
	char const *search_cc;

	ASSERT(db != NULL);
	ASSERT(search_icao != NULL);
	search_cc = extract_icao_country_code(search_icao);

	load_airports_in_tile(db, pos);
	tile = geo_table_get_tile(db, pos, B_FALSE, NULL);
	if (tile == NULL)
		return (NULL);

	for (airport_t *arpt = avl_first(&tile->arpts); arpt != NULL;
	    arpt = AVL_NEXT(&tile->arpts, arpt)) {
		/*
		 * Because the passed in ICAO code might be invalid or of an
		 * unknown country, if that is the case and we can't extract
		 * the country code, we'll just try to do the best job we can
		 * and grab any airport in the tile with a TA/TL value.
		 */
		if ((arpt->TA != 0 || arpt->TL != 0) && (search_cc == NULL ||
		    search_cc == extract_icao_country_code(arpt->icao)))
			return (arpt);
	}

	return (NULL);
}
