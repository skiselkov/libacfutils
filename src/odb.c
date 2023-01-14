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

#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <curl/curl.h>

#include <junzip.h>

#include <acfutils/assert.h>
#include <acfutils/avl.h>
#include <acfutils/compress.h>
#include <acfutils/list.h>
#include <acfutils/odb.h>
#include <acfutils/perf.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/thread.h>

#include "chart_prov_common.h"

#define	DEFAULT_UNLOAD_DELAY	60		/* seconds */

#define	DL_TIMEOUT		300L		/* seconds */
#define	LOW_SPD_LIM		4096L		/* bytes/s */
#define	LOW_SPD_TIME		30L		/* seconds */

#define	REALLOC_STEP		(8 << 20)	/* 1 MiB */

#define	FAA_DOF_URL	"https://aeronav.faa.gov/Obst_Data/DAILY_DOF_CSV.ZIP"

enum {
	ODB_REGION_US,		/* USA */
	NUM_ODB_REGIONS
};

typedef struct {
	odb_t		*odb;
	uint8_t		*buf;
	size_t		bufsz;
	size_t		bufcap;
} dl_info_t;

typedef struct {
	obst_type_t	type;
	geo_pos3_t	pos;
	float		agl;
	obst_light_t	light;
	unsigned	quant;

	list_node_t	node;
} obst_t;

typedef struct {
	odb_t		*odb;
	int		lat, lon;
	time_t		access_t;
	list_t		obst;
	avl_node_t	node;
} odb_tile_t;

struct odb_s {
	char 		*cache_dir;
	char		*cainfo;
	unsigned	unload_delay;

	mutex_t		tiles_lock;
	avl_tree_t	tiles;

	mutex_t		refresh_lock;
	thread_t	refresh_thr;
	bool_t		refresh_run;

	time_t		refresh_times[NUM_ODB_REGIONS];

	mutex_t		proxy_lock;
	char		*proxy;
};

static void add_obst_to_odb(obst_type_t type, geo_pos3_t pos, float agl,
    obst_light_t light, unsigned quant, void *userinfo);
static odb_tile_t *load_tile(odb_t *odb, int lat, int lon,
    bool_t load_from_db);
static void odb_flush_tiles(odb_t *odb);

static int
tile_compar(const void *a, const void *b)
{
	const odb_tile_t *ta = a, *tb = b;

	if (ta->lat < tb->lat)
		return (-1);
	if (ta->lat > tb->lat)
		return (1);
	if (ta->lon < tb->lon)
		return (-1);
	if (ta->lon > tb->lon)
		return (1);

	return (0);
}

static void
latlon2path(int lat, int lon, char dname[32])
{
	ASSERT(dname != NULL);
	snprintf(dname, 32, "%+03d%+04d%c%+03d%+04d",
	    (int)floor(lat / 10.0) * 10, (int)floor(lon / 10.0) * 10,
	    DIRSEP, lat, lon);
}

static inline obst_type_t
dof2type(const char *type)
{
	if (strcmp(type, "BLDG") == 0)
		return (OBST_BLDG);
	if (strcmp(type, "TOWER") == 0 || strstr(type, "TWR") != NULL)
		return (OBST_TOWER);
	if (strcmp(type, "STACK") == 0)
		return (OBST_STACK);
	if (strcmp(type, "RIG") == 0)
		return (OBST_RIG);
	if (strstr(type, "POLE") != NULL)
		return (OBST_POLE);
	return (OBST_OTHER);
}

static inline obst_light_t
dof2light(const char *light)
{
	switch (light[0]) {
	case 'R':
		return (OBST_LIGHT_RED);
	case 'D':
		return (OBST_LIGHT_STROBE_WR_MED);
	case 'H':
		return (OBST_LIGHT_STROBE_WR_HI);
	case 'M':
		return (OBST_LIGHT_STROBE_W_MED);
	case 'S':
		return (OBST_LIGHT_STROBE_W_HI);
	case 'F':
		return (OBST_LIGHT_FLOOD);
	case 'C':
		return (OBST_LIGHT_DUAL_MED_CAT);
	case 'W':
		return (OBST_LIGHT_SYNC_RED);
	case 'L':
		return (OBST_LIGHT_LIGHTED);
	case 'N':
		return (OBST_LIGHT_NONE);
	default:
		return (OBST_LIGHT_UNK);
	}
}

