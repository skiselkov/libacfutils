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

#if	!IBM
#include <sys/stat.h>
#endif	/* !IBM */

#include "chart_prov_common.h"
#include "acfutils/stat.h"

#define	REALLOC_STEP	(8 << 20)	/* bytes */
#define	MAX_DL_SIZE	(128 << 20)	/* bytes */
#define	DL_TIMEOUT	300L		/* seconds */
#define	LOW_SPD_LIM	4096L		/* bytes/s */
#define	LOW_SPD_TIME	30L		/* seconds */

void
chart_dl_info_init(chart_dl_info_t *info, chartdb_t *cdb, const char *url)
{
	ASSERT(info != NULL);
	ASSERT(url != NULL);
	ASSERT(cdb != NULL);
	memset(info, 0, sizeof (*info));
	info->url = url;
	info->cdb = cdb;
}

void
chart_dl_info_fini(chart_dl_info_t *info)
{
	ASSERT(info != NULL);
	free(info->buf);
	memset(info, 0, sizeof (*info));
}

size_t
chart_dl_info_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	chart_dl_info_t *dl_info = userdata;
	size_t bytes = size * nmemb;

	ASSERT(dl_info != NULL);

	/* Respond to an early termination request */
	if (dl_info->cdb != NULL && !dl_info->cdb->loader.run)
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

static struct curl_slist *
append_if_mod_since_hdr(struct curl_slist *hdrs, const char *path)
{
	struct stat st;

	if (file_exists(path, NULL) && stat(path, &st) == 0) {
		char buf[64];
		time_t t = st.st_mtime;
		struct tm tm;

		lacf_gmtime_r(&t, &tm);
		strftime(buf, sizeof (buf),
		    "If-Modified-Since: %a, %d %b %Y %H:%M:%S GMT", &tm);
		hdrs = curl_slist_append(hdrs, buf);
	}

	return (hdrs);
}

static bool_t
write_dl(chart_dl_info_t *dl_info, const char *filepath, const char *url,
    const char *error_prefix)
{
	char *dname = lacf_dirname(filepath);
	FILE *fp;

	if (!create_directory_recursive(dname)) {
		free(dname);
		return (B_FALSE);
	}
	free(dname);
	fp = fopen(filepath, "wb");

	if (fp == NULL) {
		logMsg("%s %s: error writing disk file %s: %s",
		    error_prefix, url, filepath, strerror(errno));
		return (B_FALSE);
	}
	fwrite(dl_info->buf, 1, dl_info->bufsz, fp);
	fclose(fp);

	return (B_TRUE);
}

bool_t
chart_download_multi(CURL **curl_p, chartdb_t *cdb, const char *url,
    const char *filepath, const char *method,
    const chart_prov_info_login_t *login, int timeout,
    const char *error_prefix, chart_dl_info_t *raw_output)
{
	char *proxy = NULL;
	bool_t res;

	if (cdb != NULL) {
		mutex_enter(&cdb->lock);
		if (cdb->proxy != NULL)
			proxy = safe_strdup(cdb->proxy);
		mutex_exit(&cdb->lock);
	}
	res = chart_download_multi2(curl_p, proxy, url, filepath, method,
	    login, timeout, error_prefix, raw_output);
	LACF_DESTROY(proxy);

	return (res);
}

