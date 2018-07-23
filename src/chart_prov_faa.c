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
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#if	!IBM
#include <sys/stat.h>
#endif	/* !IBM */

#include "acfutils/compress.h"
#include "acfutils/helpers.h"
#include "chart_prov_faa.h"

#define	SERVER_NAME	"https://aeronav.faa.gov"
#define	INDEX_URL	SERVER_NAME "/d-tpp/%d/xml_data/d-TPP_Metafile.xml"
#define	CHART_URL	SERVER_NAME "/d-tpp/%d/%s"

#define	REALLOC_STEP	(8 << 20)	/* bytes */
#define	MAX_DL_SIZE	(128 << 20)	/* bytes */
#define	DL_TIMEOUT	300L		/* seconds */
#define	LOW_SPD_LIM	50L		/* bytes/s */
#define	LOW_SPD_TIME	30L		/* seconds */

typedef struct {
	const char	*url;
	chartdb_t	*cdb;
	uint8_t		*buf;
	size_t		bufcap;
	size_t		bufsz;
} dl_info_t;

static size_t
dl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	dl_info_t *dl_info = userdata;
	size_t bytes = size * nmemb;

	ASSERT(dl_info != NULL);

	/* Respond to an early termination request */
	if (!dl_info->cdb->loader.run)
		return (0);

	if (dl_info->bufcap < dl_info->bufsz + bytes) {
		dl_info->bufcap += REALLOC_STEP;
		if (dl_info->bufcap > MAX_DL_SIZE) {
			logMsg("Error downloading %s: too much data received "
			    "(%ld bytes)", dl_info->url, (long)dl_info->bufcap);
			return (0);
		}
		dl_info->buf = realloc(dl_info->buf, dl_info->bufcap);
	}
	memcpy(&dl_info->buf[dl_info->bufsz], ptr, bytes);
	dl_info->bufsz += bytes;

	return (bytes);
}

static char *
mk_index_path(chartdb_t *cdb)
{
	char airac_nr[8];

	snprintf(airac_nr, sizeof (airac_nr), "%d", cdb->airac);
	return (mkpathname(cdb->path, cdb->prov_name, airac_nr,
	    "d-TPP_Metafile.xml", NULL));
}

static struct curl_slist *
append_if_mod_since_hdr(struct curl_slist *hdrs, const char *path)
{
	struct stat st;

	if (file_exists(path, NULL) && stat(path, &st) == 0) {
		char buf[64];
		time_t t = st.st_mtime;

		strftime(buf, sizeof (buf),
		    "If-Modified-Since: %a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
		hdrs = curl_slist_append(hdrs, buf);
	}

	return (hdrs);
}

bool_t
download_common(chartdb_t *cdb, const char *url, const char *filepath,
    const char *error_prefix)
{
	CURL *curl;
	struct curl_slist *hdrs = NULL;
	dl_info_t dl_info = { NULL };
	CURLcode res;
	long code = 0;
	bool_t result = B_TRUE;

	curl = curl_easy_init();
	VERIFY(curl != NULL);

	dl_info.cdb = cdb;
	dl_info.url = url;

	hdrs = append_if_mod_since_hdr(hdrs, filepath);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, DL_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPD_TIME);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPD_LIM);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dl_info);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

	res = curl_easy_perform(curl);
	if (res == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if (res == CURLE_OK && code == 200 && dl_info.bufsz != 0) {
		char *dname = lacf_dirname(filepath);
		FILE *fp;

		if (!create_directory_recursive(dname)) {
			free(dname);
			goto out;
		}
		free(dname);
		fp = fopen(filepath, "wb");

		if (fp != NULL) {
			fwrite(dl_info.buf, 1, dl_info.bufsz, fp);
			fclose(fp);
		} else {
			logMsg("%s %s: error writing disk file %s: %s",
			    error_prefix, url, filepath, strerror(errno));
			result = B_FALSE;
		}
	} else {
		if (res != CURLE_OK) {
			logMsg("%s %s: %s", error_prefix, url,
			    curl_easy_strerror(res));
			result = B_FALSE;
		} else if (code != 304) {
			/*
			 * Code `304' indicates we have a cached good copy.
			 */
			logMsg("%s %s: HTTP error %ld", error_prefix, url,
			    code);
			result = B_FALSE;
		}
	}
out:
	curl_easy_cleanup(curl);
	if (hdrs != NULL)
		curl_slist_free_all(hdrs);
	free(dl_info.buf);

	return (result);
}

bool_t
update_index(chartdb_t *cdb)
{
	char url[128];
	char *index_path = mk_index_path(cdb);
	bool_t result = B_FALSE;

	snprintf(url, sizeof (url), INDEX_URL, cdb->airac);
	result = download_common(cdb, url, index_path,
	    "Error downloading chart index");

	free(index_path);

	return (result);
}

