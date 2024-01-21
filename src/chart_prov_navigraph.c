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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#include <errno.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#if	!IBM
#include <sys/stat.h>
#endif	/* !IBM */

#include "acfutils/apps.h"
#include "acfutils/base64.h"
#include "acfutils/conf.h"
#include "acfutils/helpers.h"
#include "acfutils/osrand.h"
#include "acfutils/thread.h"
#include "chart_prov_faa.h"
#include "chart_prov_common.h"
#include "jsmn/jsmn.h"
#include "jsmn/jsmn_path.h"
#include "sha2.h"

/* #define	DEBUG_NAVIGRAPH */
#ifdef	DEBUG_NAVIGRAPH
#define	NAV_DBG_LOG(...)	logMsg(__VA_ARGS__)
#else
#define	NAV_DBG_LOG(...)	do {} while (0)
#endif

#define	DEV_AUTH_ENDPT	\
	"https://identity.api.navigraph.com/connect/deviceauthorization"
#define	TOKEN_ENDPT	\
	"https://identity.api.navigraph.com/connect/token"
#define	USERINFO_ENDPT	\
	"https://identity.api.navigraph.com/connect/userinfo"
#define	DL_TIMEOUT	30L		/* seconds */
#define	LOW_SPD_LIM	4096L		/* bytes/s */
#define	LOW_SPD_TIME	30L		/* seconds */

#define	WMARK_FONT_SIZE	32		/* points */

typedef struct {
	mutex_t	lock;

	CURL	*curl;
	char	*dev_code;
	char	*refresh_token;
	char	*access_token;
	char	*username;
	char	code_verifier[64];

	time_t	expire_t;
	int	intval;
	time_t	next_check_t;
	time_t	access_expire_t;

	bool_t	pending_ext_setup;
} navigraph_t;

static void *navigraph_dl(chartdb_t *cdb, navigraph_t *nav,
    const char *url, bool_t allow_blocking, size_t *len);

static void
conv_base64_to_url(char *str)
{
	ASSERT(str != NULL);
	for (int i = 0, n = strlen(str); i < n; i++) {
		if (str[i] == '+') {
			str[i] = '-';
		} else if (str[i] == '/') {
			str[i] = '_';
		} else if (str[i] == '=') {
			str[i] = '\0';
			break;
		}
	}
}

static bool_t
get_json_error(const chart_dl_info_t *dl_info, char *error, size_t cap)
{
	jsmn_parser parser;
	jsmntok_t toks[128];
	int n_toks;

	ASSERT(dl_info != NULL);
	ASSERT(error != NULL);

	jsmn_init(&parser);
	n_toks = jsmn_parse(&parser, (const char *)dl_info->buf,
	    dl_info->bufsz, toks, ARRAY_NUM_ELEM(toks));
	if (n_toks < 0)
		return (B_FALSE);
	return (jsmn_get_tok_data_path((const char *)dl_info->buf,
	    toks, n_toks, "error", error, cap));
}

/*
 * Sort the charts based on the index number.
 */
static int
chart_sort_func_navigraph(const void *a, const void *b, void *c)
{
	const char *na = *(char **)a, *nb = *(char **)b;
	const char *idx_a = strrchr(na, '#'), *idx_b = strrchr(nb, '#');
	const char *dash_a, *dash_b;
	char sect_a_str[8] = {}, sect_b_str[8] = {};
	char page_a_str[8] = {}, page_b_str[8] = {};
	int sect_a, sect_b, page_a, page_b, cmp;

	LACF_UNUSED(c);

	if (idx_a == NULL || idx_a < &na[1] || idx_a[-1] != '#' ||
	    idx_b == NULL || idx_b < &nb[1] || idx_b[-1] != '#') {
		return (0);
	}
	idx_a++;
	idx_b++;
	dash_a = strchr(idx_a, '-');
	dash_b = strchr(idx_b, '-');
	if (dash_a == NULL || dash_b == NULL)
		return (0);
	strlcpy(sect_a_str, idx_a,
	    MIN(sizeof (sect_a_str), (unsigned)(dash_a - idx_a) + 1));
	strlcpy(page_a_str, &dash_a[1], sizeof (page_a_str));
	strlcpy(sect_b_str, idx_b,
	    MIN(sizeof (sect_b_str), (unsigned)(dash_b - idx_b) + 1));
	strlcpy(page_b_str, &dash_b[1], sizeof (page_b_str));

	sect_a = atoi(sect_a_str);
	page_a = atoi(page_a_str);
	sect_b = atoi(sect_b_str);
	page_b = atoi(page_b_str);

	if (sect_a < sect_b)
		return (-1);
	if (sect_a > sect_b)
		return (1);
	if (page_a < page_b)
		return (-1);
	if (page_a > page_b)
		return (1);
	cmp = strcmp(idx_a, idx_b);
	if (cmp < 0)
		return (-1);
	if (cmp > 0)
		return (1);
	return (0);
}

