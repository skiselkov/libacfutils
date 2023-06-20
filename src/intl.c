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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>

#include <XPLMUtilities.h>

#include <acfutils/assert.h>
#include <acfutils/avl.h>
#include <acfutils/intl.h>
#include <acfutils/helpers.h>

static avl_tree_t tbl;
static bool_t acfutils_xlate_inited = B_FALSE;

typedef struct {
	char		*msgid;
	char		*msgstr;
	avl_node_t	node;
} xlate_ent_t;

static int
xlate_compar(const void *a, const void *b)
{
	const xlate_ent_t *xa = a, *xb = b;
	int res = strcmp(xa->msgid, xb->msgid);

	if (res < 0)
		return (-1);
	else if (res == 0)
		return (0);
	else
		return (1);
}

/**
 * Initializes the internationalization engine. You must call this before
 * starting to use any translation routines contained in this subsystem.
 * @param po_file Path to a PO file containing translations. The format
 *	of this file must conform to the
 *	[PO file format](https://www.gnu.org/software/gettext/manual/html_node/PO-Files.html)
 *	used by the GNU gettext software. Please note that we only support
 *	the `msgid` and `msgstr` commands from the PO file format. The
 *	intl.h system in libacfutils isn't a full replacement or
 *	reimplementation of GNU gettext.
 * @return `B_TRUE` if the translation engine was initialized successfully,
 *	`B_FALSE` if not. Failures can happen only due to failure to read
 *	or parse the passed .po file. The exact error reason is dumped
 *	into the log file using logMsg().
 * #### Example PO File
 *```
 * # This is a comment
 * msgid "Can't start planner: pushback already in progress. Please "
 * "stop the pushback operation first."
 * msgstr "Não pode iniciar o planejador: pushback já em progresso. "
 * "Por favor primeiro pare a operação do pushback."
 *```
 */
bool_t
acfutils_xlate_init(const char *po_file)
{
	FILE *fp = NULL;
	char cmd[32];
	xlate_ent_t *e = NULL;

	ASSERT(!acfutils_xlate_inited);
	avl_create(&tbl, xlate_compar, sizeof (xlate_ent_t),
	    offsetof(xlate_ent_t, node));

	fp = fopen(po_file, "r");
	if (fp == NULL)
		goto errout;

	while (!feof(fp)) {
		char c;

		do {
			c = fgetc(fp);
		} while (isspace(c));

		if (c == EOF)
			break;

		ungetc(c, fp);

		switch (c) {
		case '#':
			do {
				c = fgetc(fp);
			} while (c != '\n' && c != '\r' && c != EOF);
			break;
		case '"': {
			char *str = parser_get_next_quoted_str(fp);
			if (e == NULL) {
				logMsg("malformed po file %s: out of place "
				    "quoted string found", po_file);
				free(str);
				goto errout;
			}
			if (e->msgid == NULL) {
				e->msgid = str;
			} else if (e->msgstr == NULL) {
				e->msgstr = str;
			} else {
				logMsg("malformed po file %s: too many strings "
				    "following msgid or msgstr", po_file);
				free(str);
				free(e->msgid);
				free(e->msgstr);
				free(e);
				goto errout;
			}
			break;
		}
		default:
			VERIFY3S(fscanf(fp, "%31s", cmd), ==, 1);
			if (strcmp(cmd, "msgid") == 0) {
				if (e != NULL) {
					if (e->msgid == NULL ||
					    e->msgstr == NULL) {
						logMsg("malformed po file %s: "
						    "incomplete msgid entry",
						    po_file);
						free(e->msgid);
						free(e);
						goto errout;
					}
					if (*e->msgid == 0) {
						free(e->msgid);
						free(e->msgstr);
						free(e);
					} else {
						avl_add(&tbl, e);
					}
				}
				e = calloc(1, sizeof (*e));
			} else if (strcmp(cmd, "msgstr") == 0) {
				if (e == NULL || e->msgid == NULL) {
					logMsg("malformed po file %s: "
					    "misplaced \"msgstr\" diretive",
					    po_file);
					free(e);
					goto errout;
				}
			} else {
				logMsg("maformed po file %s: unknown "
				    "directive \"%s\".", po_file, cmd);
				free(e->msgid);
				free(e->msgstr);
				free(e);
				goto errout;
			}
			break;
		}
	}

	if (e != NULL) {
		if (e->msgid == NULL || e->msgstr == NULL) {
			logMsg("malformed po file %s: incomplete msgid entry",
			    po_file);
			free(e->msgid);
			free(e);
			goto errout;
		}
		if (*e->msgid == 0) {
			free(e->msgid);
			free(e->msgstr);
			free(e);
		} else {
			avl_add(&tbl, e);
		}
	}

	fclose(fp);

	acfutils_xlate_inited = B_TRUE;
	return (B_TRUE);
errout:
	if (fp != NULL)
		fclose(fp);

	/* fake init success and immediately tear down */
	acfutils_xlate_inited = B_TRUE;
	acfutils_xlate_fini();
	return (B_FALSE);
}

