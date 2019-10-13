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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#include <stdbool.h>
#include <stddef.h>

#include "acfutils/avl.h"
#include "acfutils/dr_cmd_reg.h"
#include "acfutils/safe_alloc.h"

static bool inited = false;
static avl_tree_t drs;
static avl_tree_t cmds;

typedef struct {
	XPLMCommandRef			cmd;
	XPLMCommandCallback_f		cb;
	bool				before;
	void				*refcon;
	avl_node_t			node;
} reg_cmd_t;

typedef struct {
	dr_t				*dr;
	avl_node_t			node;
} reg_dr_t;

static int
reg_dr_compar(const void *a, const void *b)
{
	const reg_dr_t *ra = a, *rb = b;

	if (ra->dr < rb->dr)
		return (-1);
	if (ra->dr > rb->dr)
		return (1);

	return (0);
}

static int
reg_cmd_compar(const void *a, const void *b)
{
	int res = memcmp(a, b, offsetof(reg_cmd_t, node));

	if (res < 0)
		return (-1);
	if (res > 0)
		return (1);

	return (0);
}

void
dcr_init(void)
{
	ASSERT(!inited);
	inited = true;

	avl_create(&drs, reg_dr_compar, sizeof (reg_dr_t),
	    offsetof(reg_dr_t, node));
	avl_create(&cmds, reg_cmd_compar, sizeof (reg_cmd_t),
	    offsetof(reg_cmd_t, node));
}

void
dcr_fini(void)
{
	reg_dr_t *dr;
	reg_cmd_t *cmd;
	void *cookie;

	if (!inited)
		return;
	inited = B_FALSE;

	cookie = NULL;
	while ((dr = avl_destroy_nodes(&drs, &cookie)) != NULL) {
		dr_delete(dr->dr);
		free(dr);
	}
	avl_destroy(&drs);

	cookie = NULL;
	while ((cmd = avl_destroy_nodes(&cmds, &cookie)) != NULL) {
		XPLMUnregisterCommandHandler(cmd->cmd, cmd->cb, cmd->before,
		    cmd->refcon);
		free(cmd);
	}
	avl_destroy(&cmds);
}

void
dcr_add_dr(dr_t *dr)
{
	reg_dr_t *rdr;
	avl_index_t where;

	ASSERT(inited);
	ASSERT(dr != NULL);

	rdr = safe_calloc(1, sizeof (*rdr));
	rdr->dr = dr;

	VERIFY_MSG(avl_find(&drs, rdr, &where) == NULL,
	    "Duplicate dataref registration for dr %s (%p)\n", dr->name, dr);
	avl_insert(&drs, rdr, where);
}

XPLMCommandRef
dcr_find_cmd(const char *cmdname, XPLMCommandCallback_f cb, bool before,
    void *refcon)
{
	reg_cmd_t *cmd;
	avl_index_t where;

	ASSERT(inited);
	ASSERT(cmdname != NULL);
	ASSERT(cb != NULL);

	cmd = safe_calloc(1, sizeof (*cmd));
	cmd->cmd = XPLMFindCommand(cmdname);
	if (cmd->cmd == NULL) {
		free(cmd);
		return (NULL);
	}
	cmd->cb = cb;
	cmd->before = before;
	cmd->refcon = refcon;

	XPLMRegisterCommandHandler(cmd->cmd, cb, before, refcon);

	VERIFY_MSG(avl_find(&cmds, cmd, &where) == NULL,
	    "Found duplicate registration of command %s with cb: %p  "
	    "before: %d,  refcon: %p", cmdname, cb, before, refcon);
	avl_insert(&cmds, cmd, where);

	return (cmd->cmd);
}

XPLMCommandRef
f_dcr_find_cmd(const char *cmdname, XPLMCommandCallback_f cb, bool before,
    void *refcon)
{
	XPLMCommandRef ref = dcr_find_cmd(cmdname, cb, before, refcon);
	VERIFY_MSG(ref != NULL, "Command %s not found", cmdname);
	return (ref);
}

XPLMCommandRef
dcr_create_cmd(const char *cmdname, const char *cmddesc,
    XPLMCommandCallback_f cb, bool before, void *refcon)
{
	XPLMCommandRef ref;

	ASSERT(inited);

	ref = XPLMCreateCommand(cmdname, cmddesc);
	VERIFY_MSG(ref != NULL,
	    "Cannot create command %s: XPLMCreateCommand failed", cmdname);

	return (f_dcr_find_cmd(cmdname, cb, before, refcon));
}