static bool_t
handle_dev_auth(const chart_dl_info_t *dl_info, navigraph_t *nav)
{
	jsmn_parser parser;
	jsmntok_t toks[128];
	int n_toks;
	const jsmntok_t *dev_code, *verif_uri, *expires, *intval;
	const char *str;
	char *uri;
	char tmpbuf[32];

	ASSERT(dl_info != NULL);
	str = (const char *)dl_info->buf;
	ASSERT(nav != NULL);

	jsmn_init(&parser);
	n_toks = jsmn_parse(&parser, str, dl_info->bufsz,
	    toks, ARRAY_NUM_ELEM(toks));
	if (n_toks < 0) {
		logMsg("Can't authorize device: server responded with "
		    "what looks like invalid JSON data");
		return (B_FALSE);
	}
	if (jsmn_get_tok_data_path(str, toks, n_toks, "error",
	    tmpbuf, sizeof (tmpbuf))) {
		logMsg("Can't authorize device: server responded with "
		    "error \"%s\"", tmpbuf);
		return (B_FALSE);
	}
	dev_code = jsmn_path_lookup_format(str, toks, n_toks, "device_code");
	verif_uri = jsmn_path_lookup_format(str, toks, n_toks,
	    "verification_uri_complete");
	expires = jsmn_path_lookup_format(str, toks, n_toks, "expires_in");
	intval = jsmn_path_lookup_format(str, toks, n_toks, "interval");
	if (dev_code == NULL || verif_uri == NULL || expires == NULL ||
	    intval == NULL) {
		logMsg("Can't authorize device: server responded with "
		    "invalid JSON structure");
		return (B_FALSE);
	}
	uri = jsmn_strdup_tok_data(str, verif_uri);
	NAV_DBG_LOG("Opening verification URI: %s", uri);
	if (!lacf_open_URL(uri)) {
		free(uri);
		logMsg("Can't authorize device: can't launch default browser "
		    "for verification URI");
		return (B_FALSE);
	}
	free(uri);

	if (nav->dev_code != NULL)
		free(nav->dev_code);
	nav->dev_code = jsmn_strdup_tok_data(str, dev_code);
	jsmn_get_tok_data(str, expires, tmpbuf, sizeof (tmpbuf));
	nav->expire_t = time(NULL) + atol(tmpbuf);
	jsmn_get_tok_data(str, intval, tmpbuf, sizeof (tmpbuf));
	nav->intval = MAX(atoi(tmpbuf), 1);
	nav->next_check_t = time(NULL) + nav->intval;

	NAV_DBG_LOG("devauth started, dev code \"%s\", intval: %d",
	    nav->dev_code, nav->intval);

	return (B_TRUE);
}

static bool_t
start_auth(chartdb_t *cdb, navigraph_t *nav)
{
	uint8_t code_verifier[32];
	uint8_t chlg_raw[SHA256_DIGEST_SIZE];
	char chlg_enc[64];
	size_t len;
	char post_body[512];
	const chart_prov_info_login_t *login;
	CURLcode res;
	chart_dl_info_t dl_info;

	ASSERT(cdb != NULL);
	ASSERT(cdb->prov_login != NULL);
	login = cdb->prov_login;
	ASSERT(nav != NULL);

	chart_dl_info_init(&dl_info, cdb, DEV_AUTH_ENDPT);
	/*
	 * We generate the code verifier and Base64-URL encode it.
	 * This must be kept after successful device authorization.
	 */
	if (!osrand(code_verifier, sizeof (code_verifier))) {
		logMsg("Cannot generate auth request: osrand() failed");
		goto errout;
	}
	len = lacf_base64_encode(code_verifier, sizeof (code_verifier),
	    (uint8_t *)nav->code_verifier);
	ASSERT3U(len, ==, 44);
	nav->code_verifier[len] = '\0';
	conv_base64_to_url(nav->code_verifier);
	/*
	 * Now SHA-256 hash the encoded code verifier, Base64-URL-encode
	 * it again and send it over the wire.
	 */
	sha256(nav->code_verifier, 43, chlg_raw);
	len = lacf_base64_encode(chlg_raw, sizeof (chlg_raw),
	    (uint8_t *)chlg_enc);
	chlg_enc[len] = '\0';
	conv_base64_to_url(chlg_enc);

	snprintf(post_body, sizeof (post_body), "client_id=%s&"
	    "client_secret=%s&code_challenge=%s&code_challenge_method=S256",
	    login->username, login->password, chlg_enc);

	curl_easy_setopt(nav->curl, CURLOPT_URL, DEV_AUTH_ENDPT);
	curl_easy_setopt(nav->curl, CURLOPT_POST, 1L);
	curl_easy_setopt(nav->curl, CURLOPT_POSTFIELDS, post_body);
	curl_easy_setopt(nav->curl, CURLOPT_WRITEDATA, &dl_info);

	NAV_DBG_LOG("devauth starting, code_challenge \"%s\"", chlg_enc);
	res = curl_easy_perform(nav->curl);
	if (res == CURLE_OK) {
		long code;

		curl_easy_getinfo(nav->curl, CURLINFO_RESPONSE_CODE, &code);
		if (code == 200) {
			NAV_DBG_LOG("devauth server responded with 200");
			if (!handle_dev_auth(&dl_info, nav))
				goto errout;
		} else {
			logMsg("Cannot generate auth request: server "
			    "responded with error code %ld", code);
			goto errout;
		}
	} else {
		logMsg("Cannot generate auth request: %s",
		    curl_easy_strerror(res));
		goto errout;
	}
	curl_easy_setopt(nav->curl, CURLOPT_POST, 0L);
	nav->pending_ext_setup = B_TRUE;
	chart_dl_info_fini(&dl_info);
	return (B_TRUE);
errout:
	curl_easy_setopt(nav->curl, CURLOPT_POST, 0L);
	chart_dl_info_fini(&dl_info);
	return (B_FALSE);
}

