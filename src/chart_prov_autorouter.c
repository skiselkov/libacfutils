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

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#if	!IBM
#include <sys/stat.h>
#endif	/* !IBM */

#include "acfutils/avl.h"
#include "acfutils/helpers.h"
#include "acfutils/thread.h"
#include "chartdb_impl.h"
#include "chart_prov_autorouter.h"
#include "chart_prov_common.h"

#define	BASE_URL	"https://www.autorouter.aero"
#define	INDEX_URL_PATH	"/webdav/"
#define	INDEX_VERSION	1

static char *
mk_index_path(chartdb_t *cdb)
{
	char airac_nr[8];

	snprintf(airac_nr, sizeof (airac_nr), "%d", cdb->airac);
	return (mkpathname(cdb->path, cdb->prov_name, airac_nr, "index.xml",
	    NULL));
}

static char *
mk_country_cache_path(chartdb_t *cdb, const char *country_name)
{
	char airac_nr[8];
	char fname[strlen(country_name) + 8];

	snprintf(airac_nr, sizeof (airac_nr), "%d", cdb->airac);
	snprintf(fname, sizeof (fname), "%s.xml", country_name);
	return (mkpathname(cdb->path, cdb->prov_name, airac_nr, fname, NULL));
}

static char *
mk_arpt_cache_path(chartdb_t *cdb, const char *icao, const char *category)
{
	char airac_nr[8];
	char fname[64];

	snprintf(airac_nr, sizeof (airac_nr), "%d", cdb->airac);
	if (category == NULL)
		category = "index";
	snprintf(fname, sizeof (fname), "%s.xml", category);
	return (mkpathname(cdb->path, cdb->prov_name, airac_nr, icao,
	    fname, NULL));
}

static bool_t
webdav_foreach_dirlist(chartdb_t *cdb, CURL *curl, const char *path,
    const char *cachefile,
    bool_t (*cb)(chartdb_t *cdb, CURL *curl, const char *href))
{
	chart_dl_info_t dl = { .cdb = NULL };
	const chart_prov_info_login_t *login = cdb->prov_login;
	xmlDoc *doc = NULL;
	xmlXPathContext *xpath_ctx = NULL;
	xmlXPathObject *xpath_obj = NULL;
	char url[256];

	snprintf(url, sizeof(url), "%s%s", BASE_URL, path);

	if (!file_exists(cachefile, NULL) ||
	    (doc = xmlParseFile(cachefile)) == NULL) {
		bool_t result = chart_download_multi(&curl, cdb, url,
		    cachefile, "PROPFIND", login, -1,
		    "Error downloading chart index", &dl);
		if (result == B_FALSE || dl.buf == NULL)
			goto errout;
		doc = xmlParseMemory((char *)dl.buf, dl.bufsz);
		if (doc == NULL) {
			logMsg("Error parsing chart index: "
			    "malformed WebDAV XML response from server");
			goto errout;
		}
	}
	xpath_ctx = xmlXPathNewContext(doc);
	if (xpath_ctx == NULL) {
		logMsg("Error parsing chart index: error creating XPath "
		    "context for XML");
		goto errout;
	};
	VERIFY0(xmlXPathRegisterNs(xpath_ctx, (xmlChar *)"d",
	    (xmlChar *)"DAV:"));
	xpath_obj = xmlXPathEvalExpression(
	    (xmlChar *)"/d:multistatus/d:response/d:href", xpath_ctx);
	if (xpath_obj == NULL || xpath_obj->nodesetval->nodeNr < 1) {
		logMsg("Error parsing chart index: malformed WebDAV dirlist "
		    "%s %p.", path, xpath_obj);
		goto errout;
	}
	for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
		const xmlNode *href = xpath_obj->nodesetval->nodeTab[i];

		if (href->children == NULL || href->children->content == NULL) {
			logMsg("Error parsing chart index: directory list "
			    "entry was missing HREF content.");
			goto errout;
		}
		/* Skip references back to the index */
		if (strcmp((char *)href->children->content, path) == 0)
			continue;
		if (!cb(cdb, curl, (const char *)href->children->content)) {
			goto errout;
		}
	}

	xmlXPathFreeObject(xpath_obj);
	xmlXPathFreeContext(xpath_ctx);
	xmlFreeDoc(doc);
	free(dl.buf);

	return (B_TRUE);
errout:
	if (xpath_obj != NULL)
		xmlXPathFreeObject(xpath_obj);
	if (xpath_ctx != NULL)
		xmlXPathFreeContext(xpath_ctx);
	if (doc != NULL)
		xmlFreeDoc(doc);
	free(dl.buf);
	return (B_FALSE);
}

