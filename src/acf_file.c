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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <acfutils/acf_file.h>
#include <acfutils/avl.h>
#include <acfutils/helpers.h>
#include <acfutils/log.h>
#include <acfutils/safe_alloc.h>

/**
 * A single property entry of an ACF file.
 */
typedef struct {
	char		*name;	/**< Property name. */
	char		*value;	/**< Property value. */
	avl_node_t	node;
} acf_prop_t;

/**
 * This provides functionality to read and inspect the contents of an
 * .acf file of X-Plane. Use acf_file_read() to generate this structure
 * from an .acf file, and acf_file_free() to release it after use.
 */
struct acf_file {
	int		version;	/**< Version number of the file. */
	avl_tree_t	props;		/**< Tree of \ref acf_prop_t's. */
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

/**
 * Reads an X-Plane .acf file and returns a structure which can be used
 * to access its properties.
 * @param filename A full path to the .acf file to read.
 * @return A constructed .acf file in an accessible structure, or NULL
 *	on error. The exact error is emitted via logMsg.
 * @see acf_prop_find()
 * @see acf_file_get_version()
 */
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
	acf = safe_calloc(1, sizeof (*acf));
	avl_create(&acf->props, acf_prop_compar, sizeof (acf_prop_t),
	    offsetof(acf_prop_t, node));

	for (int line_num = 1; getline(&line, &cap, fp) > 0; line_num++) {
		char *name_end;
		acf_prop_t *prop;
		avl_index_t where;

		strip_space(line);
		if (line_num <= 3) {
			size_t n_comps;
			char **comps = strsplit(line, " ", B_TRUE, &n_comps);

			if (n_comps >= 2 && strcmp(comps[1], "Version") == 0) {
				acf->version = atoi(comps[0]);
			} else if (line_num == 3 &&
			    strcmp(comps[0], "ACF") != 0) {
				logMsg("Error reading acf file %s: missing "
				    "file header. Are you sure this is an "
				    "ACF file?", filename);
				free_strlist(comps, n_comps);
				goto errout;
			}
			free_strlist(comps, n_comps);
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

		name_end = strchr(&line[2], ' ');
		if (name_end == NULL) {
			logMsg("Error reading acf file %s:%d: bad parameter "
			    "line.", filename, line_num);
			goto errout;
		}
		prop = safe_calloc(1, sizeof (*prop));
		prop->name = safe_malloc(name_end - line - 1);
		prop->value = safe_malloc(strlen(&name_end[1]) + 1);
		lacf_strlcpy(prop->name, &line[2], name_end - line - 1);
		lacf_strlcpy(prop->value, &name_end[1], strlen(&name_end[1]) +
		    1);
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

/**
 * Frees the structured returned by acf_file_read().
 */
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

/**
 * Locates a property in a parsed .acf file and returns its contents.
 *
 * @param acf The parsed acf file structure.
 * @param prop_path A nul-terminated string containing the full path
 *	of the property in the acf file.
 *
 * @return A pointer to a nul-terminated string containing the value
 *	of the property if found, or NULL if the property doesn't
 *	exist in the acf file.
 */
const char *
acf_prop_find(const acf_file_t *acf, const char *prop_path)
{
	const acf_prop_t srch = { .name = (char *)prop_path };
	acf_prop_t *prop = avl_find(&acf->props, &srch, NULL);

	if (prop == NULL)
		return (NULL);
	return (prop->value);
}

/**
 * Returns the version number of an ACF file read by acf_file_read().
 */
int
acf_file_get_version(const acf_file_t *acf)
{
	ASSERT(acf != NULL);
	return (acf->version);
}
