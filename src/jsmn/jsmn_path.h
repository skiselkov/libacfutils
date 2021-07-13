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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#ifndef	_JSMN_PATH_H_
#define	_JSMN_PATH_H_

#include <stdarg.h>

#include "../acfutils/helpers.h"

#include "jsmn.h"

#ifdef __cplusplus
extern "C" {
#endif

static void
jsmn_unescape_string(char *str, const char *esc, char c)
{
	int len;
	ASSERT(str != NULL);
	ASSERT(esc != NULL);
	len = strlen(esc);
	while ((str = strstr(str, esc)) != NULL) {
		str[0] = c;
		memmove(&str[1], &str[len], strlen(&str[len]) + 1);
	}
}

static void
jsmn_unescape(char *str)
{
	ASSERT(str != NULL);
	jsmn_unescape_string(str, "\\n", '\n');
	jsmn_unescape_string(str, "\\/", '/');
	jsmn_unescape_string(str, "&amp;", '&');
	jsmn_unescape_string(str, "&gt;", '>');
	jsmn_unescape_string(str, "&lt;", '<');
	jsmn_unescape_string(str, "&tab;", '\t');
	jsmn_unescape_string(str, "&quot;", '"');
	jsmn_unescape_string(str, "&apos;", '\'');
}

static int
jsmn_count_toks_r(const jsmntok_t *toks, int n_toks, int cur_tok)
{
	int num = 1;

	ASSERT(toks != NULL);
	ASSERT3S(cur_tok, <, n_toks);

	switch (toks[cur_tok].type) {
	case JSMN_OBJECT: {
		const jsmntok_t *obj = &toks[cur_tok];
		cur_tok++;
		for (int i = 0; i < obj->size; i++) {
			int n;

			n = jsmn_count_toks_r(toks, n_toks, cur_tok);
			cur_tok += n;
			num += n;
			if (cur_tok >= n_toks)
				break;
			n = jsmn_count_toks_r(toks, n_toks, cur_tok);
			cur_tok += n;
			num += n;
			if (cur_tok >= n_toks)
				break;
		}
		break;
	}
	case JSMN_ARRAY: {
		const jsmntok_t *arr = &toks[cur_tok];
		cur_tok++;
		for (int i = 0; i < arr->size; i++) {
			int n = jsmn_count_toks_r(toks, n_toks, cur_tok);
			cur_tok += n;
			num += n;
			if (cur_tok >= n_toks)
				break;
		}
		break;
	}
	default:
		break;
	}
	return (num);
}

UNUSED_ATTR static const jsmntok_t *
jsmn_path_lookup(const char *json, const jsmntok_t *toks, int n_toks,
    const char *path)
{
	size_t n_comps;
	char **comps;
	int cur_tok = 0;

	ASSERT(json != NULL);
	ASSERT(toks != NULL);
	ASSERT(path != NULL);
	comps = strsplit(path, "/", B_TRUE, &n_comps);
	for (size_t i = 0; i < n_comps; i++) {
		if (comps[i][0] == '[') {
			const jsmntok_t *arr = &toks[cur_tok];
			int idx = atoi(&comps[i][1]);

			if (arr->type != JSMN_ARRAY || idx >= arr->size)
				return (NULL);
			cur_tok++;
			for (; idx > 0; idx--) {
				cur_tok += jsmn_count_toks_r(toks, n_toks,
				    cur_tok);
			}
		} else {
			bool_t found = B_FALSE;
			const jsmntok_t *obj = &toks[cur_tok];

			if (obj->type != JSMN_OBJECT)
				return (NULL);
			cur_tok++;
			for (int j = cur_tok, idx = 0; idx < obj->size &&
			    j + 1 < n_toks; idx++) {
				const jsmntok_t *key = &toks[j];
				unsigned len;

				if (key->type != JSMN_STRING)
					return (NULL);
				len = key->end - key->start;
				if (strlen(comps[i]) == len && strncmp(
				    &json[key->start], comps[i], len) == 0) {
					found = B_TRUE;
					cur_tok = j + 1;
					break;
				}
				j += 1 + jsmn_count_toks_r(toks, n_toks, j + 1);
			}
			if (!found)
				return (NULL);
		}
	}
	free_strlist(comps, n_comps);

	return (&toks[cur_tok]);
}

UNUSED_ATTR PRINTF_ATTR(4) static const jsmntok_t *
jsmn_path_lookup_format(const char *json, const jsmntok_t *toks, int n_toks,
    const char *format, ...)
{
	va_list ap;
	char *path;
	const jsmntok_t *tok;

	va_start(ap, format);
	path = vsprintf_alloc(format, ap);
	va_end(ap);
	tok = jsmn_path_lookup(json, toks, n_toks, path);
	free(path);

	return (tok);
}

UNUSED_ATTR static char *
jsmn_strdup_tok_data(const char *json, const jsmntok_t *tok)
{
	char *output;

	ASSERT(json != NULL);
	ASSERT(tok != NULL);
	ASSERT3S(tok->start, <=, tok->end);
	output = safe_calloc(1, (tok->end - tok->start) + 1);
	memcpy(output, &json[tok->start], tok->end - tok->start);
	if (tok->type == JSMN_STRING)
		jsmn_unescape(output);

	return (output);
}

UNUSED_ATTR static int
jsmn_get_tok_data(const char *json, const jsmntok_t *tok,
    char *outstr, size_t cap)
{
	ASSERT(json != NULL);
	ASSERT(tok != NULL);
	ASSERT3S(tok->start, <=, tok->end);
	ASSERT(outstr != NULL || cap == 0);
	if (cap != 0) {
		strlcpy(outstr, &json[tok->start], MIN((int)cap,
		    (tok->end - tok->start) + 1));
		if (tok->type == JSMN_STRING)
			jsmn_unescape(outstr);
	}

	return (tok->end - tok->start);
}

UNUSED_ATTR static bool_t
jsmn_get_tok_data_path(const char *json, const jsmntok_t *toks,
    int n_toks, const char *path, char *outstr, size_t cap)
{
	const jsmntok_t *tok;

	ASSERT(json != NULL);
	ASSERT(toks != NULL);
	ASSERT(path != NULL);
	ASSERT(outstr != NULL || cap == 0);

	tok = jsmn_path_lookup(json, toks, n_toks, path);
	if (tok != NULL) {
		jsmn_get_tok_data(json, tok, outstr, cap);
		return (B_TRUE);
	} else {
		if (cap != 0)
			outstr[0] = '\0';
		return (B_FALSE);
	}
}


UNUSED_ATTR PRINTF_ATTR2(4, 7) static bool_t
jsmn_get_tok_data_path_format(const char *json, const jsmntok_t *toks,
    int n_toks, const char *format, char *outstr, size_t cap, ...)
{
	va_list ap;
	char *path;
	bool_t result;

	va_start(ap, cap);
	path = vsprintf_alloc(format, ap);
	va_end(ap);
	result = jsmn_get_tok_data_path(json, toks, n_toks, path, outstr, cap);
	free(path);

	return (result);
}

#ifdef __cplusplus
}
#endif

#endif	/* _JSMN_PATH_H_ */