static void
load_record(chart_arpt_t *arpt, const xmlNode *rec)
{
	chart_t *chart = calloc(1, sizeof (*chart));

	for (const xmlNode *node = rec->children; node != NULL;
	    node = node->next) {
		const char *content;

		if (node->children == NULL || node->name == NULL)
			continue;
		content = (char *)node->children[0].content;
		if (strcmp((char *)node->name, "chart_name") == 0) {
			strlcpy(chart->name, content, sizeof (chart->name));
		} else if (strcmp((char *)node->name, "faanfd18") == 0) {
			strlcpy(chart->codename, content,
			    sizeof (chart->codename));
		} else if (strcmp((char *)node->name, "pdf_name") == 0) {
			/*
			 * "DELETED_JOB.PDF" in the PDF filename means that
			 * the chart no longer exists, so get rid of it.
			 */
			if (strcmp(content, "DELETED_JOB.PDF") == 0) {
				chart->type = CHART_TYPE_UNKNOWN;
				break;
			}
			strlcpy(chart->filename, content,
			    sizeof (chart->filename));
		} else if (strcmp((char *)node->name, "chart_code") == 0) {
			if (strcmp(content, "APD") == 0)
				chart->type = CHART_TYPE_APD;
			else if (strcmp(content, "IAP") == 0)
				chart->type = CHART_TYPE_IAP;
			else if (strcmp(content, "DP") == 0)
				chart->type = CHART_TYPE_DP;
			else if (strcmp(content, "ODP") == 0)
				chart->type = CHART_TYPE_ODP;
			else if (strcmp(content, "STAR") == 0)
				chart->type = CHART_TYPE_STAR;
			else if (strcmp(content, "MIN") == 0)
				chart->type = CHART_TYPE_MIN;
			else
				chart->type = CHART_TYPE_UNKNOWN;
		}
	}

	if (chart->name[0] == '\0' || chart->type == CHART_TYPE_UNKNOWN ||
	    !chartdb_add_chart(arpt, chart)) {
		free(chart);
	}
}

static void
load_airport(chartdb_t *cdb, const xmlNode *arpt_node)
{
	char *icao_ident = (char *)xmlGetProp(arpt_node,
	    (xmlChar *)"icao_ident");
	char *apt_ident = (char *)xmlGetProp(arpt_node, (xmlChar *)"apt_ident");
	chart_arpt_t *arpt;
	char icao[8];

	if (icao_ident == NULL || apt_ident == NULL) {
		/* Malformed file */
		goto out;
	}
	if (is_valid_icao_code(icao_ident)) {
		/* Normal ICAO identifier present, use it */
		strlcpy(icao, icao_ident, sizeof (icao));
	} else if (strlen(apt_ident) > 0) {
		/*
		 * Local non-ICAO identifier, convert it into a pseudo-ICAO
		 * identifier.
		 */
		snprintf(icao, sizeof (icao), "K%s", apt_ident);
	} else {
		/* No valid ID present, skip the airport */
		goto out;
	}
	arpt = chartdb_add_arpt(cdb, icao);

	for (const xmlNode *rec = arpt_node->children; rec != NULL;
	    rec = rec->next) {
		if (rec->name == NULL ||
		    strcmp((char *)rec->name, "record") != 0)
			continue;
		load_record(arpt, rec);
	}
out:
	if (icao_ident != NULL)
		xmlFree(icao_ident);
	if (apt_ident != NULL)
		xmlFree(apt_ident);
}

static bool_t
load_index(chartdb_t *cdb)
{
	char *index_path = mk_index_path(cdb);
	xmlDoc *doc = NULL;
	xmlXPathContext *xpath_ctx = NULL;
	xmlXPathObject *arpts_obj = NULL;

	doc = xmlParseFile(index_path);
	if (doc == NULL) {
		logMsg("Error parsing chart index %s: XML parsing error",
		    index_path);
		goto errout;
	}
	xpath_ctx = xmlXPathNewContext(doc);
	if (xpath_ctx == NULL) {
		logMsg("Error creating XPath context for document %s.",
		    index_path);
		goto errout;
	}
	arpts_obj = xmlXPathEvalExpression((xmlChar *)
	    "/digital_tpp/state_code/city_name/airport_name", xpath_ctx);
	for (int i = 0; i < arpts_obj->nodesetval->nodeNr; i++)
		load_airport(cdb, arpts_obj->nodesetval->nodeTab[i]);
	xmlXPathFreeObject(arpts_obj);
	arpts_obj = NULL;

	xmlXPathFreeContext(xpath_ctx);
	xmlFreeDoc(doc);
	free(index_path);

	return (B_TRUE);
errout:
	if (arpts_obj != NULL)
		xmlXPathFreeObject(arpts_obj);
	if (xpath_ctx != NULL)
		xmlXPathFreeContext(xpath_ctx);
	if (doc != NULL)
		xmlFreeDoc(doc);
	free(index_path);

	return (B_FALSE);
}

bool_t
chart_faa_init(chartdb_t *cdb)
{
	if (!update_index(cdb) || !load_index(cdb))
		goto errout;

	return (B_TRUE);

errout:
	chart_faa_fini(cdb);
	return (B_FALSE);
}

void
chart_faa_fini(chartdb_t *cdb)
{
	UNUSED(cdb);
}

bool_t
chart_faa_get_chart(chart_t *chart)
{
	chartdb_t *cdb;
	chart_arpt_t *arpt;
	char *filepath;
	bool_t result;
	char url[128];

	ASSERT(chart->arpt != NULL);
	arpt = chart->arpt;
	ASSERT(arpt->db != NULL);
	cdb = arpt->db;

	filepath = chartdb_mkpath(chart);
	snprintf(url, sizeof (url), CHART_URL, cdb->airac, chart->filename);
	result = download_common(cdb, url, filepath, "Error downloading chart");
	free(filepath);

	return (result);
}
