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

#ifndef	_DR_H_
#define	_DR_H_

#include <stdlib.h>

#include <XPLMDataAccess.h>

#include <acfutils/helpers.h>
#include <acfutils/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DR_MAX_NAME_LEN	128

typedef struct dr dr_t;

struct dr {
	char		name[DR_MAX_NAME_LEN];
	XPLMDataRef	dr;
	XPLMDataTypeID	type;
	bool_t		writable;
	void		*value;
	ssize_t		count;
	void		(*read_cb)(dr_t *);
	void		(*write_cb)(dr_t *);
	void		(*read_array_cb)(dr_t *, void *, int, int);
	void		(*write_array_cb)(dr_t *, void *, int, int);
	void		*cb_userinfo;
};

bool_t dr_find(dr_t *dr, const char *fmt, ...) PRINTF_ATTR(2);
#define	fdr_find(dr, ...) \
	do { \
		if (!dr_find(dr, __VA_ARGS__)) { \
			char drname[DR_MAX_NAME_LEN]; \
			snprintf(drname, sizeof (drname), __VA_ARGS__); \
			VERIFY_MSG(0, "dataref \"%s\" not found", drname); \
		} \
	} while (0)

int dr_geti(dr_t *dr);
void dr_seti(dr_t *dr, int i);

double dr_getf(dr_t *dr);
void dr_setf(dr_t *dr, double f);

int dr_getvi(dr_t *dr, int *i, unsigned off, unsigned num);
void dr_setvi(dr_t *dr, int *i, unsigned off, unsigned num);

int dr_getvf(dr_t *dr, double *df, unsigned off, unsigned num);
void dr_setvf(dr_t *dr, double *df, unsigned off, unsigned num);

int dr_getvf32(dr_t *dr, float *ff, unsigned off, unsigned num);
void dr_setvf32(dr_t *dr, float *ff, unsigned off, unsigned num);

int dr_gets(dr_t *dr, char *str, size_t cap);
void dr_sets(dr_t *dr, char *str);

void dr_create_i(dr_t *dr, int *value, bool_t writable, const char *fmt, ...)
    PRINTF_ATTR(4);
void dr_create_f(dr_t *dr, float *value, bool_t writable, const char *fmt, ...)
    PRINTF_ATTR(4);
void dr_create_vi(dr_t *dr, int *value, size_t n, bool_t writable,
    const char *fmt, ...) PRINTF_ATTR(5);
void dr_create_vf(dr_t *dr, float *value, size_t n, bool_t writable,
    const char *fmt, ...) PRINTF_ATTR(5);
void dr_create_b(dr_t *dr, void *value, size_t n, bool_t writable,
    const char *fmt, ...) PRINTF_ATTR(5);
void dr_delete(dr_t *dr);

#ifdef	__cplusplus
}
#endif

#endif	/* _DR_H_ */