static void
save_refresh_token(const chartdb_t *cdb, const navigraph_t *nav)
{
	conf_t *conf = conf_create_empty();

	ASSERT(cdb != NULL);
	ASSERT(nav != NULL);

	conf_set_str(conf, "refresh_token", nav->refresh_token);
	if (create_directory_recursive(cdb->path)) {
		char *confpath = mkpathname(cdb->path,
		    "navigraph-tokens.cache", NULL);
		if (!conf_write_file(conf, confpath)) {
			logMsg("Error writing %s: %s", confpath,
			    strerror(errno));
		}
	}
	conf_free(conf);
}

static bool_t
handle_token_response(chartdb_t *cdb, const chart_dl_info_t *dl_info,
    navigraph_t *nav)
{
	jsmn_parser parser;
	jsmntok_t toks[128];
	int n_toks;
	const char *json;
	const jsmntok_t *access_tok, *expires_tok, *refresh_tok;
	char tmpbuf[32];

	ASSERT(dl_info != NULL);
	json = (const char *)dl_info->buf;
	ASSERT(nav != NULL);

	jsmn_init(&parser);
	n_toks = jsmn_parse(&parser, json, dl_info->bufsz, toks,
	    ARRAY_NUM_ELEM(toks));
	if (n_toks < 0) {
		logMsg("Cannot fetch access token: server responded with "
		    "what looks like invalid JSON data");
		return (B_FALSE);
	}
	access_tok = jsmn_path_lookup_format(json, toks, n_toks, "access_token");
	expires_tok = jsmn_path_lookup_format(json, toks, n_toks, "expires_in");
	refresh_tok = jsmn_path_lookup_format(json, toks, n_toks,
	    "refresh_token");
	/* We must at least have an access token */
	if (access_tok == NULL || expires_tok == NULL ||
	    (nav->refresh_token == NULL && refresh_tok == NULL)) {
		logMsg("Cannot fetch access token: server responded with "
		    "invalid JSON structure");
		return (B_FALSE);
	}
	if (refresh_tok != NULL) {
		LACF_DESTROY(nav->refresh_token);
		nav->refresh_token = jsmn_strdup_tok_data(json, refresh_tok);
		save_refresh_token(cdb, nav);
		NAV_DBG_LOG("got new refresh_token \"%s\"",
		    nav->refresh_token);
	}
	LACF_DESTROY(nav->access_token);
	nav->access_token = jsmn_strdup_tok_data(json, access_tok);
	jsmn_get_tok_data(json, expires_tok, tmpbuf, sizeof (tmpbuf));
	nav->access_expire_t = time(NULL) + atoi(tmpbuf);

	NAV_DBG_LOG("got new access_token \"%s\" expires in %d seconds",
	    nav->access_token, atoi(tmpbuf));

	return (B_TRUE);
}

static bool_t
get_tokens(chartdb_t *cdb, navigraph_t *nav)
{
	CURLcode res;
	chart_dl_info_t dl_info;
	char post_body[1024];
	const chart_prov_info_login_t *login;

	ASSERT(cdb != NULL);
	ASSERT(nav != NULL);
	ASSERT(cdb->prov_login != NULL);
	login = cdb->prov_login;

	chart_dl_info_init(&dl_info, cdb, TOKEN_ENDPT);

	while (time(NULL) <= nav->next_check_t) {
		NAV_DBG_LOG("waiting for next token check (in %ld secs)",
		    (long)(nav->next_check_t - time(NULL)));
		usleep(1000000);
		/* Early termination request from outside? */
		if (!cdb->loader.run) {
			NAV_DBG_LOG("wait aborted due to worker thread "
			    "shutdown request");
			goto errout;
		}
	}
	if (nav->refresh_token != NULL) {
		/*
		 * Do an access token refresh.
		 */
		NAV_DBG_LOG("have refresh_token, doing an access_token "
		    "refresh");
		snprintf(post_body, sizeof (post_body),
		    "grant_type=refresh_token&client_id=%s&client_secret=%s&"
		    "refresh_token=%s", login->username, login->password,
		    nav->refresh_token);
	} else {
		NAV_DBG_LOG("no tokens, expecting both a refresh_token "
		    "and access_token");
		snprintf(post_body, sizeof (post_body),
		    "grant_type=urn:ietf:params:oauth:grant-type:device_code&"
		    "device_code=%s&code_verifier=%s&client_id=%s&"
		    "client_secret=%s&scope=openid charts offline_access",
		    nav->dev_code, nav->code_verifier, login->username,
		    login->password);
	}
	curl_easy_setopt(nav->curl, CURLOPT_URL, TOKEN_ENDPT);
	curl_easy_setopt(nav->curl, CURLOPT_POST, 1L);
	curl_easy_setopt(nav->curl, CURLOPT_POSTFIELDS, post_body);
	curl_easy_setopt(nav->curl, CURLOPT_WRITEDATA, &dl_info);

	res = curl_easy_perform(nav->curl);
	if (res == CURLE_OK) {
		long code;
		char error[32];

		curl_easy_getinfo(nav->curl, CURLINFO_RESPONSE_CODE, &code);
		if (code == 200) {
			NAV_DBG_LOG("server responded with 200");
			if (get_json_error(&dl_info, error, sizeof (error))) {
				logMsg("Cannot fetch access token: server "
				    "responded with error \"%s\"", error);
				goto errout;
			} else if (!handle_token_response(cdb, &dl_info, nav)) {
				goto errout;
			}
		} else if (code == 400 && get_json_error(&dl_info,
		    error, sizeof (error))) {
			NAV_DBG_LOG("server responded with: 400 %s", error);
			if (strcmp(error, "authorization_pending") == 0) {
				/* Just keep on waiting */
				nav->next_check_t = time(NULL) + nav->intval;
				chart_dl_info_fini(&dl_info);
				return (get_tokens(cdb, nav));
			} else if (strcmp(error, "slow_down") == 0) {
				/* Just keep on waiting - increase intval +5s */
				nav->intval += 5;
				nav->next_check_t = time(NULL) + nav->intval;
				chart_dl_info_fini(&dl_info);
				return (get_tokens(cdb, nav));
			} else if (strcmp(error, "invalid_grant") == 0 &&
			    nav->refresh_token != NULL) {
				/*
				 * Refresh token is invalid, we need to drop
				 * it and restart a new device authorization
				 */
				LACF_DESTROY(nav->refresh_token);
				logMsg("Refresh token has become stale: "
				    "restarting a new device authorization");
			} else {
				/* All other errors are fatal */
				logMsg("Cannot fetch access token: server "
				    "responded with error \"%s\"", error);
				goto errout;
			}
		} else {
			logMsg("Cannot fetch access token: server "
			    "responded with error code %ld", code);
			goto errout;
		}
	} else {
		logMsg("Cannot generate auth request: %s",
		    curl_easy_strerror(res));
		goto errout;
	}
	curl_easy_setopt(nav->curl, CURLOPT_POST, 0L);
	nav->pending_ext_setup = B_FALSE;
	chart_dl_info_fini(&dl_info);
	return (B_TRUE);
errout:
	curl_easy_setopt(nav->curl, CURLOPT_POST, 0L);
	/* Even if we fail completely, we exit the pending auth state */
	nav->pending_ext_setup = B_FALSE;
	chart_dl_info_fini(&dl_info);
	return (B_FALSE);
}

