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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <acfutils/assert.h>
#include <acfutils/dr.h>

bool_t
dr_find(dr_t *dr, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(dr->name, sizeof (dr->name), fmt, ap);
	va_end(ap);

	dr->dr = XPLMFindDataRef(dr->name);
	if (dr->dr == NULL) {
		memset(dr, 0, sizeof (*dr));
		return (B_FALSE);
	}
	dr->type = XPLMGetDataRefTypes(dr->dr);
	VERIFY_MSG(dr->type & (xplmType_Int | xplmType_Float | xplmType_Double |
	    xplmType_IntArray | xplmType_FloatArray | xplmType_Data),
	    "dataref \"%s\" has bad type %x", dr->name, dr->type);
	dr->writable = XPLMCanWriteDataRef(dr->dr);
	return (B_TRUE);
}

int
dr_geti(dr_t *dr)
{
	if (dr->type & xplmType_Int)
		return (XPLMGetDatai(dr->dr));
	if (dr->type & xplmType_Float)
		return (XPLMGetDataf(dr->dr));
	if (dr->type & xplmType_Double)
		return (XPLMGetDatad(dr->dr));
	if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data)) {
		int i;
		VERIFY3U(dr_getvi(dr, &i, 0, 1), ==, 1);
		return (i);
	}
	VERIFY_MSG(0, "dataref \"%s\" has bad type %x", dr->name, dr->type);
}

void
dr_seti(dr_t *dr, int i)
{
	ASSERT_MSG(dr->writable, "%s", dr->name);
	if (dr->type & xplmType_Int) {
		XPLMSetDatai(dr->dr, i);
	} else if (dr->type & xplmType_Float) {
		XPLMSetDataf(dr->dr, i);
	} else if (dr->type & xplmType_Double) {
		XPLMSetDatad(dr->dr, i);
	} else if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data)) {
		dr_setvi(dr, &i, 0, 1);
	} else {
		VERIFY_MSG(0, "dataref \"%s\" has bad type %x",
		    dr->name, dr->type);
	}
}

double
dr_getf(dr_t *dr)
{
	if (dr->type & xplmType_Int)
		return (XPLMGetDatai(dr->dr));
	if (dr->type & xplmType_Float)
		return (XPLMGetDataf(dr->dr));
	if (dr->type & xplmType_Double)
		return (XPLMGetDatad(dr->dr));
	if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data)) {
		double f;
		VERIFY3U(dr_getvf(dr, &f, 0, 1), ==, 1);
		return (f);
	}
	VERIFY_MSG(0, "dataref \"%s\" has bad type %x", dr->name, dr->type);
}

void
dr_setf(dr_t *dr, double f)
{
	ASSERT_MSG(dr->writable, "%s", dr->name);
	if (dr->type & xplmType_Int)
		XPLMSetDatai(dr->dr, f);
	else if (dr->type & xplmType_Float)
		XPLMSetDataf(dr->dr, f);
	else if (dr->type & xplmType_Double)
		XPLMSetDatad(dr->dr, f);
	else if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data))
		dr_setvf(dr, &f, 0, 1);
	else
		VERIFY_MSG(0, "dataref \"%s\" has bad type %x",
		    dr->name, dr->type);
}

int
dr_getvi(dr_t *dr, int *i, unsigned off, unsigned num)
{
	if (dr->type & xplmType_IntArray)
		return (XPLMGetDatavi(dr->dr, i, off, num));
	if (dr->type & xplmType_FloatArray) {
		float f[num];
		int n = XPLMGetDatavf(dr->dr, f, off, num);
		for (int x = 0; x < n; x++)
			i[x] = f[x];
		return (n);
	}
	if (dr->type & xplmType_Data) {
		uint8_t u[num];
		int n = XPLMGetDatab(dr->dr, u, off, num);
		for (int x = 0; x < n; x++)
			i[x] = u[x];
		return (n);
	}
	VERIFY_MSG(0, "dataref \"%s\" has bad type %x", dr->name, dr->type);
}

void
dr_setvi(dr_t *dr, int *i, unsigned off, unsigned num)
{
	ASSERT_MSG(dr->writable, "%s", dr->name);
	if (dr->type & xplmType_IntArray) {
		XPLMSetDatavi(dr->dr, i, off, num);
	} else if (dr->type & xplmType_FloatArray) {
		float f[num];
		for (unsigned x = 0; x < num; x++)
			f[x] = i[x];
		XPLMSetDatavf(dr->dr, f, off, num);
	} else if (dr->type & xplmType_Data) {
		uint8_t u[num];
		for (unsigned x = 0; x < num; x++)
			u[x] = i[x];
		XPLMSetDatab(dr->dr, u, off, num);
	} else {
		VERIFY_MSG(0, "dataref \"%s\" has bad type %x",
		    dr->name, dr->type);
	}
}

