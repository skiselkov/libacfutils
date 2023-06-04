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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <zlib.h>

#include <curl/curl.h>

#include "acfutils/assert.h"
#include "acfutils/avl.h"
#include "acfutils/base64.h"
#include "acfutils/conf.h"
#include "acfutils/helpers.h"
#include "acfutils/log.h"
#include "acfutils/safe_alloc.h"

/* For conf_get_da and conf_set_da. */
CTASSERT(sizeof (double) == sizeof (unsigned long long));

struct conf {
	avl_tree_t	tree;
};

typedef enum {
	CONF_KEY_STR,
	CONF_KEY_DATA
} conf_key_type_t;

typedef struct {
	char			*key;
	conf_key_type_t		type;
	union {
		char		*str;
		struct {
			void	*buf;
			size_t	sz;
		} data;
	};
	avl_node_t	node;
} conf_key_t;

static void conf_set_common(conf_t *conf, const char *key,
    const char *fmt, ...) PRINTF_ATTR(3);
static int conf_write_impl(const conf_t *conf, void *fp, size_t bufsz,
    bool_t compressed, bool_t is_buf);

static int
conf_key_compar(const void *a, const void *b)
{
	const conf_key_t *ka = a, *kb = b;
	int c = strcmp(ka->key, kb->key);
	if (c < 0)
		return (-1);
	else if (c == 0)
		return (0);
	else
		return (1);
}

/**
 * Creates an empty configuration. Set values using conf_set_* and write
 * to a file using conf_write,
 */
conf_t *
conf_create_empty(void)
{
	conf_t *conf = safe_calloc(1, sizeof (*conf));
	avl_create(&conf->tree, conf_key_compar, sizeof (conf_key_t),
	    offsetof(conf_key_t, node));
	return (conf);
}

/**
 * Creates a new configuration as a copy of an existing configuration.
 * @return A copy of `conf2` which mustbe freed by the caller using conf_free().
 */
conf_t *
conf_create_copy(const conf_t *conf2)
{
	conf_t *conf = conf_create_empty();

	ASSERT(conf2 != NULL);
	conf_merge(conf2, conf);

	return (conf);
}

/**
 * Given two configurations, takes all values in `conf_from' and inserts
 * them into `conf_to', in essence, merging the two configurations.
 * After this call, `conf_to' will contain all the values present in
 * `conf_from', as well as any pre-existing values that were already in
 * `conf_to'. If a value exists in both `conf_to' and `conf_from', the
 * value in `conf_from' replaces the value in `conf_to'.
 */
void
conf_merge(const conf_t *conf_from, conf_t *conf_to)
{
	ASSERT(conf_from != NULL);
	ASSERT(conf_to != NULL);

	for (const conf_key_t *key = avl_first(&conf_from->tree); key != NULL;
	    key = AVL_NEXT(&conf_from->tree, key)) {
		switch (key->type) {
		case CONF_KEY_STR:
			conf_set_str(conf_to, key->key, key->str);
			break;
		case CONF_KEY_DATA:
			conf_set_data(conf_to, key->key, key->data.buf,
			    key->data.sz);
			break;
		}
	}
}

/**
 * Frees a conf_t object and all of its internal resources.
 */
void
conf_free(conf_t *conf)
{
	void *cookie = NULL;
	conf_key_t *ck;

	while ((ck = avl_destroy_nodes(&conf->tree, &cookie)) != NULL) {
		free(ck->key);
		switch (ck->type) {
		case CONF_KEY_STR:
			free(ck->str);
			break;
		case CONF_KEY_DATA:
			free(ck->data.buf);
			break;
		default:
			VERIFY(0);
		}
		free(ck);
	}
	avl_destroy(&conf->tree);
	free(conf);
}

/**
 * Same as conf_read, but serves as a shorthand for reading directly from
 * a file path on disk. If the file cannot be opened for reading and errline
 * is not NULL, it will be set to -1.
 * @return The parsed configuration object, or `NULL` if reading failed.
 */
conf_t *
conf_read_file(const char *filename, int *errline)
{
	uint8_t gz_magic[2];
	FILE *fp = fopen(filename, "rb");
	conf_t *conf;

	if (fp == NULL) {
		if (errline != NULL)
			*errline = -1;
		return (NULL);
	}
	/*
	 * We automatically detect the 16-bit Gzip magic header. We know
	 * a valid conf file will never contain this header unless it really
	 * is Gzip compressed.
	 */
	if (fread(gz_magic, 1, sizeof (gz_magic), fp) != sizeof (gz_magic)) {
		if (errline != NULL)
			*errline = -1;
		fclose(fp);
		return (NULL);
	}
	rewind(fp);
	if (gz_magic[0] == 0x1f && gz_magic[1] == 0x8b) {
		gzFile gz_fp = gzopen(filename, "r");

		fclose(fp);
		if (gz_fp == NULL) {
			if (errline != NULL)
				*errline = -1;
			return (NULL);
		}
		conf = conf_read2(gz_fp, errline, B_TRUE);
		gzclose(gz_fp);
	} else {
		conf = conf_read2(fp, errline, B_FALSE);
		fclose(fp);
	}

	return (conf);
}