static void *
do_download(chartdb_t *cdb, navigraph_t *nav, const char *url, size_t *len,
    bool_t *retry)
{
	CURLcode res;
	chart_dl_info_t dl_info;
	struct curl_slist *slist = NULL;
	char *auth_hdr;

	ASSERT(cdb != NULL);
	ASSERT(nav != NULL);
	ASSERT(url != NULL);
	ASSERT(len != NULL);
	ASSERT(retry != NULL);
	*retry = B_FALSE;

	chart_dl_info_init(&dl_info, cdb, TOKEN_ENDPT);
	ASSERT(nav->access_token != NULL);
	auth_hdr = sprintf_alloc("Authorization: Bearer %s", nav->access_token);
	slist = curl_slist_append(slist, auth_hdr);
	LACF_DESTROY(auth_hdr);

	curl_easy_setopt(nav->curl, CURLOPT_URL, url);
	curl_easy_setopt(nav->curl, CURLOPT_HTTPHEADER, slist);
	curl_easy_setopt(nav->curl, CURLOPT_WRITEDATA, &dl_info);

	NAV_DBG_LOG("%s", url);
	res = curl_easy_perform(nav->curl);
	if (res == CURLE_OK) {
		long code;
		char error[32];

		curl_easy_getinfo(nav->curl, CURLINFO_RESPONSE_CODE, &code);
		if (code == 200) {
			/* no-op */
			NAV_DBG_LOG("success");
		} else if (code == 400 && get_json_error(&dl_info,
		    error, sizeof (error))) {
			if (strcmp(error, "expired_token") == 0) {
				NAV_DBG_LOG("server says token has expired, "
				    "will try to refresh it");
				/*
				 * Token expired, even though we didn't expect
				 * it. Could just be a timing issue. Just drop
				 * the access token and retry to fetch anew.
				 */
				LACF_DESTROY(nav->access_token);
				*retry = B_TRUE;
				goto errout;
			} else {
				logMsg("Cannot download %s: server responded "
				    "with error %s", url, error);
				goto errout;
			}
		} else if (code == 401) {
			NAV_DBG_LOG("401 error, will try to refresh access "
			    "token");
			/*
			 * Server rejected our authorization completely.
			 * Let's try to start a completely new device auth.
			 */
			LACF_DESTROY(nav->access_token);
			LACF_DESTROY(nav->refresh_token);
			LACF_DESTROY(nav->dev_code);
			*retry = B_TRUE;
			goto errout;
		} else {
			logMsg("Cannot download %s: server responded with "
			    "error code %ld", url, code);
		}
	} else {
		logMsg("Cannot download %s: %s", url, curl_easy_strerror(res));
		goto errout;
	}
	/* Don't destroy the dl_info, we want to return the buffer from it */
	*len = dl_info.bufsz;
	curl_slist_free_all(slist);
	curl_easy_setopt(nav->curl, CURLOPT_HTTPHEADER, NULL);
	return (dl_info.buf);
errout:
	chart_dl_info_fini(&dl_info);
	curl_slist_free_all(slist);
	curl_easy_setopt(nav->curl, CURLOPT_HTTPHEADER, NULL);
	return (B_FALSE);
}