/**
 * Deinitializes the internationalization support of libacfutils. This is
 * always safe to call, even if you didn't call acfutils_xlate_init(), and
 * it's safe to call multiple times.
 */
void
acfutils_xlate_fini(void)
{
	xlate_ent_t *ent;
	void *cookie = NULL;

	if (!acfutils_xlate_inited)
		return;

	while ((ent = avl_destroy_nodes(&tbl, &cookie)) != NULL) {
		free(ent->msgid);
		free(ent->msgstr);
		free(ent);
	}
	avl_destroy(&tbl);

	acfutils_xlate_inited = B_FALSE;
}

/**
 * Translates a message given a message ID string. This performs a lookup
 * in the PO file parsed in acfutils_xlate_init() for a matching `msgid`
 * stanza and returns the corresponding `msgstr` value. If no matching
 * `msgid` is found in the file (or acfutils_xlate_init() was never called),
 * the input msgid string is returned instead.
 * @note You can use the `_()` macro as a shorthand for a call to
 *	acfutils_xlate(). This provides a convenient in-line method to
 *	localize messages, e.g.:
 *```
 * // The message below will automatically be translated
 * logMsg(_("Hello World!"));
 *```
 * If acfutils_xlate_init() was called and a suitable translation exists,
 * the translated text will be printed. Otherwise, "Hello World!" will be
 * printed.
 */
const char *
acfutils_xlate(const char *msgid)
{
	const xlate_ent_t srch = { .msgid = (char *)msgid };
	const xlate_ent_t *ent;

	if (!acfutils_xlate_inited)
		return (msgid);

	ent = avl_find(&tbl, &srch, NULL);
	if (ent == NULL)
		return (msgid);
	else
		return (ent->msgstr);
}

/**
 * Translates an X-Plane language enum into a 2-letter ISO-639-1 code.
 * @param lang An XPLMLanguageCode enum as obtained from XPLMGetLanguage().
 * @return The ISO-639-1 2-letter language code corresponding to the
 *	language enum. If the language enum is unknown, returns "xx" instead.
 */
const char *
acfutils_xplang2code(int lang)
{
	switch (lang) {
	case xplm_Language_English:
		return ("en");
	case xplm_Language_French:
		return ("fr");
	case xplm_Language_German:
		return ("de");
	case xplm_Language_Italian:
		return ("it");
	case xplm_Language_Spanish:
		return ("es");
	case xplm_Language_Korean:
		return ("ko");
	case xplm_Language_Russian:
		return ("ru");
	case xplm_Language_Greek:
		return ("el");
	case xplm_Language_Japanese:
		return ("ja");
	case xplm_Language_Chinese:
		return ("ch");
	default:
		return ("xx");
	}
}