static inline void
ck_free_value(conf_key_t *ck)
{
	ASSERT(ck != NULL);
	switch (ck->type) {
	case CONF_KEY_STR:
		free(ck->str);
		ck->str = NULL;
		break;
	case CONF_KEY_DATA:
		free(ck->data.buf);
		memset(&ck->data, 0, sizeof (ck->data));
		break;
	default:
		VERIFY(0);
	}
}

/**
 * Parses a configuration from a file. The file is structured as a
 * series of "key = value" lines. The parser understands "#" and "--"
 * comments.
 *
 * @return The parsed conf_t object, or NULL in case an error was found.
 * If errline is not NULL, it is set to the line number where the error
 * was encountered.
 * @return Use conf_free() to free the returned structure.
 */
conf_t *
conf_read(FILE *fp, int *errline)
{
	return (conf_read2(fp, errline, B_FALSE));
}

static bool_t
conf_parse_line(char *line, conf_t *conf)
{
	char *sep;
	conf_key_t srch;
	conf_key_t *ck;
	avl_index_t where;
	conf_key_type_t type;
	bool unescape = false;

	ASSERT(line != NULL);
	ASSERT(conf != NULL);

	sep = strstr(line, "`");
	if (sep != NULL) {
		type = CONF_KEY_DATA;
		sep[0] = '\0';
	} else {
		sep = strstr(line, "%=");
		if (sep != NULL) {
			type = CONF_KEY_STR;
			sep[0] = '\0';
			sep++;
			sep[0] = '\0';
			unescape = true;
		} else {
			sep = strstr(line, "=");
			if (sep != NULL) {
				sep[0] = '\0';
				type = CONF_KEY_STR;
			} else {
				return (B_FALSE);
			}
		}
	}
	strip_space(line);
	strip_space(&sep[1]);

	srch.key = safe_malloc(strlen(line) + 1);
	srch.type = type;
	strcpy(srch.key, line);
	strtolower(srch.key);	/* keys are case-insensitive */
	ck = avl_find(&conf->tree, &srch, &where);
	if (ck == NULL) {
		/* if the key didn't exist yet, create a new one */
		ck = safe_calloc(1, sizeof (*ck));
		ck->key = srch.key;
		avl_insert(&conf->tree, ck, where);
	} else {
		/* key already exists, free the search one */
		free(srch.key);
	}
	ck_free_value(ck);
	ck->type = type;
	if (type == CONF_KEY_STR) {
		ck->str = safe_malloc(strlen(&sep[1]) + 1);
		strcpy(ck->str, &sep[1]);
		if (unescape)
			unescape_percent(ck->str);
	} else {
		size_t l = strlen(&sep[1]);
		ssize_t sz_est = BASE64_DEC_SIZE(l);
		ssize_t sz_dec;
		ck->data.buf = safe_malloc(sz_est);
		sz_dec = lacf_base64_decode((const uint8_t *)&sep[1],
		    l, ck->data.buf);
		if (sz_dec <= 0)
			return (B_FALSE);
		ck->data.sz = sz_dec;
	}
	return (B_TRUE);
}

/**
 * Parses a configuration from a file. The file is structured as a
 * series of "key = value" lines. The parser understands "#" and "--"
 * comments.
 *
 * @return The parsed conf_t object, or NULL in case an error was found.
 * If errline is not NULL, it is set to the line number where the error
 * was encountered.
 * @return Use conf_free() to free the returned structure.
 */
conf_t *
conf_read2(void *fp, int *errline, bool_t compressed)
{
	conf_t *conf;
	char *line = NULL;
	size_t linecap = 0;
	unsigned linenum = 0;
	ASSERT(fp != NULL);
	FILE *f_fp = compressed ? NULL : fp;
	gzFile gz_fp = compressed ? fp : NULL;

	conf = conf_create_empty();
	while (compressed ? !gzeof(gz_fp) : !feof(f_fp)) {
		if (compressed) {
			if (parser_get_next_gzline(gz_fp, &line, &linecap,
			    &linenum) <= 0) {
				break;
			}
		} else {
			if (parser_get_next_line(f_fp, &line, &linecap,
			    &linenum) <= 0) {
				break;
			}
		}
		if (!conf_parse_line(line, conf))
			goto errout;
	}
	free(line);
	return (conf);
errout:
	free(line);
	conf_free(conf);
	if (errline != NULL)
		*errline = linenum;
	return (NULL);
}