static void *
navigraph_dl(chartdb_t *cdb, navigraph_t *nav, const char *url,
    bool_t allow_blocking, size_t *len)
{
	ASSERT(cdb != NULL);
	ASSERT(nav != NULL);
	ASSERT_MUTEX_HELD(&nav->lock);
	ASSERT(url != NULL);
	ASSERT(len != NULL);
	*len = 0;
	/*
	 * Pre-emptively try to fetch a new access token if we're nearing
	 * the end of the validity period.
	 */
	if (nav->access_token != NULL &&
	    time(NULL) + 60 >= nav->access_expire_t) {
		LACF_DESTROY(nav->access_token);
	}
	NAV_DBG_LOG("%s", url);
	if (nav->access_token != NULL) {
		bool_t retry;
		uint8_t *result;
		/*
		 * If we have an access token, we can try to just grab
		 * the actual resource we're interested in.
		 */
		NAV_DBG_LOG("have access token, fetching target");
		result = do_download(cdb, nav, url, len, &retry);
		if (result != NULL) {
			return (result);
		} else if (retry) {
			NAV_DBG_LOG("soft failure, retrying");
			return (navigraph_dl(cdb, nav, url,
			    allow_blocking, len));
		} else {
			return (NULL);
		}
	} else if (nav->refresh_token != NULL ||
	    (nav->dev_code != NULL && nav->code_verifier[0] != '\0')) {
		/*
		 * We either already have a refresh token, or we've started
		 * the device authorization and we're awaiting a new refresh
		 * (+access) token.
		 */
		NAV_DBG_LOG("retrieving tokens");
		if (get_tokens(cdb, nav)) {
			if (nav->refresh_token != NULL)
				NAV_DBG_LOG("tokens acquired, retrying dl");
			else
				NAV_DBG_LOG("tokens stale, dropping devauth");
			return (navigraph_dl(cdb, nav, url,
			    allow_blocking, len));
		} else {
			return (NULL);
		}
	} else {
		/*
		 * We don't have anything. Start a new device authorization.
		 * If we can't block device authorization, fail immediately.
		 */
		NAV_DBG_LOG("device not authorized, starting devauth");
		if (!allow_blocking) {
			NAV_DBG_LOG("caller says we can't block, so failing");
			return (NULL);
		} else if (start_auth(cdb, nav)) {
			NAV_DBG_LOG("devauth succeeded, retrying dl");
			return (navigraph_dl(cdb, nav, url,
			    allow_blocking, len));
		} else {
			return (NULL);
		}
	}
}

static bool_t
get_username(chartdb_t *cdb, navigraph_t *nav)
{
	size_t len;
	void *data;
	jsmn_parser parser;
	jsmntok_t toks[128];
	int n_toks;
	const jsmntok_t *username_tok;

	ASSERT(cdb != NULL);
	ASSERT(nav != NULL);
	ASSERT_MUTEX_HELD(&nav->lock);
	ASSERT3P(nav->username, ==, NULL);

	NAV_DBG_LOG("Retrieving charts username");

	data = navigraph_dl(cdb, nav, USERINFO_ENDPT, B_TRUE, &len);
	if (data == NULL)
		return (B_FALSE);
	jsmn_init(&parser);
	n_toks = jsmn_parse(&parser, data, len, toks, ARRAY_NUM_ELEM(toks));
	username_tok = jsmn_path_lookup_format(data, toks, n_toks,
	    "preferred_username");
	if (username_tok == NULL) {
		logMsg("Can't fetch Navigraph userinfo: server returned "
		    "invalid JSON structure");
		free(data);
		return (B_FALSE);
	}
	nav->username = jsmn_strdup_tok_data(data, username_tok);
	free(data);

	return (B_TRUE);
}

static char *
stringify(const void *data, size_t len)
{
	char *str = safe_malloc(len + 1);
	ASSERT(data != NULL || len == 0);
	memcpy(str, data, len);
	str[len] = '\0';
	return (str);
}

static uint8_t *
dl_signed_url(chartdb_t *cdb, navigraph_t *nav, const char *top_url,
    bool_t allow_blocking, size_t *len)
{
	size_t signed_url_len;
	void *signed_url_raw;
	char *signed_url;
	void *result;

	ASSERT(cdb != NULL);
	ASSERT(nav != NULL);
	ASSERT(top_url != NULL);
	ASSERT(len != NULL);
	*len = 0;

	signed_url_raw = navigraph_dl(cdb, nav, top_url, allow_blocking,
	    &signed_url_len);
	if (signed_url_raw == NULL)
		return (NULL);
	signed_url = stringify(signed_url_raw, signed_url_len);
	free(signed_url_raw);

	result = navigraph_dl(cdb, nav, signed_url, allow_blocking, len);
	free(signed_url);

	return (result);
}