static inline const char *
type2dof(obst_type_t type)
{
	switch (type) {
	case OBST_BLDG:
		return ("BLDG");
	case OBST_TOWER:
		return ("TOWER");
	case OBST_STACK:
		return ("STACK");
	case OBST_RIG:
		return ("RIG");
	case OBST_POLE:
		return ("POLE");
	default:
		return ("OTHER");
	}
}

static inline char
light2dof(obst_light_t light)
{
	switch (light) {
	case OBST_LIGHT_RED:
		return ('R');
	case OBST_LIGHT_STROBE_WR_MED:
		return ('D');
	case OBST_LIGHT_STROBE_WR_HI:
		return ('H');
	case OBST_LIGHT_STROBE_W_MED:
		return ('M');
	case OBST_LIGHT_STROBE_W_HI:
		return ('S');
	case OBST_LIGHT_FLOOD:
		return ('F');
	case OBST_LIGHT_DUAL_MED_CAT:
		return ('C');
	case OBST_LIGHT_SYNC_RED:
		return ('W');
	case OBST_LIGHT_LIGHTED:
		return ('L');
	case OBST_LIGHT_NONE:
		return ('N');
	default:
		return ('U');
	}
}

static bool_t
odb_proc_us_dof_impl(const char *buf, size_t len, add_obst_cb_t cb,
    void *userinfo)
{
	ASSERT(buf != NULL);
	ASSERT(cb != NULL);

	for (const char *line_start = buf, *line_end = buf;
	    line_start < buf + len; line_start = line_end + 1) {
		char **comps;
		size_t n_comps;
		obst_type_t type;
		obst_light_t light;
		geo_pos3_t pos;
		float agl, amsl;
		unsigned quant;
		char line[256];

		line_end = strchr(line_start, '\n');
		if (line_end == NULL)
			line_end = buf + len;
		lacf_strlcpy(line, line_start, MIN((int)sizeof (line),
		    (line_end - line_start) + 1));

		strip_space(line);

		comps = strsplit(line, ",", B_FALSE, &n_comps);

		if (n_comps < 19 || strcmp(comps[0], "OAS") == 0)
			goto next;

		quant = atoi(comps[10]);
		agl = FEET2MET(atof(comps[11]));
		amsl = FEET2MET(atof(comps[12]));
		type = dof2type(comps[9]);
		light = dof2light(comps[13]);
		pos.lat = atof(comps[5]);
		pos.lon = atof(comps[6]);
		pos.elev = amsl - agl;

		if (!is_valid_lat(pos.lat) || !is_valid_lon(pos.lon) ||
		    agl < 0 || !is_valid_alt_m(agl) || !is_valid_alt_m(amsl) ||
		    quant == 0)
			goto next;

		cb(type, pos, agl, light, quant, userinfo);
next:
		free_strlist(comps, n_comps);
	}

	return (B_TRUE);
}

static bool_t
odb_proc_us_dof(const char *path, add_obst_cb_t cb, void *userinfo)
{
	char *str;
	bool_t res;

	ASSERT(path != NULL);
	str = file2str(path, NULL);

	if (str == NULL)
		return (B_FALSE);
	res = odb_proc_us_dof_impl(str, strlen(str), cb, userinfo);
	LACF_DESTROY(str);

	return (res);
}

static void
free_tile(odb_tile_t *tile)
{
	obst_t *obst;

	ASSERT(tile != NULL);

	while ((obst = list_remove_head(&tile->obst)) != NULL)
		free(obst);
	list_destroy(&tile->obst);
	memset(tile, 0, sizeof (*tile));
	free(tile);
}

odb_t *
odb_init(const char *xpdir, const char *cainfo)
{
	odb_t *odb = safe_calloc(1, sizeof (*odb));

	ASSERT(xpdir != NULL);

	odb->cache_dir = mkpathname(xpdir, "Output", "caches",
	    "obstacle.db", NULL);
	if (cainfo != NULL)
		odb->cainfo = strdup(cainfo);
	odb->unload_delay = DEFAULT_UNLOAD_DELAY;

	mutex_init(&odb->tiles_lock);
	avl_create(&odb->tiles, tile_compar, sizeof (odb_tile_t),
	    offsetof(odb_tile_t, node));

	mutex_init(&odb->refresh_lock);

	mutex_init(&odb->proxy_lock);

	return (odb);
}