/**
 * Same as conf_read(), but takes an in-memory buffer containing the text
 * of the configuration.
 * @param buf The memory buffer containing the configuration file text.
 *	The buffer DOESN'T need to be NUL-terminated.
 * @param cap The number of bytes in `buf`.
 * @param errline Optional return argument, which will be the filled with
 *	the failing line number, if a parsing error occurs.
 * @return The parsed conf_t object, or NULL in case an error was found.
 * If errline is not NULL, it is set to the line number where the error
 * was encountered.
 * @return Use conf_free() to free the returned structure.
 */
conf_t *
conf_read_buf(const void *buf, size_t cap, int *errline)
{
	uint8_t *tmpbuf = NULL;
	const char *instr;
	conf_t *conf = conf_create_empty();
	size_t n_lines;
	char **lines;

	ASSERT(buf != NULL);
	ASSERT(cap != 0);
	/*
	 * Check if the input is NUL-terminated. If not, copy it into
	 * tmpbuf and place a NUL byte at the end manually.
	 */
	if (((const uint8_t *)buf)[cap - 1] == '\0') {
		instr = buf;
	} else {
		tmpbuf = safe_malloc(cap + 1);
		memcpy(tmpbuf, buf, cap);
		tmpbuf[cap] = '\0';
		instr = (char *)tmpbuf;
	}
	/*
	 * Split at line breaks. If the file uses \r\n line terminators,
	 * a strip_space of each line gets rid of the trailing \r.
	 */
	lines = strsplit(instr, "\n", true, &n_lines);
	for (size_t i = 0; i < n_lines; i++) {
		char *comment;
		/*
		 * Strip away comments
		 */
		comment = strchr(lines[i], '#');
		if (comment != NULL)
			*comment = '\0';
		comment = strstr(lines[i], "--");
		if (comment != NULL)
			*comment = '\0';
		strip_space(lines[i]);
		if (lines[i][0] != '\0') {
			if (!conf_parse_line(lines[i], conf)) {
				if (errline != NULL)
					*errline = i + 1;
				goto errout;
			}
		}
	}
	free_strlist(lines, n_lines);
	if (tmpbuf != NULL)
		free(tmpbuf);
	return (conf);
errout:
	free_strlist(lines, n_lines);
	if (tmpbuf != NULL)
		free(tmpbuf);
	conf_free(conf);
	return (NULL);
}

/**
 * Same as conf_write(), but serves as a shorthand for writing directly to
 * a file path on disk.
 */
bool_t
conf_write_file(const conf_t *conf, const char *filename)
{
	return (conf_write_file2(conf, filename, B_FALSE));
}

/**
 * Same as conf_write_file(), but lets you specify whether the output
 * should be Gzip-compressed.
 */
bool_t
conf_write_file2(const conf_t *conf, const char *filename, bool_t compressed)
{
	ssize_t res;
	char *filename_tmp;
	int rename_err = 0;

	ASSERT(conf != NULL);
	ASSERT(filename != NULL);
	/*
	 * Initially we write the file into a .tmp temporary file on the side.
	 * We when atomically replace the target file to avoid the possibility
	 * of writing an incomplete file.
	 */
	filename_tmp = sprintf_alloc("%s.tmp", filename);

	if (!compressed) {
		FILE *fp = fopen(filename_tmp, "wb");

		if (fp == NULL) {
			free(filename_tmp);
			return (B_FALSE);
		}
		res = (conf_write_impl(conf, fp, 0, B_FALSE, B_FALSE) >= 0);
		fclose(fp);
	} else {
		gzFile fp = gzopen(filename_tmp, "w");

		if (fp == NULL) {
			free(filename_tmp);
			return (B_FALSE);
		}
		res = (conf_write_impl(conf, fp, 0, B_TRUE, B_FALSE) >= 0);
		gzclose(fp);
	}
	if (res) {
#if	IBM
		/*
		 * Windows needs special handling, because it doesn't let us
		 * use rename for the replace operation.
		 */
		if (file_exists(filename, NULL)) {
			if (!ReplaceFileA(filename, filename_tmp, NULL,
			    REPLACEFILE_IGNORE_MERGE_ERRORS |
			    REPLACEFILE_IGNORE_ACL_ERRORS, NULL, NULL)) {
				win_perror(GetLastError(), "Error writing %s: "
				"ReplaceFile failed", filename);
			}
		} else {
			rename_err = rename(filename_tmp, filename);
		}
#else	/* !IBM */
		rename_err = rename(filename_tmp, filename);
#endif	/* !IBM */
		if (rename_err != 0) {
			logMsg("Error writing %s: atomic rename failed: %s",
			    filename, strerror(errno));
			res = B_FALSE;
		}
	}
	free(filename_tmp);

	return (res);
}