bool_t
chart_navigraph_init(chartdb_t *cdb)
{
	navigraph_t *nav = safe_calloc(1, sizeof (*nav));
	const chart_prov_info_login_t *login;
	char *path;
	conf_t *conf;
	bool_t result;

	ASSERT(cdb != NULL);

	VERIFY(cdb->prov_login != NULL);
	login = cdb->prov_login;
	VERIFY(login->username != NULL);
	VERIFY(login->password != NULL);
	VERIFY(login->cainfo != NULL);

	path = mkpathname(cdb->path, "navigraph-tokens.cache", NULL);
	conf = conf_read_file(path, NULL);
	if (conf != NULL) {
		const char *str;
		if (conf_get_str(conf, "refresh_token", &str))
			nav->refresh_token = safe_strdup(str);
		conf_free(conf);
	}
	free(path);

	mutex_init(&nav->lock);
	nav->curl = curl_easy_init();
	curl_easy_setopt(nav->curl, CURLOPT_TIMEOUT, DL_TIMEOUT);
	curl_easy_setopt(nav->curl, CURLOPT_LOW_SPEED_TIME, LOW_SPD_TIME);
	curl_easy_setopt(nav->curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPD_LIM);
	curl_easy_setopt(nav->curl, CURLOPT_WRITEFUNCTION, chart_dl_info_write);
	curl_easy_setopt(nav->curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(nav->curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(nav->curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(nav->curl, CURLOPT_CAINFO, login->cainfo);
	mutex_enter(&cdb->lock);
	if (cdb->proxy != NULL)
		curl_easy_setopt(nav->curl, CURLOPT_PROXY, cdb->proxy);
	mutex_exit(&cdb->lock);
	/*
	 * Navigraph API rules disallow local caching.
	 */
	cdb->disallow_caching = B_TRUE;
	cdb->prov_priv = nav;
	cdb->chart_sort_func = chart_sort_func_navigraph;
	/*
	 * Do NOT normalize 3-letter identifiers.
	 */
	cdb->normalize_non_icao = false;
	/*
	 * Force a connection right away to set up the account from
	 * the worker thread, where we can block for user input.
	 */
	mutex_enter(&nav->lock);
	result = get_username(cdb, nav);
	mutex_exit(&nav->lock);

	return (result);
}

void
chart_navigraph_fini(chartdb_t *cdb)
{
	navigraph_t *nav;

	ASSERT(cdb != NULL);

	if (cdb->prov_priv == NULL)
		return;
	nav = cdb->prov_priv;
	if (nav->curl != NULL)
		curl_easy_cleanup(nav->curl);
	if (nav->dev_code != NULL)
		ZERO_FREE_N(nav->dev_code, strlen(nav->dev_code));
	if (nav->access_token != NULL)
		ZERO_FREE_N(nav->access_token, strlen(nav->access_token));
	if (nav->refresh_token != NULL)
		ZERO_FREE_N(nav->refresh_token, strlen(nav->refresh_token));
	if (nav->username != NULL)
		ZERO_FREE_N(nav->username, strlen(nav->username));
	mutex_destroy(&nav->lock);
	ZERO_FREE(nav);

	cdb->prov_priv = NULL;
}

bool_t
chart_navigraph_get_chart(chart_t *chart)
{
	chartdb_t *cdb;
	chart_arpt_t *arpt;
	navigraph_t *nav;
	size_t png_data_len;
	void *png_data;
	char url[256];

	ASSERT(chart != NULL);
	nav = chart_get_prov_info(chart, &cdb, &arpt);

	mutex_enter(&nav->lock);
	if (nav->username == NULL && !get_username(cdb, nav)) {
		mutex_exit(&nav->lock);
		return (B_FALSE);
	}
	if (!chart->night || chart->filename_night == NULL) {
		snprintf(url, sizeof (url), "https://api.navigraph.com/v1/"
		    "charts/airports/%s/signedurls/%s", arpt->icao,
		    chart->filename);
	} else {
		snprintf(url, sizeof (url), "https://api.navigraph.com/v1/"
		    "charts/airports/%s/signedurls/%s", arpt->icao,
		    chart->filename_night);
	}
	png_data = dl_signed_url(cdb, nav, url, B_TRUE, &png_data_len);
	mutex_exit(&nav->lock);

	if (png_data != NULL) {
		if (chart->png_data != NULL) {
			memset(chart->png_data, 0, chart->png_data_len);
			free(chart->png_data);
		}
		chart->png_data = png_data;
		chart->png_data_len = png_data_len;
	}
	return (png_data != NULL);
}

void
chart_navigraph_watermark_chart(chart_t *chart, cairo_surface_t *surf)
{
	navigraph_t *nav;
	cairo_t *cr;
	unsigned w, h;
	char *watermark;

	ASSERT(chart != NULL);
	ASSERT(surf != NULL);
	nav = chart_get_prov_info(chart, NULL, NULL);
	ASSERT(nav != NULL);
	ASSERT(nav->username != NULL);
	watermark = sprintf_alloc("This chart is linked to Navigraph "
	    "account %s", nav->username);

	cr = cairo_create(surf);
	cairo_set_font_size(cr, WMARK_FONT_SIZE);
	if (chart->night)
		cairo_set_source_rgb(cr, 1, 1, 1);
	else
		cairo_set_source_rgb(cr, 0, 0, 0);
	w = cairo_image_surface_get_width(surf);
	h = cairo_image_surface_get_height(surf);
	/*
	 * Navigraph places the "not for navigational use" notice into
	 * the narrow margin, so put our watermark into the other margin.
	 */
	if (w < h) {
		cairo_translate(cr, 10, 10);
		cairo_rotate(cr, DEG2RAD(90));
		cairo_move_to(cr, 0, 0);
	} else {
		cairo_move_to(cr, 10, h - 15);
	}
	cairo_show_text(cr, watermark);
	cairo_destroy(cr);

	free(watermark);
}

static chart_arpt_t *
parse_arpt_json(chartdb_t *cdb, const char *icao, const void *json, size_t len)
{
	jsmn_parser parser;
	size_t max_toks = MAX(len / 4, 10000);
	jsmntok_t *toks = safe_calloc(max_toks, sizeof (*toks));
	int n_toks;
	chart_arpt_t *arpt;
	char name[128] = {}, city[128] = {}, state[8] = {};

	ASSERT(cdb != NULL);
	ASSERT(icao);
	ASSERT(json != NULL);

	jsmn_init(&parser);
	n_toks = jsmn_parse(&parser, json, len, toks, max_toks);
	if (n_toks < 0) {
		logMsg("Error parsing airport %s: airport data doesn't "
		    "look like well-formed JSON", icao);
		free(toks);
		return (NULL);
	}
	jsmn_get_tok_data_path(json, toks, n_toks, "name", name, sizeof (name));
	jsmn_get_tok_data_path(json, toks, n_toks, "city", city, sizeof (city));
	jsmn_get_tok_data_path(json, toks, n_toks, "state_province_code",
	    state, sizeof (state));

	arpt = chartdb_add_arpt(cdb, icao, name, city, state);
	arpt->load_complete = B_TRUE;
	free(toks);

	return (arpt);
}

static void
parse_chart_georef_data(const void *json, const jsmntok_t *toks, int n_toks,
    chart_t *chart, int i)
{
	char georefd[8];
	char x1[8], x2[8], y1[8], y2[8], lat1[16], lat2[16], lon1[16], lon2[16];
	const jsmntok_t *insets;
	chart_georef_t *georef;

	ASSERT(json != NULL);
	ASSERT(toks != NULL);
	ASSERT(chart != NULL);
	georef = &chart->georef;

	jsmn_get_tok_data_path_format(json, toks, n_toks,
	    "charts/[%d]/is_georeferenced", georefd, sizeof (georefd), i);
	if (strcmp(georefd, "true") != 0)
		return;
	if (!jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/pixels/x1", x1, sizeof (x1), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/pixels/x2", x2, sizeof (x2), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/pixels/y1", y1, sizeof (y1), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/pixels/y2", y2, sizeof (y2), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/latlng/lat1", lat1, sizeof (lat1), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/latlng/lat2", lat2, sizeof (lat2), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/latlng/lng1", lon1, sizeof (lon1), i) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/planview/latlng/lng2", lon2, sizeof (lon2), i)) {
		return;
	}
	georef->present = B_TRUE;
	georef->pixels[0] = VECT2(atoi(x1), atoi(y1));
	georef->pixels[1] = VECT2(atoi(x2), atoi(y2));
	georef->pos[0] = GEO_POS2(atof(lat1), atof(lon1));
	georef->pos[1] = GEO_POS2(atof(lat2), atof(lon2));

	insets = jsmn_path_lookup_format(json, toks, n_toks,
	    "charts/[%d]/bounding_boxes/insets", i);
	for (int j = 0; insets != NULL && j < insets->size &&
	    georef->n_insets < ARRAY_NUM_ELEM(georef->insets); j++) {
		chart_bbox_t *bbox = &georef->insets[georef->n_insets];
		if (!jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/bounding_boxes/insets/[%d]/pixels/x1",
		    x1, sizeof (x1), i, j) ||
		    !jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/bounding_boxes/insets/[%d]/pixels/x2",
		    x2, sizeof (x2), i, j) ||
		    !jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/bounding_boxes/insets/[%d]/pixels/y1",
		    y1, sizeof (y1), i, j) ||
		    !jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/bounding_boxes/insets/[%d]/pixels/y2",
		    y2, sizeof (y2), i, j)) {
			continue;
		}
		bbox->pts[0] = VECT2(atoi(x1), atoi(y1));
		bbox->pts[1] = VECT2(atoi(x2), atoi(y2));
		georef->n_insets++;
	}
}

