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

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "acfutils/assert.h"
#include "acfutils/avl.h"
#include "acfutils/chartdb.h"
#include "acfutils/helpers.h"
#include "acfutils/list.h"
#include "acfutils/png.h"
#include "acfutils/thread.h"
#include "acfutils/worker.h"

#include "chartdb_impl.h"
#include "chart_prov_faa.h"

static chart_prov_t prov[NUM_PROVIDERS] = {
    {
	.name = "aeronav.faa.gov",
	.init = chart_faa_init,
	.fini = chart_faa_fini,
	.get_chart = chart_faa_get_chart
    }
};

static chart_t loader_cmd_purge = { NULL };

static int
chart_name_compar(const void *a, const void *b)
{
	const chart_t *ca = a, *cb = b;
	int res = strcmp(ca->name, cb->name);

	if (res < 0)
		return (-1);
	if (res == 0)
		return (0);
	return (1);
}

static int
arpt_compar(const void *a, const void *b)
{
	const chart_arpt_t *ca = a, *cb = b;
	int res = strcmp(ca->icao, cb->icao);

	if (res < 0)
		return (-1);
	if (res == 0)
		return (0);
	return (1);
}

static void
chart_destroy(chart_t *chart)
{
	free(chart->pixels);
	free(chart);
}

static void
arpt_destroy(chart_arpt_t *arpt)
{
	void *cookie;
	chart_t *chart;

	cookie = NULL;
	while ((chart = avl_destroy_nodes(&arpt->charts, &cookie)) != NULL)
		chart_destroy(chart);
	avl_destroy(&arpt->charts);

	free(arpt);
}