/**
 * Writes a conf_t object to an in-memory buffer.
 * @param buf The buffer into which to write the configuration.
 *	This must be sized sufficiently large to contain the entire
 *	configuration file. Please note that the written buffer is not
 *	explicitly NUL-terminated, so you mustn't treat it as a char string.
 * @param cap Capacity of `buf` in bytes. This function will never write
 *	more than `cap` bytes, even if that means cutting the configuration
 *	file short. You should check the return value of the function and
 *	allocate enough space in `buf` to contain the entire configuration.
 * @return The number of bytes it would have taken to contain the entire
 *	configuration. Example how to allocate an appropriately-sized buffer
 *	first, before writing the configuration "for real":
 *```
 *	size_t len = conf_write_buf(conf, NULL, 0);
 *	void *buf = safe_malloc(len);
 *	conf_write_buf(conf, buf, len);
 *```
 */
size_t
conf_write_buf(const conf_t *conf, void *buf, size_t cap)
{
	return (conf_write_impl(conf, buf, cap, B_FALSE, B_TRUE));
}

static bool_t
needs_escape(const char *str)
{
	ASSERT(str != NULL);

	for (unsigned i = 0, n = strlen(str); i < n; i++) {
		if ((i == 0 && isspace(str[i])) ||
		    (i + 1 == n && isspace(str[i]))) {
			/* String begins and ends in whitespace */
			return (true);
		}
		if (str[i] < 32 || str[i] == '#' || str[i] == '%' ||
		    str[i] == '`') {
			/* String contains chars that could confuse us */
			return (true);
		}
	}
	return (false);
}

static int
conf_write_impl(const conf_t *conf, void *fp, size_t bufsz,
    bool_t compressed, bool_t is_buf)
{
#define	SNPRINTF_ADV(...) \
	do { \
		int req_here = snprintf(buf, (fp + bufsz) - buf, __VA_ARGS__); \
		ASSERT3S(req_here, >=, 0); \
		req_total += req_here; \
		buf = MIN(buf + req_here, fp + bufsz); \
	} while (0)

	char *data_buf = NULL;
	size_t cap = 0;
	ASSERT(fp != NULL || (is_buf && bufsz == 0));
	void *buf = (is_buf ? fp : NULL);
	FILE *f_fp = compressed ? NULL : fp;
	gzFile gz_fp = compressed ? fp : NULL;
	size_t req_total = 0;
	/* This is only used for generating escape sequences */
	CURL *curl = curl_easy_init();
	ASSERT(curl != NULL);

	ASSERT(conf != NULL);

	if (!compressed && !is_buf &&
	    fprintf(f_fp, "# libacfutils configuration file - "
	    "DO NOT EDIT!\n") < 0) {
		goto errout;
	}
	for (conf_key_t *ck = avl_first(&conf->tree); ck != NULL;
	    ck = AVL_NEXT(&conf->tree, ck)) {
		switch (ck->type) {
		case CONF_KEY_STR:
			if (!needs_escape(ck->str)) {
				if (is_buf) {
					SNPRINTF_ADV("%s = %s\n",
					    ck->key, ck->str);
				} else if ((compressed ?
				    gzprintf(gz_fp, "%s = %s\n", ck->key,
				    ck->str) < 0 :
				    fprintf(f_fp, "%s = %s\n", ck->key,
				    ck->str) < 0)) {
					goto errout;
				}
			} else {
				char *str = curl_easy_escape(curl, ck->str, 0);
				if (is_buf) {
					SNPRINTF_ADV("%s %%= %s\n",
					    ck->key, str);
				} else if ((compressed ?
				    gzprintf(gz_fp, "%s %%= %s\n", ck->key,
				    str) < 0 :
				    fprintf(f_fp, "%s %%= %s\n", ck->key,
				    str) < 0)) {
					curl_free(str);
					goto errout;
				}
				curl_free(str);
			}
			break;
		case CONF_KEY_DATA: {
			size_t req = BASE64_ENC_SIZE(ck->data.sz);
			size_t act;
			if (req > cap) {
				free(data_buf);
				cap = req;
				data_buf = safe_malloc(cap + 1);
			}
			act = lacf_base64_encode(ck->data.buf, ck->data.sz,
			    (uint8_t*)data_buf);
			data_buf[act] = '\0';
			if (is_buf) {
				SNPRINTF_ADV("%s`%s\n", ck->key, data_buf);
			} else if (compressed) {
				gzwrite(gz_fp, ck->key, strlen(ck->key));
				gzwrite(gz_fp, "`", 1);
				gzwrite(gz_fp, data_buf, strlen(data_buf));
				gzwrite(gz_fp, "\n", 1);
			} else {
				fprintf(f_fp, "%s`%s\n", ck->key, data_buf);
			}
			break;
		}
		default:
			VERIFY(0);
		}
	}
	free(data_buf);
	curl_easy_cleanup(curl);
	/* Add room for the terminating NUL byte */
	if (is_buf)
		req_total++;
	return (req_total);
errout:
	free(data_buf);
	curl_easy_cleanup(curl);
	return (-1);
#undef	SNPRINTF_ADV
}