static void
parse_chart_view_data(const void *json, const jsmntok_t *toks, int n_toks,
    chart_t *chart, int i, chart_view_t view)
{
	static const char *view_names[] = {
	    [CHART_VIEW_HEADER] = "header",
	    [CHART_VIEW_PLANVIEW] = "planview",
	    [CHART_VIEW_PROFILE] = "profile",
	    [CHART_VIEW_MINIMUMS] = "minimums"
	};
	CTASSERT(ARRAY_NUM_ELEM(view_names) == NUM_CHART_VIEWS);
	const char *name;
	chart_bbox_t *bbox;
	char x1[8], x2[8], y1[8], y2[8];

	ASSERT(json != NULL);
	ASSERT(toks != NULL);
	ASSERT(chart != NULL);
	ASSERT3U(view, <, ARRAY_NUM_ELEM(view_names));
	name = view_names[view];
	bbox = &chart->views[view];

	memset(bbox, 0, sizeof (*bbox));
	if (!jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/%s/pixels/x1", x1, sizeof (x1), i, name) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/%s/pixels/x2", x2, sizeof (x2), i, name) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/%s/pixels/y1", y1, sizeof (y1), i, name) ||
	    !jsmn_get_tok_data_path_format(json, toks, n_toks, "charts/[%d]/"
	    "bounding_boxes/%s/pixels/y2", y2, sizeof (y2), i, name)) {
		return;
	}
	bbox->pts[0] = VECT2(atoi(x1), atoi(y1));
	bbox->pts[1] = VECT2(atoi(x2), atoi(y2));
}