static bool_t
loader_init(void *userinfo)
{
	chartdb_t *cdb = userinfo;

	ASSERT(cdb != NULL);
	ASSERT3U(cdb->prov, <, NUM_PROVIDERS);

	if (!prov[cdb->prov].init(cdb))
		return (B_FALSE);

	mutex_enter(&cdb->lock);
	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

static void
loader_purge(chartdb_t *cdb)
{
	for (chart_arpt_t *arpt = avl_first(&cdb->arpts); arpt != NULL;
	    arpt = AVL_NEXT(&cdb->arpts, arpt)) {
		for (chart_t *chart = avl_first(&arpt->charts); chart != NULL;
		    chart = AVL_NEXT(&arpt->charts, chart)) {
			if (chart->refcnt == 0) {
				free(chart->pixels);
				chart->pixels = NULL;
				chart->zoom = 0;
				chart->width = 0;
				chart->height = 0;
			}
		}
	}
}

chart_arpt_t *
chartdb_add_arpt(chartdb_t *cdb, const char *icao)
{
	chart_arpt_t *arpt, srch;
	avl_index_t where;

	ASSERT(cdb != NULL);

	strlcpy(srch.icao, icao, sizeof (srch.icao));

	mutex_enter(&cdb->lock);
	arpt = avl_find(&cdb->arpts, &srch, &where);
	if (arpt == NULL) {
		arpt = calloc(1, sizeof (*arpt));
		avl_create(&arpt->charts, chart_name_compar, sizeof (chart_t),
		    offsetof(chart_t, node));
		strlcpy(arpt->icao, icao, sizeof (arpt->icao));
		arpt->db = cdb;
		avl_insert(&cdb->arpts, arpt, where);
	}

	mutex_exit(&cdb->lock);

	return (arpt);
}

bool_t
chartdb_add_chart(chart_arpt_t *arpt, chart_t *chart)
{
	avl_index_t where;
	chartdb_t *cdb = arpt->db;

	ASSERT(cdb != NULL);

	mutex_enter(&cdb->lock);
	if (avl_find(&arpt->charts, chart, &where) != NULL) {
		mutex_exit(&cdb->lock);
		return (B_FALSE);
	}
	avl_insert(&arpt->charts, chart, where);
	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

char *
chartdb_mkpath(chart_t *chart)
{
	chart_arpt_t *arpt = chart->arpt;
	chartdb_t *cdb;
	char airac_nr[8];

	ASSERT(arpt != NULL);
	cdb = arpt->db;
	ASSERT(cdb != NULL);

	snprintf(airac_nr, sizeof (airac_nr), "%d", cdb->airac);
	return (mkpathname(cdb->path, prov[cdb->prov].name, airac_nr,
	    arpt->icao, chart->filename, NULL));
}

static char *
pdf_convert(const char *imagemagick_path, char *old_path, double zoom)
{
	char *ext, *new_path;
	char cmd[3 * MAX_PATH];

	new_path = strdup(old_path);
	ext = strrchr(new_path, '.');
	VERIFY(ext != NULL);
	strlcpy(&ext[1], "png", strlen(&ext[1]) + 1);

	snprintf(cmd, sizeof (cmd), "\"%s\" -flatten -quality 20 -density %d "
	    "\"%s\" \"%s\"", imagemagick_path, (int)(100 * zoom), old_path,
	    new_path);
	if (system(cmd) != 0) {
		logMsg("Error converting chart %s to PNG: %s", old_path,
		    strerror(errno));
		free(new_path);
		free(old_path);
		return (NULL);
	}
	free(old_path);

	return (new_path);
}

static void
loader_load(chartdb_t *cdb, chart_t *chart)
{
	char *path;
	char *ext;
	uint8_t *buf;
	int w, h;

	if (!prov[cdb->prov].get_chart(chart))
		return;

	path = chartdb_mkpath(chart);
	ext = strrchr(path, '.');
	if (ext != NULL &&
	    (strcmp(&ext[1], "pdf") == 0 || strcmp(&ext[1], "PDF") == 0)) {
		path = pdf_convert(cdb->imagemagick_path, path, chart->zoom);
		if (path == NULL)
			goto out;
	}

	buf = png_load_from_file_rgba(path, &w, &h);
	if (buf != NULL) {
		mutex_enter(&cdb->lock);

		while (chart->refcnt != 0)
			cv_wait(&chart->refcnt_cv, &cdb->lock);
		free(chart->pixels);
		chart->pixels = buf;
		chart->width = w;
		chart->height = h;

		mutex_exit(&cdb->lock);
	}

out:
	free(path);
}

static bool_t
loader(void *userinfo)
{
	chartdb_t *cdb = userinfo;
	chart_t *chart;

	mutex_enter(&cdb->lock);
	while ((chart = list_remove_head(&cdb->loader_queue)) != NULL) {
		if (chart == &loader_cmd_purge) {
			loader_purge(cdb);
		} else {
			mutex_exit(&cdb->lock);
			loader_load(cdb, chart);
			mutex_enter(&cdb->lock);
		}
	}
	mutex_exit(&cdb->lock);

	return (B_TRUE);
}

static void
loader_fini(void *userinfo)
{
	chartdb_t *cdb = userinfo;

	ASSERT(cdb != NULL);
	ASSERT3U(cdb->prov, <, NUM_PROVIDERS);
	prov[cdb->prov].fini(cdb);
}

API_EXPORT chartdb_t *
chartdb_init(const char *cache_path, const char *imagemagick_path,
    unsigned airac, const char *provider_name, void *provider_info)
{
	chartdb_t *cdb;
	chart_prov_id_t pid;

	for (pid = 0; pid < NUM_PROVIDERS; pid++) {
		if (strcmp(provider_name, prov[pid].name) == 0)
			break;
	}
	if (pid >= NUM_PROVIDERS)
		return (NULL);

	cdb = calloc(1, sizeof (*cdb));
	mutex_init(&cdb->lock);
	avl_create(&cdb->arpts, arpt_compar, sizeof (chart_arpt_t),
	    offsetof(chart_arpt_t, node));
	cdb->path = strdup(cache_path);
	cdb->imagemagick_path = strdup(imagemagick_path);
	cdb->airac = airac;
	cdb->prov_info = provider_info;
	strlcpy(cdb->prov_name, provider_name, sizeof (cdb->prov_name));

	worker_init2(&cdb->loader, loader_init, loader, loader_fini, 0, cdb,
	    "chartdb");

	return (cdb);
}

API_EXPORT
void chartdb_fini(chartdb_t *cdb)
{
	void *cookie;
	chart_arpt_t *arpt;

	worker_fini(&cdb->loader);

	cookie = NULL;
	while ((arpt = avl_destroy_nodes(&cdb->arpts, &cookie)) != NULL)
		arpt_destroy(arpt);
	avl_destroy(&cdb->arpts);
	mutex_destroy(&cdb->lock);

	free(cdb->path);
	free(cdb->imagemagick_path);
	free(cdb);
}

API_EXPORT void
chartdb_purge(chartdb_t *cdb)
{
	mutex_enter(&cdb->lock);

	/* purge the queue */
	while (list_remove_head(&cdb->loader_queue) != NULL)
		;
	list_insert_tail(&cdb->loader_queue, &loader_cmd_purge);
	worker_wake_up(&cdb->loader);

	mutex_exit(&cdb->lock);
}

API_EXPORT char **
chartdb_get_chart_names(chartdb_t *cdb, const char *icao, chart_type_t type,
    size_t *num_charts)
{
	chart_arpt_t srch, *arpt;
	avl_index_t where;
	char **charts;
	chart_t *chart;
	size_t i, num;

	mutex_enter(&cdb->lock);

	strlcpy(srch.icao, icao, sizeof (srch.icao));
	arpt = avl_find(&cdb->arpts, &srch, &where);

	if (arpt == NULL) {
		mutex_exit(&cdb->lock);
		*num_charts = 0;
		return (NULL);
	}

	for (chart = avl_first(&arpt->charts), num = 0; chart != NULL;
	    chart = AVL_NEXT(&arpt->charts, chart)) {
		if (chart->type == type)
			num++;
	}
	charts = calloc(num, sizeof (*charts));
	for (chart = avl_first(&arpt->charts), i = 0; chart != NULL;
	    chart = AVL_NEXT(&arpt->charts, chart)) {
		if (chart->type == type) {
			ASSERT3U(i, <, num);
			charts[i] = strdup(chart->name);
			i++;
		}
	}

	mutex_exit(&cdb->lock);

	*num_charts = num;

	return (charts);
}

API_EXPORT void
chartdb_get_chart_codename(chartdb_t *cdb, const char *icao,
    const char *chartname, char codename[32])
{
	chart_arpt_t *arpt, srch_arpt;
	chart_t *chart, srch_chart;

	mutex_enter(&cdb->lock);

	strlcpy(srch_arpt.icao, icao, sizeof (srch_arpt.icao));
	arpt = avl_find(&cdb->arpts, &srch_arpt, NULL);
	VERIFY(arpt != NULL);
	strlcpy(srch_chart.name, chartname, sizeof (srch_chart.name));
	chart = avl_find(&arpt->charts, &srch_chart, NULL);
	VERIFY(chart != NULL);
	strlcpy(codename, chart->codename, 32);

	mutex_exit(&cdb->lock);
}

API_EXPORT void
chartdb_free_str_list(char **l, size_t num)
{
	free_strlist(l, num);
}

static chart_t *
chart_find(chartdb_t *cdb, const char *icao,
    const char *chart_name)
{
	chart_arpt_t *arpt, srch_arpt;
	chart_t srch_chart;

	strlcpy(srch_arpt.icao, icao, sizeof (srch_arpt.icao));
	arpt = avl_find(&cdb->arpts, &srch_arpt, NULL);
	if (arpt == NULL)
		return (NULL);
	strlcpy(srch_chart.name, chart_name, sizeof (srch_chart.name));

	return (avl_find(&arpt->charts, &srch_chart, NULL));
}

API_EXPORT uint8_t *
chartdb_get_chart_image(chartdb_t *cdb, const char *icao,
    const char *chart_name, double zoom, double rotate,
    unsigned *width, unsigned *height)
{
	chart_t *chart;

	mutex_enter(&cdb->lock);

	chart = chart_find(cdb, icao, chart_name);
	if (chart == NULL) {
		mutex_exit(&cdb->lock);
		return (NULL);
	}

	if ((chart->pixels == NULL || chart->zoom != zoom ||
	    chart->rotate != rotate) &&
	    !list_link_active(&chart->loader_node)) {
		chart->zoom = zoom;
		chart->rotate = rotate;
		list_insert_tail(&cdb->loader_queue, chart);
		worker_wake_up(&cdb->loader);
	}

	if (chart->pixels == NULL) {
		mutex_exit(&cdb->lock);
		return (NULL);
	}

	chart->refcnt++;

	mutex_exit(&cdb->lock);

	*width = chart->width;
	*height = chart->height;

	return (chart->pixels);
}

API_EXPORT void
chartdb_release_chart_image(chartdb_t *cdb, const char *icao,
    const char *chart_name)
{
	chart_t *chart;

	mutex_enter(&cdb->lock);

	chart = chart_find(cdb, icao, chart_name);
	VERIFY(chart != NULL);
	VERIFY3U(chart->refcnt, >, 0);

	chart->refcnt--;
	if (chart->refcnt == 0)
		cv_broadcast(&chart->refcnt_cv);

	mutex_exit(&cdb->lock);
}