/**
 * Writes a conf_t object to a file. Returns B_TRUE if the write was
 * successful, B_FALSE otherwise.
 */
bool_t
conf_write(const conf_t *conf, FILE *fp)
{
	return (conf_write_impl(conf, fp, 0, B_FALSE, B_TRUE));
}

/**
 * \internal
 * Looks for a pre-existing configuration key-value pair based on key name.
 * @return The conf_key_t object if found, NULL otherwise.
 */
static conf_key_t *
conf_find(const conf_t *conf, const char *key, avl_index_t *where)
{
	char buf[strlen(key) + 1];
	const conf_key_t srch = { .key = buf };
	lacf_strlcpy(buf, key, sizeof (buf));
	strtolower(buf);
	return (avl_find(&conf->tree, &srch, where));
}

/**
 * Retrieves the string value of a configuration key. If found, the value
 * is placed in `value`.
 * @return B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_str(const conf_t *conf, const char *key, const char **value)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
	*value = ck->str;
	return (B_TRUE);
}

/*
 * Retrieves the 32-bit int value of a configuration key. If found, the value
 * is placed in `value`.
 * @return B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_i(const conf_t *conf, const char *key, int *value)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
	*value = atoi(ck->str);
	return (B_TRUE);
}

/**
 * Retrieves the 64-bit int value of a configuration key. If found, the value
 * is placed in `value`.
 * @return B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_lli(const conf_t *conf, const char *key, long long *value)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
	*value = atoll(ck->str);
	return (B_TRUE);
}

/**
 * Retrieves the 64-bit float value of a configuration key. If found, the value
 * is placed in `value`.
 * @return B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_d(const conf_t *conf, const char *key, double *value)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
	if (strcmp(ck->str, "nan") == 0) {
		*value = NAN;
		return (true);
	} else {
		return (sscanf(ck->str, "%lf", value) == 1);
	}
}

/**
 * Same as conf_get_d, but for float values.
 */
bool_t
conf_get_f(const conf_t *conf, const char *key, float *value)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
	if (strcmp(ck->str, "nan") == 0) {
		*value = NAN;
		return (true);
	} else {
		return (sscanf(ck->str, "%f", value) == 1);
	}
}

/**
 * Retrieves the 64-bit float value previously stored using conf_set_da. If
 * found, the value is placed in `value`.
 *
 * Due to a limitation in MinGW, we can't use '%a' here. Instead, we directly
 * write the binary representation of the double value. Since IEEE754 is used
 * on all our supported platforms, that makes it portable. As future-proofing
 * for big endian platforms, we always enforce storing the value in LE.
 *
 * @return B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_da(const conf_t *conf, const char *key, double *value)
{
	const conf_key_t *ck;
	unsigned long long x;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
#if	IBM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"	/* Workaround for MinGW crap */
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#endif	/* IBM */
	if (sscanf(ck->str, "%llx", &x) != 1)
		return (B_FALSE);
#if	IBM
#pragma GCC diagnostic pop
#endif
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	x = BSWAP64(x);
#endif
	memcpy(value, &x, sizeof (double));
	return (B_TRUE);
}

/**
 * Retrieves the boolean value of a configuration key. If found, the value
 * is placed in `value`.
 * @return B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_b(const conf_t *conf, const char *key, bool_t *value)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(value != NULL);
	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_STR)
		return (B_FALSE);
	*value = (strcmp(ck->str, "true") == 0 ||
	    strcmp(ck->str, "1") == 0 ||
	    strcmp(ck->str, "yes") == 0);
	return (B_TRUE);
}

/**
 * Same as conf_get_b(), but takes a C99-style `bool` type argument,
 * instead of libacfutils' own `bool_t` type.
 */
bool
conf_get_b2(const conf_t *conf, const char *key, bool *value)
{
	bool_t tmp;
	if (!conf_get_b(conf, key, &tmp))
		return (false);
	*value = tmp;
	return (true);
}