static bool_t
parse_chart_json(const void *json, size_t len, chart_arpt_t *arpt)
{
	jsmn_parser parser;
	size_t max_toks = MAX(len / 4, 10000);
	jsmntok_t *toks = safe_calloc(max_toks, sizeof (*toks));
	int n_toks;
	const jsmntok_t *charts_tok;

	ASSERT(json != NULL);
	ASSERT(arpt != NULL);

	jsmn_init(&parser);
	n_toks = jsmn_parse(&parser, json, len, toks, max_toks);
	if (n_toks < 0) {
		logMsg("Error parsing airport %s: chart data doesn't "
		    "look like well-formed JSON", arpt->icao);
		goto errout;
	}
	charts_tok = jsmn_path_lookup(json, toks, n_toks, "charts");
	if (charts_tok == NULL) {
		logMsg("Error parsing airport %s: chart data JSON "
		    "has invalid structure", arpt->icao);
		goto errout;
	}
	for (int i = 0; i < charts_tok->size; i++) {
		char idx_nr[32], name[128], cat[8];
		char image_day[128], image_night[128];
		const jsmntok_t *procs_tok;
		chart_t *chart;

		if (!jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/index_number", idx_nr, sizeof (idx_nr), i) ||
		    !jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/name", name, sizeof (name), i) ||
		    !jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/category", cat, sizeof (cat), i) ||
		    !jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/image_day", image_day,
		    sizeof (image_day), i)) {
			continue;
		}
		jsmn_get_tok_data_path_format(json, toks, n_toks,
		    "charts/[%d]/image_night", image_night,
		    sizeof (image_night), i);

		chart = safe_calloc(1, sizeof (*chart));
		chart->arpt = arpt;
		/*
		 * Navigraph charts don't always contain a unique name,
		 * so to avoid name conflicts, we suffix the readable
		 * chart name with "##<index>". The apps using the
		 * interface must handle this case specially to avoid
		 * showing the suffix.
		 */
		chart->name = sprintf_alloc("%s##%s", name, idx_nr);
		if (strcmp(cat, "ARR") == 0) {
			chart->type = CHART_TYPE_STAR;
		} else if (strcmp(cat, "DEP") == 0) {
			chart->type = CHART_TYPE_DP;
		} else if (strstr(idx_nr, "0-9") ==
		    idx_nr + strlen(idx_nr) - 3) {
			/* Anything ending in "0-9" is an airport diagram */
			chart->type = CHART_TYPE_APD;
		} else if (strcmp(cat, "REF") == 0 || strcmp(cat, "APT") == 0) {
			chart->type = CHART_TYPE_INFO;
		} else if (strcmp(cat, "APP") == 0) {
			chart->type = CHART_TYPE_IAP;
		} else {
			chart->type = CHART_TYPE_UNKNOWN;
		}
		chart->codename = safe_strdup(idx_nr);
		chart->filename = safe_strdup(image_day);
		if (image_night[0] != '\0')
			chart->filename_night = safe_strdup(image_night);

		procs_tok = jsmn_path_lookup_format(json, toks, n_toks,
		    "charts/[%d]/procedures", i);
		for (int j = 0; procs_tok != NULL && j < procs_tok->size &&
		    chart->procs.n_procs < MAX_CHART_PROCS; j++) {
			if (jsmn_get_tok_data_path_format(json, toks, n_toks,
			    "charts/[%d]/procedures/[%d]",
			    chart->procs.procs[chart->procs.n_procs],
			    sizeof (*chart->procs.procs), i, j)) {
				chart->procs.n_procs++;
			}
		}
		parse_chart_georef_data(json, toks, n_toks, chart, i);
		for (chart_view_t view = 0; view < NUM_CHART_VIEWS; view++) {
			parse_chart_view_data(json, toks, n_toks, chart, i,
			    view);
		}
		if (!chartdb_add_chart(arpt, chart)) {
			/*
			 * Duplicate chart - unfortunately, Navigraph is
			 * prone to sending duplicates.
			 */
			logMsg("Chart error: airport %s contains duplicate "
			    "chart %s", arpt->icao, chart->name);
			chartdb_chart_destroy(chart);
		}
	}
	free(toks);
	return (B_TRUE);
errout:
	free(toks);
	return (B_FALSE);
}

chart_arpt_t *
chart_navigraph_arpt_lazy_discover(chartdb_t *cdb, const char *icao)
{
	char url[256];
	size_t len;
	void *json;
	navigraph_t *nav;
	chart_arpt_t *arpt = NULL;

	ASSERT(cdb != NULL);
	ASSERT(cdb->prov_priv != NULL);
	nav = cdb->prov_priv;
	ASSERT(icao != NULL);

	mutex_enter(&nav->lock);
	snprintf(url, sizeof (url), "https://api.navigraph.com/v1/charts/"
	    "airports/%s/signedurls/charts_v3_std.json", icao);
	json = dl_signed_url(cdb, nav, url, B_FALSE, &len);
	mutex_exit(&nav->lock);

	if (json != NULL) {
		arpt = parse_arpt_json(cdb, icao, json, len);
		free(json);
	}
	if (arpt != NULL) {
		mutex_enter(&nav->lock);
		snprintf(url, sizeof (url), "https://api.navigraph.com/v1/"
		    "charts/airports/%s/signedurls/charts_v3_std.json", icao);
		json = dl_signed_url(cdb, nav, url, B_FALSE, &len);
		mutex_exit(&nav->lock);

		if (json != NULL) {
			parse_chart_json(json, len, arpt);
			free(json);
		}
	}

	return (arpt);
}

bool_t
chart_navigraph_pending_ext_account_setup(chartdb_t *cdb)
{
	const navigraph_t *nav;

	ASSERT(cdb != NULL);
	/*
	 * We might be called *really* early can't rely on init_complete
	 * to be set before called here, so handle this case.
	 */
	if (cdb->prov_priv == NULL)
		return (B_FALSE);
	nav = cdb->prov_priv;

	return (nav->pending_ext_setup);
}