void
odb_fini(odb_t *odb)
{
	if (odb == NULL)
		return;

	if (odb->refresh_run) {
		odb->refresh_run = B_FALSE;
		thread_join(&odb->refresh_thr);
	}
	mutex_destroy(&odb->refresh_lock);

	mutex_enter(&odb->tiles_lock);
	odb_flush_tiles(odb);
	mutex_exit(&odb->tiles_lock);

	avl_destroy(&odb->tiles);
	mutex_destroy(&odb->tiles_lock);

	free(odb->proxy);
	mutex_destroy(&odb->proxy_lock);

	free(odb->cainfo);
	free(odb->cache_dir);
	memset(odb, 0, sizeof (*odb));
	free(odb);
}

void
odb_set_unload_delay(odb_t *odb, unsigned seconds)
{
	ASSERT(odb != NULL);
	odb->unload_delay = seconds;
}

time_t
odb_get_cc_refresh_date_impl(odb_t* odb, const char *cc)
{
	char *str;
	time_t res = 0;

	ASSERT(odb != NULL);
	ASSERT(cc != NULL);
	ASSERT_MSG(strlen(cc) == 2 && strchr(cc, DIRSEP) == NULL,
	    "invalid country code \"%s\" passed", cc);

	str = file2str(odb->cache_dir, cc, "refresh.txt", NULL);

	if (str != NULL) {
		res = atoll(str);
		lacf_free(str);
	}

	return (res);
}

time_t
odb_get_cc_refresh_date(odb_t *odb, const char *cc)
{
	ASSERT(odb != NULL);
	ASSERT(cc != NULL);

	if (strcmp(cc, "US") == 0) {
		if (odb->refresh_times[ODB_REGION_US] == 0) {
			odb->refresh_times[ODB_REGION_US] =
			    odb_get_cc_refresh_date_impl(odb, cc);
		}
		return odb->refresh_times[ODB_REGION_US];
	} else {
		return (0);
	}
}

static size_t
dl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	dl_info_t *dl_info;
	size_t bytes = size * nmemb;

	ASSERT(userdata != NULL);
	dl_info = userdata;

	/* Respond to an early termination request */
	if (!dl_info->odb->refresh_run)
		return (0);

	if (dl_info->bufcap < dl_info->bufsz + bytes) {
		do {
			dl_info->bufcap += REALLOC_STEP;
		} while (dl_info->bufcap < dl_info->bufsz + bytes);
		dl_info->buf = realloc(dl_info->buf, dl_info->bufcap);
	}
	memcpy(&dl_info->buf[dl_info->bufsz], ptr, bytes);
	dl_info->bufsz += bytes;

	return (bytes);
}

static bool_t
write_tile(odb_t *odb, odb_tile_t *tile, const char *cc)
{
	char subpath[32];
	char *path, *dirpath, *p;
	FILE *fp = NULL;
	bool_t res = B_FALSE;

	ASSERT(odb != NULL);
	ASSERT(tile != NULL);
	ASSERT(cc != NULL);

	latlon2path(tile->lat, tile->lon, subpath);
	path = mkpathname(odb->cache_dir, cc, subpath, NULL);
	dirpath = strdup(path);
	VERIFY(dirpath != NULL);

	/* strip the last path component to create the directory */
	p = strrchr(dirpath, DIRSEP);
	ASSERT(p != NULL);
	*p = 0;

	if (!create_directory_recursive(dirpath))
		goto errout;
	fp = fopen(path, "wb");
	if (fp == NULL) {
		logMsg("Error writing obstacle database tile %s: %s",
		    path, strerror (errno));
		goto errout;
	}
	for (obst_t *obst = list_head(&tile->obst); obst != NULL;
	    obst = list_next(&tile->obst, obst)) {
		fprintf(fp, ",,US,,,%f,%f,,,%s,%d,%.0f,%.0f,%c,1A,,,,\n",
		    obst->pos.lat, obst->pos.lon, type2dof(obst->type),
		    obst->quant, MET2FEET(obst->agl),
		    MET2FEET(obst->pos.elev + obst->agl),
		    light2dof(obst->light));
	}

	res = B_TRUE;
errout:
	if (fp != NULL)
		fclose(fp);
	lacf_free(path);
	free(dirpath);

	return (res);
}

static void
odb_write_tiles(odb_t *odb, const char *cc)
{
	ASSERT(odb != NULL);
	ASSERT_MUTEX_HELD(&odb->tiles_lock);
	ASSERT(cc != NULL);

	for (odb_tile_t *tile = avl_first(&odb->tiles); tile != NULL;
	    tile = AVL_NEXT(&odb->tiles, tile)) {
		if (!write_tile(odb, tile, cc))
			break;
	}
}