static void
chart_name_process(char *name)
{
	const char *subst[] = {
	    "STANDARD DEPARTURE CHART - INSTRUMENT", "",
	    "STANDARD ARRIVAL CHART - INSTRUMENT", "",
	    "DEPARTURE CHART - INSTRUMENT", "",
	    "ARRIVAL CHART - INSTRUMENT", "",
	    "DEPARTURE CHART", "",
	    "ARRIVAL CHART", "",
	    "INSTRUMENT APPROACH CHART", "",
	    "ICAO", "",
	    "IAC", "",
	    "-", "",
	    ".PDF", "",
	    "ARRIVAL CHART", "",
	    "AERODROME", "AD",
	    "TERRAIN", "TERR",
	    "OBSTACLE", "OBST",
	    "TRANSITION", "TRANS",
	    "NOISE ABATEMENT", "NOISE ABTMT",
	    "PRECISION APPROACH", "PRECISION APP",
	    "STANDARD DEPARTURE ROUTES - INSTRUMENT", "",
	    "STANDARD ARRIVAL ROUTES - INSTRUMENT", "",
	    "STANDARD DEPARTURE ROUTES INSTRUMENT", "",
	    "STANDARD ARRIVAL ROUTES INSTRUMENT", "",
	    "FINAL APCH", "FINAL APP",
	    "FINAL APPROACH", "FINAL APP",
	    "(SID)", "",
	    "(STAR)", "",
	    NULL	/* list terminator */
	};

	strtoupper(name);
	word_subst(name, subst);
	/* Kill any leading or trailing whitespace */
	strip_space(name);
	/*
	 * Kill double spaces that might have been generated as a result
	 * of the word removal.
	 */
	for (unsigned i = 0, n = strlen(name); i + 1 < n;) {
		if (isspace(name[i]) && isspace(name[i + 1])) {
			memmove(&name[i], &name[i + 1], strlen(&name[i]));
			n--;
		} else {
			i++;
		}
	}
}

static bool_t
parse_chart(chartdb_t *cdb, CURL *curl, const char *path)
{
	char **comps;
	size_t n_comps;
	char icao[8];
	char *filename, *country, *arpt_name, *typename, *chartname;
	chart_type_t chart_type;
	char common_prefix[16];
	chart_arpt_t *arpt;
	chart_t *chart;

	UNUSED(curl);

	comps = strsplit(path, "/", B_TRUE, &n_comps);
	if (n_comps < 5)
		goto out;

	unescape_percent(comps[1]);
	country = comps[1];

	unescape_percent(comps[2]);
	if (strlen(comps[2]) < 8)
		goto out;
	lacf_strlcpy(icao, comps[2], 5);	/* first 4 chars is ICAO */
	arpt_name = &(comps[2][7]);

	unescape_percent(comps[3]);
	typename = comps[3];
	if (strcmp(typename, "Airport") == 0 || strcmp(typename, "VFR") == 0)
		chart_type = CHART_TYPE_APD;
	else if (strcmp(typename, "Arrival") == 0)
		chart_type = CHART_TYPE_STAR;
	else if (strcmp(typename, "Approach") == 0)
		chart_type = CHART_TYPE_IAP;
	else if (strcmp(typename, "Departure") == 0)
		chart_type = CHART_TYPE_DP;
	else
		goto out;

	/* Use the unescaped version as the on-disk filename */
	filename = strdup(comps[4]);

	snprintf(common_prefix, sizeof (common_prefix), "AD 2 %s ", icao);
	unescape_percent(comps[4]);
	chartname = comps[4];
	if (strncmp(chartname, common_prefix, strlen(common_prefix)) == 0) {
		int l = strlen(common_prefix);
		memmove(chartname, &chartname[l], strlen(chartname) - l + 1);
	}
	chart_name_process(chartname);

	arpt = chartdb_add_arpt(cdb, icao, arpt_name, country, "");
	chart = safe_calloc(1, sizeof (*chart));
	chart->name = strdup(chartname);
	chart->codename = strdup(path);
	chart->type = chart_type;
	chart->filename = filename;
	if (!chartdb_add_chart(arpt, chart)) {
		free(chart->name);
		free(chart->codename);
		free(chart->filename);
		free(chart);
	}
out:
	free_strlist(comps, n_comps);
	return (B_TRUE);
}

static bool_t
parse_category(chartdb_t *cdb, CURL *curl, const char *path)
{
	bool_t result;
	char *cachefile;
	char **comps;
	size_t n_comps;
	char icao[8];

	comps = strsplit(path, "/", B_TRUE, &n_comps);
	if (n_comps < 4) {
		logMsg("Malformed chart category dir listing: %s", path);
		free_strlist(comps, n_comps);
		return (B_FALSE);
	}
	unescape_percent(comps[2]);
	if (strlen(comps[2]) < 8) {
		logMsg("Malformed chart category dir listing: %s", path);
		free_strlist(comps, n_comps);
		return (B_FALSE);
	}
	lacf_strlcpy(icao, comps[2], 5);	/* first 4 chars is ICAO */
	unescape_percent(comps[3]);

	cachefile = mk_arpt_cache_path(cdb, icao, comps[3]);
	result = webdav_foreach_dirlist(cdb, curl, path, cachefile,
	    parse_chart);
	free(cachefile);
	free_strlist(comps, n_comps);

	return (result);
}