/**
 * Retrieves a binary buffer value of a configuration key.
 * @param buf The buffer which will be filled with the configuration data.
 *	You must pre-allocate the buffer appropriately to hold the data,
 *	otherwise it will be truncated.
 * @param cap The capacity of `buf` in bytes. This function will never write
 *	more than `cap` bytes to `buf`. Use the return value of this function
 *	to determine if the value was truncated.
 * @return The number of bytes that would have been written to `buf` if it
 *	had been large enough to contain the entire value. If the key doesn't
 *	exist in the configuration, returns 0 instead. Example how to
 *	allocate an appropriately-sized buffer first, before retrieving
 *	the configuration data "for real":
 *```
 *	size_t len = conf_get_data(conf, key, NULL, 0);
 *	void *buf = safe_malloc(len);
 *	if (conf_get_data(conf, key, buf, len) != 0) {
 *		... use data ...
 *	}
 *```
 */
size_t
conf_get_data(const conf_t *conf, const char *key, void *buf, size_t cap)
{
	const conf_key_t *ck;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ASSERT(buf != NULL || cap == 0);

	ck = conf_find(conf, key, NULL);
	if (ck == NULL || ck->type != CONF_KEY_DATA)
		return (0);
	ASSERT(ck->data.buf != NULL);
	ASSERT(ck->data.sz != 0);
	memcpy(buf, ck->data.buf, MIN(ck->data.sz, cap));

	return (ck->data.sz);
}

/**
 * Sets up a key-value pair in the conf_t structure with a string value.
 * @param key The key name for which to set the value. If the key already
 *	exists, it will be overwritten.
 * @param value The value to set for the new key. If you pass `NULL`,
 *	this will instead remove the key-value pair (if present).
 */
void
conf_set_str(conf_t *conf, const char *key, const char *value)
{
	conf_key_t *ck;
	avl_index_t where;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	ck = conf_find(conf, key, &where);
	if (ck == NULL) {
		if (value == NULL)
			return;
		ck = safe_calloc(1, sizeof (*ck));
		ck->key = safe_strdup(key);
		strtolower(ck->key);
		avl_insert(&conf->tree, ck, where);
	}
	ck_free_value(ck);
	if (value == NULL) {
		avl_remove(&conf->tree, ck);
		free(ck->key);
		free(ck);
		return;
	} else {
		ck->type = CONF_KEY_STR;
		ck->str = safe_strdup(value);
	}
}

/*
 * Common setter back-end for conf_set_{i,d,b}.
 */
static void
conf_set_common(conf_t *conf, const char *key, const char *fmt, ...)
{
	int n;
	avl_index_t where;
	conf_key_t *ck = conf_find(conf, key, &where);
	va_list ap1, ap2;

	va_start(ap1, fmt);
	va_copy(ap2, ap1);

	if (ck == NULL) {
		ck = safe_calloc(1, sizeof (*ck));
		ck->key = safe_strdup(key);
		strtolower(ck->key);
		avl_insert(&conf->tree, ck, where);
	} else {
		ck_free_value(ck);
	}
	ck->type = CONF_KEY_STR;
	n = vsnprintf(NULL, 0, fmt, ap1);
	ASSERT3S(n, >, 0);
	ck->str = safe_malloc(n + 1);
	(void) vsnprintf(ck->str, n + 1, fmt, ap2);
	va_end(ap1);
	va_end(ap2);
}

/**
 * Same as conf_set_str() but with an int value. Obviously this cannot
 * remove a key, use `conf_set_str(conf, key, NULL)` for that.
 */
void
conf_set_i(conf_t *conf, const char *key, int value)
{
	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	conf_set_common(conf, key, "%i", value);
}

/**
 * Same as conf_set_str() but with a long long value. Obviously this
 * cannot remove a key, use `conf_set_str(conf, key, NULL)` for that.
 */
void
conf_set_lli(conf_t *conf, const char *key, long long value)
{
	ASSERT(conf != NULL);
	ASSERT(key != NULL);
#if	IBM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"	/* Workaround for MinGW crap */
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#endif	/* IBM */
	conf_set_common(conf, key, "%lld", value);
#if	IBM
#pragma GCC diagnostic pop
#endif
}

/**
 * Same as conf_set_str() but with a double value. Obviously this cannot
 * remove a key, use `conf_set_str(conf, key, NULL)` for that.
 */
void
conf_set_d(conf_t *conf, const char *key, double value)
{
	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	if (isnan(value))
		conf_set_str(conf, key, "nan");
	else
		conf_set_common(conf, key, "%.15f", value);
}

/**
 * Same as conf_set_str() but with a float value. Obviously this cannot
 * remove a key, use `conf_set_str(conf, key, NULL)` for that.
 */
void
conf_set_f(conf_t *conf, const char *key, float value)
{
	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	if (isnan(value))
		conf_set_str(conf, key, "nan");
	else
		conf_set_common(conf, key, "%.12f", value);
}

