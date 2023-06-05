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
	dr_t				dr;
	avl_node_t			node;
} reg_dr_t;

static int
reg_dr_compar(const void *a, const void *b)
{
	const reg_dr_t *ra = a, *rb = b;
	int res = strcmp(ra->dr.name, rb->dr.name);

	if (res < 0)
		return (-1);
	if (res > 0)
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

/**
 * Initializes the DCR machinery. This should be called before any of
 * the other DCR macros or functions are called - typically near the
 * top of your `XPluginEnable` or `XPluginStart` callbacks.
 */
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

/**
 * Deinitializes the DCR machinery. This should be called on plugin shutdown
 * and after you are done with all dataref manipulations, typically near the
 * bottom of your `XPluginEnable` or `XPluginStart` callbacks.
 *
 * This function will go through all aggregated datarefs and commands that
 * were created using the DCR family of macros and functions and
 * destroy/unregister them all as necessary.
 */
void
dcr_fini(void)
{
	reg_dr_t *rdr;
	reg_cmd_t *cmd;
	void *cookie;

	if (!inited)
		return;
	inited = B_FALSE;

	cookie = NULL;
	while ((rdr = avl_destroy_nodes(&drs, &cookie)) != NULL) {
		dr_delete(&rdr->dr);
		free(rdr);
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

/** Internal, do not call, use the `DCR_CREATE` macros instead */
void *
dcr_alloc_rdr(void)
{
	ASSERT(inited);
	return (safe_calloc(1, sizeof (reg_dr_t)));
}

/** Internal, do not call, use the `DCR_CREATE` macros instead */
dr_t *
dcr_get_dr(void *token)
{
	ASSERT(inited);
	ASSERT(token != NULL);
	return (&((reg_dr_t *)token)->dr);
}

/** Internal, do not call, use the `DCR_CREATE` macros instead */
void
dcr_insert_rdr(void *token)
{
	reg_dr_t *rdr;
	avl_index_t where;

	ASSERT(inited);
	ASSERT(token != NULL);
	rdr = token;
	ASSERT(rdr->dr.dr != NULL);

	VERIFY_MSG(avl_find(&drs, rdr, &where) == NULL,
	    "Duplicate dataref registration for dr %s\n", rdr->dr.name);
	avl_insert(&drs, rdr, where);
}

/**
 * Finds a command and register a callback to handle it, while registering
 * it with DCR, so the handler is automatically deregistered when you call
 * dcr_fini().
 * @param fmt A printf-style format string which will be evaluated to form
 *	the name of the command for the lookup. The variadic arguments to
 *	this function are used for the format specifiers in this string.
 * @param cb Callback function to register for the command handler.
 * @param before Flag describing whether the callback should be called before
 *	X-Plane handles the command, or after.
 * @param refcon Reference constant, which will be passed to the callback
 *	in the `refcon` argument.
 * @return Returns the `XPLMCommandRef` of the command, if found and the
 *	command has been registered successfully. Otherwise returns `NULL`.
 * @see https://www.man7.org/linux/man-pages/man3/printf.3.html
 */
XPLMCommandRef
dcr_find_cmd(const char *fmt, XPLMCommandCallback_f cb, bool before,
    void *refcon, ...)
{
	XPLMCommandRef ref;
	va_list ap;

	va_start(ap, refcon);
	ref = dcr_find_cmd_v(fmt, cb, before, refcon, ap);
	va_end(ap);

	return ref;
}

/**
 * Same as dcr_find_cmd(), but takes a `va_list` argument list for the
 * format arguments, instead of being variadic.
 * @see dcr_find_cmd()
 */
XPLMCommandRef
dcr_find_cmd_v(const char *fmt, XPLMCommandCallback_f cb, bool before,
    void *refcon, va_list ap)
{
	char *cmdname;
	reg_cmd_t *cmd;
	avl_index_t where;

	ASSERT(inited);
	ASSERT(fmt != NULL);
	ASSERT(cb != NULL);

	cmdname = vsprintf_alloc(fmt, ap);
	cmd = safe_calloc(1, sizeof (*cmd));
	cmd->cmd = XPLMFindCommand(cmdname);
	if (cmd->cmd == NULL) {
		free(cmd);
		free(cmdname);
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

	free(cmdname);

	return (cmd->cmd);
}

/**
 * Same as dcr_find_cmd(), but will cause a hard assertion failure if the
 * command doesn't exist. This is similar to the fcmd_find() and fdr_find()
 * functions.
 * @see dcr_find_cmd()
 */
XPLMCommandRef
f_dcr_find_cmd(const char *fmt, XPLMCommandCallback_f cb, bool before,
    void *refcon, ...)
{
	XPLMCommandRef ref;
	va_list ap;

	va_start(ap, refcon);
	ref = f_dcr_find_cmd_v(fmt, cb, before, refcon, ap);
	va_end(ap);

	return (ref);
}

/**
 * Same as f_dcr_find_cmd(), but takes a `va_list` argument list for the
 * format arguments, instead of being variadic.
 * @see f_dcr_find_cmd()
 * @see dcr_find_cmd()
 */
XPLMCommandRef
f_dcr_find_cmd_v(const char *fmt, XPLMCommandCallback_f cb, bool before,
    void *refcon, va_list ap)
{
	XPLMCommandRef ref;
	va_list ap2;

	va_copy(ap2, ap);
	ref = dcr_find_cmd_v(fmt, cb, before, refcon, ap);
	if (ref == NULL) {
		/* No need to dealloc here, we'll be crashing anyway */
		VERIFY_MSG(ref != NULL, "Command %s not found",
		    vsprintf_alloc(fmt, ap2));
	}
	va_end(ap2);

	return (ref);
}

/**
 * Creates a new command and registers a command handler for it in a single
 * step. This also registers the command handler with DCR, so the handler
 * is automatically deregistered when you call dcr_fini().
 * @param cmdname The name of the command to be registered. This must be
 *	unique to prevent unpredictable behavior.
 * @param cmddesc A human-readable description of the command to show in
 *	X-Plane's command assignment UI.
 * @param cb Callback function to register for the command handler.
 * @param before Flag describing whether the callback should be called before
 *	X-Plane handles the command, or after.
 * @param refcon Reference constant, which will be passed to the callback
 *	in the `refcon` argument.
 * @return Returns the `XPLMCommandRef` of the newly created command. If the
 *	command creation fails, this function causes a hard assertion failure.
 */
XPLMCommandRef
dcr_create_cmd(const char *cmdname, const char *cmddesc,
    XPLMCommandCallback_f cb, bool before, void *refcon)
{
	XPLMCommandRef ref;

	ASSERT(inited);

	ref = XPLMCreateCommand(cmdname, cmddesc);
	VERIFY_MSG(ref != NULL,
	    "Cannot create command %s: XPLMCreateCommand failed", cmdname);

	return (f_dcr_find_cmd("%s", cb, before, refcon, cmdname));
}