static bool_t
parse_airport(chartdb_t *cdb, CURL *curl, const char *path)
{
	/* Initial lazyload */
	char **comps;
	size_t n_comps;
	char icao[8];
	char *arpt_name, *country;
	chart_arpt_t *arpt;

	UNUSED(curl);

	comps = strsplit(path, "/", B_TRUE, &n_comps);
	if (n_comps < 3)
		goto out;

	unescape_percent(comps[1]);
	country = comps[1];
	unescape_percent(comps[2]);
	if (strlen(comps[2]) < 8)
		goto out;
	lacf_strlcpy(icao, comps[2], 5);	/* first 4 chars is ICAO */
	arpt_name = &(comps[2][7]);
	arpt = chartdb_add_arpt(cdb, icao, arpt_name, country, "");
	arpt->codename = strdup(path);
out:
	free_strlist(comps, n_comps);
	return (B_TRUE);
}

static bool_t
parse_country(chartdb_t *cdb, CURL *curl, const char *path)
{
	bool_t result;
	char *cachefile;
	char **comps;
	size_t n_comps;

	comps = strsplit(path, "/", B_TRUE, &n_comps);
	if (n_comps < 2) {
		logMsg("Malformed country index \"%s\" in response from server",
		    path);
		free_strlist(comps, n_comps);
		return (B_FALSE);
	}
	cachefile = mk_country_cache_path(cdb, comps[1]);
	result = webdav_foreach_dirlist(cdb, curl, path, cachefile,
	    parse_airport);
	free_strlist(comps, n_comps);

	return (result);
}

static bool_t
update_index(chartdb_t *cdb)
{
	CURL *curl = NULL;
	char *cachefile = mk_index_path(cdb);
	bool_t result;

	result = webdav_foreach_dirlist(cdb, curl, INDEX_URL_PATH, cachefile,
	    parse_country);
	if (curl != NULL)
		curl_easy_cleanup(curl);
	free(cachefile);

	return (result);
}

bool_t
chart_autorouter_init(chartdb_t *cdb)
{
	const chart_prov_info_login_t *login = cdb->prov_login;

	VERIFY(cdb->prov_login != NULL);
	VERIFY(login->username != NULL);
	VERIFY(login->password != NULL);

	if (!update_index(cdb))
		goto errout;

	return (B_TRUE);

errout:
	chart_autorouter_fini(cdb);
	return (B_FALSE);
}

void
chart_autorouter_fini(chartdb_t *cdb)
{
	UNUSED(cdb);
}

bool_t
chart_autorouter_get_chart(chart_t *chart)
{
	chartdb_t *cdb;
	chart_arpt_t *arpt;
	char *filepath;
	bool_t result;
	char url[512];
	CURL *curl = NULL;
	const chart_prov_info_login_t *login;

	ASSERT(chart->arpt != NULL);
	arpt = chart->arpt;
	ASSERT(arpt->db != NULL);
	cdb = arpt->db;
	login = cdb->prov_login;
	ASSERT(login != NULL);

	filepath = chartdb_mkpath(chart);
	snprintf(url, sizeof (url), "%s%s", BASE_URL, chart->codename);
	result = chart_download_multi(&curl, cdb, url, filepath, NULL,
	    login, -1, "Error downloading chart index", NULL);
	if (!result && file_exists(filepath, NULL)) {
		logMsg("WARNING: failed to contact autorouter servers to "
		    "refresh chart \"%s\". However, we appear to still have "
		    "a locally cached copy of this chart available, so I "
		    "will display that one instead.", chart->codename);
		result = B_TRUE;
	}
	curl_easy_cleanup(curl);
	free(filepath);

	return (result);
}

void
chart_autorouter_arpt_lazyload(chart_arpt_t *arpt)
{
	CURL *curl = NULL;
	char *cachefile;

	cachefile = mk_arpt_cache_path(arpt->db, arpt->icao, NULL);
	webdav_foreach_dirlist(arpt->db, curl, arpt->codename, cachefile,
	    parse_category);
	if (curl != NULL)
		curl_easy_cleanup(curl);
	free(cachefile);
}

bool_t
chart_autorouter_test_conn(const chart_prov_info_login_t *creds,
    const char *proxy)
{
	CURL *curl = NULL;
	bool_t result = chart_download_multi2(&curl, proxy,
	    BASE_URL INDEX_URL_PATH, NULL, "PROPFIND", creds, 5,
	    "Error testing chart connection", NULL);
	if (curl != NULL)
		curl_easy_cleanup(curl);
	return (result);
}