bool_t
chart_download_multi2(CURL **curl_p, const char *proxy, const char *url,
    const char *filepath, const char *method,
    const chart_prov_info_login_t *login, int timeout,
    const char *error_prefix, chart_dl_info_t *raw_output)
{
	CURL *curl;
	struct curl_slist *hdrs = NULL;
	chart_dl_info_t dl_info = { .cdb = NULL };
	CURLcode res;
	long code = 0;
	bool_t result = B_TRUE;

	ASSERT(curl_p != NULL);
	/* cdb can be NULL */
	ASSERT(url != NULL);
	/* filepath can be NULL */
	/* method can be NULL */
	/* login can be NULL */
	ASSERT(error_prefix != NULL);
	/* raw_output can be NULL */

	dl_info.url = url;

	if (*curl_p != NULL) {
		curl = *curl_p;
	} else {
		const char *cainfo = (login != NULL ? login->cainfo : NULL);

		curl = curl_easy_init();
		VERIFY(curl != NULL);

		curl_easy_setopt(curl, CURLOPT_TIMEOUT,
		    timeout < 0 ? DL_TIMEOUT : timeout);
		chart_setup_curl(curl, cainfo);
		if (login != NULL && login->username != NULL) {
			curl_easy_setopt(curl, CURLOPT_USERNAME,
			    login->username);
		}
		if (login != NULL && login->password != NULL) {
			curl_easy_setopt(curl, CURLOPT_PASSWORD,
			    login->password);
		}
		if (proxy != NULL)
			curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
		*curl_p = curl;
	}

	if (method != NULL)
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	else
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

	if (filepath != NULL)
		hdrs = append_if_mod_since_hdr(hdrs, filepath);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dl_info);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

	res = curl_easy_perform(curl);
	if (res == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	/*
	 * HTTP code 207 is "Multi-Status" for WebDAV.
	 */
	if (res == CURLE_OK && (code == 200 || code == 207) &&
	    dl_info.bufsz != 0) {
		if (filepath != NULL) {
			result = write_dl(&dl_info, filepath, url,
			    error_prefix);
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
	if (hdrs != NULL) {
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
		curl_slist_free_all(hdrs);
	}
	if (raw_output != NULL) {
		if (code == 200 || code == 207) {
			*raw_output = dl_info;
		} else {
			memset(raw_output, 0, sizeof (*raw_output));
			free(dl_info.buf);
		}
	} else {
		free(dl_info.buf);
	}

	return (result);
}

bool_t
chart_download(chartdb_t *cdb, const char *url, const char *filepath,
    const chart_prov_info_login_t *login, const char *error_prefix,
    chart_dl_info_t *raw_output)
{
	CURL *curl = NULL;
	bool_t result = chart_download_multi(&curl, cdb, url, filepath, NULL,
	    login, -1, error_prefix, raw_output);
	curl_easy_cleanup(curl);
	return (result);
}

void
chart_setup_curl(CURL *curl, const char *cainfo)
{
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPD_TIME);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPD_LIM);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chart_dl_info_write);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	if (cainfo != NULL)
		curl_easy_setopt(curl, CURLOPT_CAINFO, cainfo);
}

void
word_subst(char *str, const char **subst)
{
	for (int i = 0; subst[i] != NULL; i += 2) {
		char *p = strstr(str, subst[i]);

		if (p != NULL &&
		    /*
		     * Make sure the word either ends the line,
		     * or it's followed by a space.
		     */
		    (strlen(p) == strlen(subst[i]) ||
		    !isalnum(p[strlen(subst[i])]))) {
			unsigned l1 = strlen(subst[i]);
			unsigned l2 = strlen(subst[i + 1]);
			memcpy(p, subst[i + 1], l2);
			memmove(&p[l2], &p[l1],
			    strlen(str) - (&p[l1] - str) + 1);
		}
	}
}

bool_t
chartdb_want_to_stop(chartdb_t *cdb)
{
	bool_t result;

	ASSERT(cdb != NULL);

	mutex_enter(&cdb->lock);
	result = (list_head(&cdb->loader_queue) == &cdb->loader_cmd_purge);
	mutex_exit(&cdb->lock);

	return (result);
}

void *
chart_get_prov_info(const chart_t *chart, chartdb_t **cdb_p,
    chart_arpt_t **arpt_p)
{
	chartdb_t *cdb;
	chart_arpt_t *arpt;

	ASSERT(chart != NULL);
	ASSERT(chart->arpt != NULL);
	arpt = chart->arpt;
	if (arpt_p != NULL)
		*arpt_p = arpt;
	ASSERT(arpt->db != NULL);
	cdb = arpt->db;
	ASSERT(cdb != NULL);
	if (cdb_p != NULL)
		*cdb_p = cdb;
	ASSERT(cdb->prov_priv != NULL);

	return (cdb->prov_priv);
}
