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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <acfutils/acf_file.h>
#include <acfutils/avl.h>
#include <acfutils/helpers.h>
#include <acfutils/log.h>

typedef struct {
	char		*name;
	char		*value;
	avl_node_t	node;
} acf_prop_t;

struct acf_file {
	avl_tree_t	props;
};

static int
acf_prop_compar(const void *a, const void *b)
{
	const acf_prop_t *pa = a, *pb = b;
	int x = strcmp(pa->name, pb->name);
	if (x < 0)
		return (-1);
	else if (x == 0)
		return (0);
	return (1);
}

acf_file_t *
acf_file_read(const char *filename)
{
	acf_file_t *acf;
	FILE *fp = fopen(filename, "rb");
	char *line = NULL;
	size_t cap = 0;
	bool_t parsing_props = B_FALSE;

	if (fp == NULL) {
		logMsg("Error reading acf file %s: %s", filename,
		    strerror(errno));
		return (NULL);
	}
	acf = calloc(1, sizeof (*acf));
	avl_create(&acf->props, acf_prop_compar, sizeof (acf_prop_t),
	    offsetof(acf_prop_t, node));

	for (int line_num = 1; getline(&line, &cap, fp) > 0; line_num++) {
		char *name_end;
		acf_prop_t *prop;
		avl_index_t where;
		if (line_num <= 3) {
			if (line_num == 3 && strncmp(line, "ACF", 3) != 0) {
				logMsg("Error reading acf file %s: missing "
				    "file header. Are you sure this is an "
				    "ACF file?", filename);
				goto errout;
			}
			continue;
		}
		if (!parsing_props) {
			if (strncmp(line, "PROPERTIES_BEGIN", 16) == 0)
				parsing_props = B_TRUE;
			continue;
		}
		if (strncmp(line, "PROPERTIES_END", 14) == 0)
			break;
		if (strncmp(line, "P ", 2) != 0)
			continue;

		strip_space(line);
		name_end = strchr(&line[2], ' ');
		if (name_end == NULL) {
			logMsg("Error reading acf file %s:%d: bad parameter "
			    "line.", filename, line_num);
			goto errout;
		}
		prop = calloc(1, sizeof (*prop));
		prop->name = malloc(name_end - line - 1);
		prop->value = malloc(strlen(&name_end[1]) + 1);
		strlcpy(prop->name, &line[2], name_end - line - 1);
		strlcpy(prop->value, &name_end[1], strlen(&name_end[1]) + 1);
		if (avl_find(&acf->props, prop, &where) != NULL) {
			logMsg("Error reading acf file %s:%d duplicate "
			    "property \"%s\" found.", filename, line_num,
			    prop->name);
			free(prop->name);
			free(prop->value);
			free(prop);
			goto errout;
		}
		avl_insert(&acf->props, prop, where);
	}

	fclose(fp);
	return (acf);
errout:
	acf_file_free(acf);
	fclose(fp);
	return (NULL);
}

void
acf_file_free(acf_file_t *acf)
{
	acf_prop_t *prop;
	void *cookie = NULL;

	while ((prop = avl_destroy_nodes(&acf->props, &cookie)) != NULL) {
		free(prop->name);
		free(prop->value);
		free(prop);
	}
	avl_destroy(&acf->props);
	free(acf);
}

const char *
acf_prop_find(const acf_file_t *acf, const char *prop_path)
{
	const acf_prop_t srch = { .name = (char *)prop_path };
	acf_prop_t *prop = avl_find(&acf->props, &srch, NULL);

	if (prop == NULL)
		return (NULL);
	return (prop->value);
}