int
dr_getvf(dr_t *dr, double *df, unsigned off, unsigned num)
{
	if (dr->type & xplmType_IntArray) {
		int i[num];
		int n = XPLMGetDatavi(dr->dr, i, off, num);
		for (int x = 0; x < n; x++)
			df[x] = i[x];
		return (n);
	}
	if (dr->type & xplmType_FloatArray) {
		float f[num];
		int n = XPLMGetDatavf(dr->dr, f, off, num);
		for (int x = 0; x < n; x++)
			df[x] = f[x];
		return (n);
	}
	if (dr->type & xplmType_Data) {
		uint8_t u[num];
		int n = XPLMGetDatab(dr->dr, u, off, num);
		for (int x = 0; x < n; x++)
			df[x] = u[x];
		return (n);
	}
	VERIFY_MSG(0, "dataref \"%s\" has bad type %x", dr->name, dr->type);
}

void
dr_setvf(dr_t *dr, double *df, unsigned off, unsigned num)
{
	ASSERT_MSG(dr->writable, "%s", dr->name);
	if (dr->type & xplmType_IntArray) {
		int i[num];
		for (unsigned x = 0; x < num; x++)
			i[x] = df[x];
		XPLMSetDatavi(dr->dr, i, off, num);
	} else if (dr->type & xplmType_FloatArray) {
		float f[num];
		for (unsigned x = 0; x < num; x++)
			f[x] = df[x];
		XPLMSetDatavf(dr->dr, f, off, num);
	} else if (dr->type & xplmType_Data) {
		uint8_t u[num];
		for (unsigned x = 0; x < num; x++)
			u[x] = df[x];
		XPLMSetDatab(dr->dr, u, off, num);
	} else {
		VERIFY_MSG(0, "dataref \"%s\" has bad type %x",
		    dr->name, dr->type);
	}
}

int
dr_gets(dr_t *dr, char *str, size_t cap)
{
	int n;

	ASSERT_MSG(dr->type & xplmType_Data, "%s", dr->name);
	n = XPLMGetDatab(dr->dr, str, 0, cap - 1);
	str[cap - 1] = 0;	/* ensure the string is properly terminated */

	return (n);
}

void
dr_sets(dr_t *dr, char *str)
{
	ASSERT_MSG(dr->type & xplmType_Data, "%s", dr->name);
	ASSERT_MSG(dr->writable, "%s", dr->name);
	XPLMSetDatab(dr->dr, str, 0, strlen(str));
}

static int
read_int_cb(void *refcon)
{
	int *ptr = refcon;
	return (*(ptr));
}

static void
write_int_cb(void *refcon, int value)
{
	int *ptr = refcon;
	*ptr = value;
}

static float
read_float_cb(void *refcon)
{
	float *ptr = refcon;
	return (*(ptr));
}

static void
write_float_cb(void *refcon, float value)
{
	float *ptr = refcon;
	*ptr = value;
}

static void
dr_create_common(dr_t *dr, void *value, int type, bool_t writable,
    void *read_cb, void *write_cb, const char *fmt, va_list ap)
{
	vsnprintf(dr->name, sizeof (dr->name), fmt, ap);
	dr->dr = XPLMRegisterDataAccessor(dr->name, type, writable,
	    read_cb, writable ? write_cb : NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, value,
	    writable ? value : NULL);
	VERIFY(dr->dr != NULL);
	dr->type = type;
	dr->writable = writable;
}

/*
 * Sets up an integer dataref that will read and optionally write to
 * an int*.
 */
void
dr_create_i(dr_t *dr, int *value, bool_t writable, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, value, xplmType_Int, writable, read_int_cb,
	    write_int_cb, fmt, ap);
	va_end(ap);
}

/*
 * Sets up a float dataref that will read and optionally write to
 * an float*.
 */
void
dr_create_f(dr_t *dr, float *value, bool_t writable, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, value, xplmType_Float, writable, read_float_cb,
	    write_float_cb, fmt, ap);
	va_end(ap);
}

/*
 * Destroys a dataref previously set up using dr_intf_add_{i,f}.
 */
void
dr_delete(dr_t *dr)
{
	XPLMUnregisterDataAccessor(dr->dr);
	memset(dr, 0, sizeof (*dr));
}