static void
odb_flush_tiles(odb_t *odb)
{
	void *cookie = NULL;
	odb_tile_t *tile;

	ASSERT(odb != NULL);
	ASSERT_MUTEX_HELD(&odb->tiles_lock);

	while ((tile = avl_destroy_nodes(&odb->tiles, &cookie)) != NULL)
		free_tile(tile);
}

static void
write_odb_refresh_date(odb_t *odb, const char *cc)
{
	char *path;
	FILE *fp;
	time_t now = time(NULL);

	ASSERT(odb != NULL);
	ASSERT(odb->cache_dir != NULL);
	ASSERT(cc != NULL);

	path = mkpathname(odb->cache_dir, cc, "refresh.txt", NULL);
	fp = fopen(path, "wb");
	if (fp == NULL) {
		logMsg("Error writing obstacle database refresh file %s: %s",
		    path, strerror(errno));
		goto errout;
	}
	fprintf(fp, "%ld\n", (long)now);
	fclose(fp);
errout:
	LACF_DESTROY(path);
}

static void
odb_refresh_us(odb_t *odb)
{
	CURL *curl;
	CURLcode res;
	dl_info_t dl_info = { .odb = odb };

	thread_set_name("odb-refresh-us");

	ASSERT(odb != NULL);

	curl = curl_easy_init();
	VERIFY(curl != NULL);

	logMsg("Downloading new obstacle data from \"%s\" for region \"US\"",
	    FAA_DOF_URL);
	curl_easy_setopt(curl, CURLOPT_URL, FAA_DOF_URL);
	chart_setup_curl(curl, odb->cainfo);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dl_info);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPD_TIME);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPD_LIM);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, DL_TIMEOUT);
	mutex_enter(&odb->proxy_lock);
	if (odb->proxy != NULL)
		curl_easy_setopt(curl, CURLOPT_PROXY, odb->proxy);
	mutex_exit(&odb->proxy_lock);

	res = curl_easy_perform(curl);

	if (res == CURLE_OK && dl_info.bufsz != 0) {
		size_t len;
		void *buf = decompress_zip(dl_info.buf, dl_info.bufsz, &len);

		if (buf != NULL) {
			char *subpath = mkpathname(odb->cache_dir, "US", NULL);

			/*
			 * The downloaded DOF is HUUUGE, so DON'T lock here.
			 * add_obst_to_odb will lock the database when it's
			 * ready to add a single obstacle instead.
			 */
			odb_proc_us_dof_impl(buf, len, add_obst_to_odb, odb);

			mutex_enter(&odb->tiles_lock);

			if (file_exists(subpath, NULL))
				remove_directory(subpath);
			create_directory_recursive(odb->cache_dir);
			odb_write_tiles(odb, "US");
			odb_flush_tiles(odb);
			write_odb_refresh_date(odb, "US");

			lacf_free(subpath);
			odb->refresh_times[ODB_REGION_US] = time(NULL);

			mutex_exit(&odb->tiles_lock);
		} else {
			logMsg("Error updating obstacle database from %s: "
			    "failed to decompress downloaded ZIP file",
			    FAA_DOF_URL);
			odb->refresh_times[ODB_REGION_US] = -1u;
		}
		free(buf);
	} else {
		logMsg("Error updating obstacle database from %s: %s",
		    FAA_DOF_URL, curl_easy_strerror(res));
		odb->refresh_times[ODB_REGION_US] = -1u;
	}

	curl_easy_cleanup(curl);
	free(dl_info.buf);

	mutex_enter(&odb->refresh_lock);
	odb->refresh_run = B_FALSE;
	mutex_exit(&odb->refresh_lock);
}

bool_t
odb_refresh_cc(odb_t *odb, const char *cc)
{
	void (*refresh_op)(odb_t *odb) = NULL;

	ASSERT(odb != NULL);
	ASSERT(cc != NULL);

	mutex_enter(&odb->refresh_lock);
	if (!odb->refresh_run) {
		if (strcmp(cc, "US") == 0)
			refresh_op = odb_refresh_us;
		if (refresh_op != NULL) {
			odb->refresh_run = B_TRUE;
			VERIFY(thread_create(&odb->refresh_thr,
			    (void (*)(void *))refresh_op, odb));
		}
	}
	mutex_exit(&odb->refresh_lock);

	return (B_TRUE);
}