/**
 * Same as conf_set_d(), but able to accurately represent the exact value of
 * a double argument. Obviously this cannot remove a key, use
 * `conf_set_str(conf, key, NULL)` for that.
 */
void
conf_set_da(conf_t *conf, const char *key, double value)
{
	unsigned long long x;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	memcpy(&x, &value, sizeof (value));
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	x = BSWAP64(x);
#endif
#if	IBM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"	/* Workaround for MinGW crap */
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#endif	/* IBM */
	conf_set_common(conf, key, "%llx", x);
#if	IBM
#pragma GCC diagnostic pop
#endif
}

/**
 * Same as conf_set_str() but with a `bool_t` value. Obviously this cannot
 * remove a key, use `conf_set_str(conf, key, NULL)` for that.
 */
void
conf_set_b(conf_t *conf, const char *key, bool_t value)
{
	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	conf_set_common(conf, key, "%s", value ? "true" : "false");
}

/**
 * Same as conf_set_b(), but using C99's native bool type.
 */
void
conf_set_b2(conf_t *conf, const char *key, bool value)
{
	ASSERT(conf != NULL);
	ASSERT(key != NULL);
	conf_set_common(conf, key, "%s", value ? "true" : "false");
}

/**
 * Same as conf_set_str(), but allows you to pass an arbitrary binary
 * data buffer to be set for the configuration key.
 * @param buf The data buffer to set for the key's value. If you pass
 *	`NULL` here instead, the key-value pair will be removed (if present).
 * @param sz The number of bytes in `buf`. If you pass 0 here instead,
 *	the key-value pair will be removed (if present).
 */
void
conf_set_data(conf_t *conf, const char *key, const void *buf, size_t sz)
{
	conf_key_t *ck;
	avl_index_t where;

	ASSERT(conf != NULL);
	ASSERT(key != NULL);

	ck = conf_find(conf, key, &where);
	if (buf == NULL || sz == 0) {
		if (ck != NULL) {
			avl_remove(&conf->tree, ck);
			ck_free_value(ck);
			free(ck->key);
			free(ck);
		}
		return;
	}
	if (ck == NULL) {
		ck = safe_calloc(1, sizeof (*ck));
		ck->key = safe_strdup(key);
		strtolower(ck->key);
		avl_insert(&conf->tree, ck, where);
	} else {
		ck_free_value(ck);
	}
	ck->type = CONF_KEY_DATA;
	ck->data.buf = safe_malloc(sz);
	memcpy(ck->data.buf, buf, sz);
	ck->data.sz = sz;
}

#define	VARIABLE_GET(getfunc, last_arg, ...) \
	do { \
		va_list ap, ap2; \
		int l; \
		char *key; \
		int64_t res; \
		va_start(ap, last_arg); \
		va_copy(ap2, ap); \
		l = vsnprintf(NULL, 0, fmt, ap); \
		key = safe_malloc(l + 1); \
		vsnprintf(key, l + 1, fmt, ap2); \
		res = getfunc(conf, key, __VA_ARGS__); \
		free(key); \
		va_end(ap); \
		va_end(ap2); \
		return (res); \
	} while (0)

#define	VARIABLE_SET(setfunc, last_arg, ...) \
	do { \
		va_list ap, ap2; \
		int l; \
		char *key; \
		va_start(ap, last_arg); \
		va_copy(ap2, ap); \
		l = vsnprintf(NULL, 0, fmt, ap); \
		key = safe_malloc(l + 1); \
		vsnprintf(key, l + 1, fmt, ap2); \
		setfunc(conf, key, __VA_ARGS__); \
		free(key); \
		va_end(ap); \
		va_end(ap2); \
	} while (0)

/**
 * Same as conf_get_str(), but allows passing in a printf-formatted argument
 * string in `fmt' and a variable number of arguments following `value'
 * to construct the configuration key name dynamically.
 */
bool_t
conf_get_str_v(const conf_t *conf, const char *fmt, const char **value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_str, value, value);
}

/**
 * Same as conf_get_i(), but with dynamic name-construction as conf_get_str_v().
 */
bool_t
conf_get_i_v(const conf_t *conf, const char *fmt, int *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_i, value, value);
}

/**
 * Same as conf_get_lli(), but with dynamic name-construction as
 * conf_get_str_v().
 */
bool_t
conf_get_lli_v(const conf_t *conf, const char *fmt, long long *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_lli, value, value);
}

/**
 * Same as conf_get_f(), but with dynamic name-construction as conf_get_str_v().
 */
bool_t
conf_get_f_v(const conf_t *conf, const char *fmt, float *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_f, value, value);
}

/**
 * Same as conf_get_d(), but with dynamic name-construction as conf_get_str_v().
 */
