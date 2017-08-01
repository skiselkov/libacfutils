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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <acfutils/avl.h>
#include <acfutils/conf.h>
#include <acfutils/helpers.h>
#include <acfutils/log.h>

struct conf {
	avl_tree_t	tree;
};

typedef struct {
	char		key[128];
	char		value[128];
	avl_node_t	node;
} conf_key_t;

static void
strtolower(char *str)
{
	for (; *str != 0; str++)
		*str = tolower(*str);
}

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

conf_t *
parse_conf(FILE *fp, int *errline)
{
	conf_t *conf;
	char *line = NULL;
	size_t linecap = 0;
	int linenum = 0;

	conf = calloc(1, sizeof (*conf));
	avl_create(&conf->tree, conf_key_compar, sizeof (conf_key_t),
	    offsetof(conf_key_t, node));

	while (!feof(fp)) {
		char *sep;
		conf_key_t srch;
		conf_key_t *ck;
		avl_index_t where;

		linenum++;
		if (getline(&line, &linecap, fp) <= 0)
			continue;
		strip_space(line);
		if (*line == 0)
			continue;

		if ((sep = strstr(line, "--")) != NULL ||
		    (sep = strstr(line, "#")) != NULL) {
			*sep = 0;
			strip_space(line);
			if (*line == 0)
				continue;
		}

		/* make the config file case-insensitive */
		strtolower(line);

		sep = strstr(line, "=");
		if (sep == NULL) {
			free_conf(conf);
			if (errline != NULL)
				*errline = linenum;
			return (NULL);
		}
		*sep = 0;

		strip_space(line);
		strip_space(&sep[1]);

		strlcpy(srch.key, line, sizeof (srch.key));
		ck = avl_find(&conf->tree, &srch, &where);
		if (ck == NULL) {
			/* if the key didn't exist yet, create a new one */
			ck = calloc(1, sizeof (*ck));
			strlcpy(ck->key, line, sizeof (ck->key));
			avl_insert(&conf->tree, ck, where);
		}
		strlcpy(ck->value, &sep[1], sizeof (ck->value));
	}

	free(line);

	return (conf);
}

void
free_conf(conf_t *conf)
{
	void *cookie = NULL;
	conf_key_t *ck;

	while ((ck = avl_destroy_nodes(&conf->tree, &cookie)) != NULL)
		free(ck);
	avl_destroy(&conf->tree);
	free(conf);
}

static conf_key_t *
conf_find(const conf_t *conf, const char *key)
{
	conf_key_t srch;
	strlcpy(srch.key, key, sizeof (srch.key));
	return (avl_find(&conf->tree, &srch, NULL));
}

bool_t
conf_get_str(const conf_t *conf, const char *key, const char **value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = ck->value;
	return (B_TRUE);
}

bool_t
conf_get_i(const conf_t *conf, const char *key, int *value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = atoi(ck->value);
	return (B_TRUE);
}

bool_t
conf_get_d(const conf_t *conf, const char *key, double *value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = atof(ck->value);
	return (B_TRUE);
}

bool_t
conf_get_b(const conf_t *conf, const char *key, bool_t *value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = (strcmp(ck->value, "true") == 0 ||
	    strcmp(ck->value, "1") == 0 ||
	    strcmp(ck->value, "yes") == 0);
	return (B_TRUE);
}