static void
add_tile_obst(obst_type_t type, geo_pos3_t pos, float agl,
    obst_light_t light, unsigned quant, void *userinfo)
{
	odb_tile_t *tile;
	obst_t *obst = safe_calloc(1, sizeof (*obst));

	ASSERT(userinfo != NULL);
	tile = userinfo;
	ASSERT(tile->odb != NULL);
	ASSERT_MUTEX_HELD(&tile->odb->tiles_lock);

	obst->type = type;
	obst->pos = pos;
	obst->agl = agl;
	obst->light = light;
	obst->quant = quant;

	list_insert_tail(&tile->obst, obst);
}

static void
add_obst_to_odb(obst_type_t type, geo_pos3_t pos, float agl,
    obst_light_t light, unsigned quant, void *userinfo)
{
	odb_t *odb;
	odb_tile_t *tile;

	ASSERT(userinfo != NULL);
	odb = userinfo;

	mutex_enter(&odb->tiles_lock);
	tile = load_tile(odb, floor(pos.lat), floor(pos.lon), B_FALSE);
	add_tile_obst(type, pos, agl, light, quant, tile);
	mutex_exit(&odb->tiles_lock);
}

static void
odb_populate_tile_us(odb_t *odb, odb_tile_t *tile)
{
	char tilepath[32];
	char *path;

	ASSERT(odb != NULL);
	ASSERT(tile != NULL);

	latlon2path(tile->lat, tile->lon, tilepath);
	path = mkpathname(odb->cache_dir, "US", tilepath, NULL);
	odb_proc_us_dof(path, add_tile_obst, tile);
	lacf_free(path);
}

static void
odb_populate_tile(odb_t *odb, odb_tile_t *tile)
{
	ASSERT(odb != NULL);
	ASSERT(tile != NULL);
	odb_populate_tile_us(odb, tile);
}

static odb_tile_t *
load_tile(odb_t *odb, int lat, int lon, bool_t load_from_db)
{
	odb_tile_t *tile;
	odb_tile_t srch = { .lat = lat, .lon = lon };
	avl_index_t where;

	ASSERT(odb != NULL);
	ASSERT_MUTEX_HELD(&odb->tiles_lock);

	tile = avl_find(&odb->tiles, &srch, &where);
	if (tile == NULL) {
		tile = safe_calloc(1, sizeof (*tile));
		tile->odb = odb;
		tile->lat = lat;
		tile->lon = lon;
		list_create(&tile->obst, sizeof (obst_t),
		    offsetof(obst_t, node));

		if (load_from_db)
			odb_populate_tile(odb, tile);
		avl_insert(&odb->tiles, tile, where);
	}
	if (load_from_db)
		tile->access_t = time(NULL);

	return (tile);
}

bool_t
odb_get_obstacles(odb_t *odb, int lat, int lon, add_obst_cb_t cb,
    void *userinfo)
{
	odb_tile_t *tile;

	ASSERT(odb != NULL);
	ASSERT(cb != NULL);

	mutex_enter(&odb->tiles_lock);

	tile = load_tile(odb, lat, lon, B_TRUE);
	for (obst_t *obst = list_head(&tile->obst); obst != NULL;
	    obst = list_next(&tile->obst, obst)) {
		cb(obst->type, obst->pos, obst->agl, obst->light, obst->quant,
		    userinfo);
	}

	mutex_exit(&odb->tiles_lock);

	return (B_TRUE);
}

void
odb_set_proxy(odb_t *odb, const char *proxy)
{
	ASSERT(odb != NULL);

	mutex_enter(&odb->proxy_lock);
	LACF_DESTROY(odb->proxy);
	if (proxy != NULL)
		odb->proxy = safe_strdup(proxy);
	mutex_exit(&odb->proxy_lock);
}

size_t
odb_get_proxy(odb_t *odb, char *proxy, size_t cap)
{
	size_t len;

	ASSERT(odb != NULL);
	ASSERT(proxy != NULL || cap == 0);

	mutex_enter(&odb->proxy_lock);
	if (odb->proxy != NULL) {
		lacf_strlcpy(proxy, odb->proxy, cap);
		len = strlen(odb->proxy) + 1;
	} else {
		lacf_strlcpy(proxy, "", cap);
		len = 0;
	}
	mutex_exit(&odb->proxy_lock);

	return (len);
}