bool_t
conf_get_d_v(const conf_t *conf, const char *fmt, double *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_d, value, value);
}

/**
 * Same as conf_get_da(), but with dynamic name-construction as
 * conf_get_str_v().
 */
bool_t
conf_get_da_v(const conf_t *conf, const char *fmt, double *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_da, value, value);
}

/**
 * Same as conf_get_data(), but with dynamic name-construction as
 * conf_get_str_v().
 */
size_t
conf_get_data_v(const conf_t *conf, const char *fmt, void *buf,
    size_t cap, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(buf != NULL || cap == 0);
	VARIABLE_GET(conf_get_data, cap, buf, cap);
}

/**
 * Same as conf_get_b(), but with dynamic name-construction as conf_get_str_v().
 */
bool_t
conf_get_b_v(const conf_t *conf, const char *fmt, bool_t *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_b, value, value);
}

/**
 * Same as conf_get_b_v(), but using the C99 native bool type.
 */
bool
conf_get_b2_v(const conf_t *conf, const char *fmt, bool *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	ASSERT(value != NULL);
	VARIABLE_GET(conf_get_b2, value, value);
}

/*
 * Same as conf_set_str(), but with dynamic name-construction as
 * conf_get_str_v().
 */
void
conf_set_str_v(conf_t *conf, const char *fmt, const char *value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_str, value, value);
}

/**
 * Same as conf_set_i(), but with dynamic name-construction as conf_get_str_v().
 */
void
conf_set_i_v(conf_t *conf, const char *fmt, int value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_i, value, value);
}

/**
 * Same as conf_set_lli(), but with dynamic name-construction as
 * conf_get_str_v().
 */
void
conf_set_lli_v(conf_t *conf, const char *fmt, long long value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_lli, value, value);
}

/**
 * Same as conf_set_f(), but with dynamic name-construction as conf_get_str_v().
 */
void
conf_set_f_v(conf_t *conf, const char *fmt, double value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_f, value, value);
}

/**
 * Same as conf_set_d(), but with dynamic name-construction as conf_get_str_v().
 */
void
conf_set_d_v(conf_t *conf, const char *fmt, double value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_d, value, value);
}

/**
 * Same as conf_set_da(), but with dynamic name-construction as
 * conf_get_str_v().
 */
void
conf_set_da_v(conf_t *conf, const char *fmt, double value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_da, value, value);
}

/**
 * Same as conf_set_b(), but with dynamic name-construction as conf_get_str_v().
 */
void
conf_set_b_v(conf_t *conf, const char *fmt, bool_t value, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_b, value, value);
}

/**
 * Same as conf_set_data(), but with dynamic name-construction as
 * conf_get_str_v().
 */
void conf_set_data_v(conf_t *conf, const char *fmt, const void *buf,
    size_t sz, ...)
{
	ASSERT(conf != NULL);
	ASSERT(fmt != NULL);
	VARIABLE_SET(conf_set_data, sz, buf, sz);
}

/**
 * Walks all configuration key-value pairs. You must NOT add or remove
 * key-value pairs in the configuration during the walk.
 *
 * Caveat: this function skips data-type keys (created with conf_set_data()).
 *
 * @param cookie A helper pointer so the function knows how far it has
 *	progressed. You MUST set it to `NULL` before the first call.
 * @param key Return parameter, which will be filled with the next key
 *	it has found.
 * @param value Return parameter, which will be filled with the next
 *	string value it has found.
 * @return B_TRUE if the function has found another key-value pair and
 *	returned them in `key` and `value, or `B_FALSE` if no more key-value
 *	have been found.
 *
 * Proper usage of this function:
 *```
 *	void *cookie = NULL;
 *	const char *key, *value;
 *	while (conf_walk(conf, &key, &value, &cookie)) {
 *		... do something with key & value ...
 *	}
 *```
 */
API_EXPORT bool_t
conf_walk(const conf_t *conf, const char **key, const char **value,
    void **cookie)
{
	static conf_key_t eol;
	conf_key_t *ck = *cookie;

	if (ck == &eol) {
		/* end of tree */
		return (B_FALSE);
	}
	if (ck == NULL) {
		/* first call */
		ck = avl_first(&conf->tree);
		if (ck == NULL) {
			/* tree is empty */
			*cookie = &eol;
			return (B_FALSE);
		}
	}
	do {
		*key = ck->key;
		*value = ck->str;
		ck = AVL_NEXT(&conf->tree, ck);
		/* conf_walk is only meant for string keys */
	} while (ck != NULL && ck->type != CONF_KEY_STR);
	if (ck != NULL) {
		*cookie = ck;
	} else {
		/* end of tree */
		*cookie = &eol;
	}

	return (B_TRUE);
}
