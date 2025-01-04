/*OH
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
 * Copyright 2025 Saso Kiselkov. All rights reserved.
 */

#include <XPLMPlugin.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "acfutils/assert.h"
#define	__INCLUDED_FROM_DR_C__
#include "acfutils/dr.h"
#include "acfutils/safe_alloc.h"

#define	DRE_MSG_ADD_DATAREF	0x01000000
#define	DR_TYPE_CHECK(__check_type, __type)	\
	__check_type ## _MSG(dr->type & (__type), \
	    "dataref \"%s\" has bad type %x (%s:%d: %s)", dr->name, \
	    dr->type, filename, line, varname)
#define	DR_WRITE_CHECK(__check_type)	\
	__check_type ## _MSG(dr->writable, "dataref \"%s\" is not writable " \
	    "(%s:%d: %s)", dr->name, filename, line, varname)

static bool_t dre_plug_lookup_done = B_FALSE;
static XPLMPluginID dre_plug = XPLM_NO_PLUGIN_ID;	/* DataRefEditor */
static XPLMPluginID drt_plug = XPLM_NO_PLUGIN_ID;	/* DataRefTool */

bool_t
dr_find(dr_t *dr, const char *fmt, ...)
{
	va_list ap;

	memset(dr, 0, sizeof (*dr));

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

bool_t
dr_writable(dr_t *dr)
{
	return (dr->writable);
}

int
dr_geti_impl(const dr_t *dr, DR_DEBUG_VARS)
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
		VERIFY3S(dr_getvi_impl(dr, filename, line, varname, &i, 0, 1),
		    ==, 1);
		return (i);
	}
	VERIFY_MSG(0, "dataref \"%s\" has bad type %x (%s:%d: %s)", dr->name,
	    dr->type, filename, line, varname);
}

void
dr_seti_impl(const dr_t *dr, DR_DEBUG_VARS, int i)
{
	DR_WRITE_CHECK(VERIFY);
	if (dr->type & xplmType_Int) {
		XPLMSetDatai(dr->dr, i);
	} else if (dr->type & xplmType_Float) {
		XPLMSetDataf(dr->dr, i);
	} else if (dr->type & xplmType_Double) {
		XPLMSetDatad(dr->dr, i);
	} else if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data)) {
		dr_setvi_impl(dr, filename, line, varname, &i, 0, 1);
	} else {
		VERIFY_MSG(0, "dataref \"%s\" has bad type %x (%s:%d: %s)",
		    dr->name, dr->type, filename, line, varname);
	}
}

double
dr_getf_impl(const dr_t *dr, DR_DEBUG_VARS)
{
	if (dr->type & xplmType_Double)
		return (XPLMGetDatad(dr->dr));
	if (dr->type & xplmType_Float)
		return (XPLMGetDataf(dr->dr));
	if (dr->type & xplmType_Int)
		return (XPLMGetDatai(dr->dr));
	if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data)) {
		double f;
		VERIFY3U(dr_getvf_impl(dr, filename, line, varname, &f, 0, 1),
		    ==, 1);
		return (f);
	}
	VERIFY_MSG(0, "dataref \"%s\" has bad type %x (%s%d: %s)", dr->name,
	    dr->type, filename, line, varname);
}

void
dr_setf_impl(const dr_t *dr, DR_DEBUG_VARS, double f)
{
	DR_WRITE_CHECK(VERIFY);
	ASSERT_MSG(!isnan(f), "%s (%s%d: %s)", dr->name, filename, line,
	    varname);
	ASSERT_MSG(isfinite(f), "%s (%s%d: %s)", dr->name, filename, line,
	    varname);
	if (dr->type & xplmType_Double)
		XPLMSetDatad(dr->dr, f);
	else if (dr->type & xplmType_Float)
		XPLMSetDataf(dr->dr, f);
	else if (dr->type & xplmType_Int)
		XPLMSetDatai(dr->dr, f);
	else if (dr->type & (xplmType_FloatArray | xplmType_IntArray |
	    xplmType_Data))
		dr_setvf_impl(dr, filename, line, varname, &f, 0, 1);
	else
		VERIFY_MSG(0, "dataref \"%s\" has bad type %x (%s:%d: %s)",
		    dr->name, dr->type, filename, line, varname);
}

int
dr_getvi_impl(const dr_t *dr, DR_DEBUG_VARS, int *i, unsigned off, unsigned num)
{
	ASSERT(i != NULL || num == 0);

	if (dr->type & xplmType_IntArray)
		return (XPLMGetDatavi(dr->dr, i, off, num));
	if (dr->type & xplmType_FloatArray) {
		float *f = safe_malloc(num * sizeof (*f));
		int n = XPLMGetDatavf(dr->dr, num > 0 ? f : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				i[x] = f[x];
		}
		free(f);
		return (n);
	}
	if (dr->type & xplmType_Data) {
		uint8_t *u = safe_malloc(num * sizeof (*u));
		int n = XPLMGetDatab(dr->dr, num > 0 ? u : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				i[x] = u[x];
		}
		free(u);
		return (n);
	}
	ASSERT_MSG(off == 0, "Attempted read scalar dataref %s (type: %x) at "
	    "offset other than 0 (%d) (%s%d: %s)", dr->name, dr->type, off,
	    filename, line, varname);
	if (num != 0)
		i[0] = dr_geti_impl(dr, filename, line, varname);
	return (1);
}

void
dr_setvi_impl(const dr_t *dr, DR_DEBUG_VARS, int *i, unsigned off, unsigned num)
{
	ASSERT(i != NULL);
	DR_WRITE_CHECK(VERIFY);
	if (dr->type & xplmType_IntArray) {
		XPLMSetDatavi(dr->dr, i, off, num);
	} else if (dr->type & xplmType_FloatArray) {
		float *f = safe_malloc(num * sizeof (*f));
		for (unsigned x = 0; x < num; x++)
			f[x] = i[x];
		XPLMSetDatavf(dr->dr, f, off, num);
		free(f);
	} else if (dr->type & xplmType_Data) {
		uint8_t *u = safe_malloc(num * sizeof (*u));
		for (unsigned x = 0; x < num; x++)
			u[x] = i[x];
		XPLMSetDatab(dr->dr, u, off, num);
		free(u);
	} else {
		ASSERT_MSG(off == 0, "Attempted write scalar dataref %s "
		    "(type: %x) at offset other than 0 (%d) (%s:%d: %s)",
		    dr->name, dr->type, off, filename, line, varname);
		dr_seti_impl(dr, filename, line, varname, i[0]);
	}
}

int
dr_getvf_impl(const dr_t *dr, DR_DEBUG_VARS, double *df, unsigned off,
    unsigned num)
{
	ASSERT(df != NULL || num == 0);

	if (dr->type & xplmType_IntArray) {
		int *i = safe_malloc(num * sizeof (*i));
		int n = XPLMGetDatavi(dr->dr, num > 0 ? i : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				df[x] = i[x];
		}
		free(i);
		return (n);
	}
	if (dr->type & xplmType_FloatArray) {
		float *f = safe_malloc(num * sizeof (*f));
		int n = XPLMGetDatavf(dr->dr, num > 0 ? f : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				df[x] = f[x];
		}
		free(f);
		return (n);
	}
	if (dr->type & xplmType_Data) {
		uint8_t *u = safe_malloc(num * sizeof (*u));
		int n = XPLMGetDatab(dr->dr, num > 0 ? u : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				df[x] = u[x];
		}
		free(u);
		return (n);
	}
	ASSERT_MSG(off == 0, "Attempted read scalar dataref %s (type: %x) at "
	    "offset other than 0 (%d) (%s:%d: %s)", dr->name, dr->type, off,
	    filename, line, varname);
	if (num != 0)
		df[0] = dr_getf_impl(dr, filename, line, varname);
	return (1);
}

int
dr_getvf32_impl(const dr_t *dr, DR_DEBUG_VARS, float *ff, unsigned off,
    unsigned num)
{
	ASSERT(ff != NULL || num == 0);

	if (dr->type & xplmType_IntArray) {
		int *i = safe_malloc(num * sizeof (*i));
		int n = XPLMGetDatavi(dr->dr, num > 0 ? i : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				ff[x] = i[x];
		}
		free(i);
		return (n);
	}
	if (dr->type & xplmType_FloatArray) {
		int n = XPLMGetDatavf(dr->dr, ff, off, num);
		return (n);
	}
	if (dr->type & xplmType_Data) {
		uint8_t *u = safe_malloc(num * sizeof (*u));
		int n = XPLMGetDatab(dr->dr, num > 0 ? u : NULL, off, num);
		if (num != 0) {
			for (int x = 0; x < n; x++)
				ff[x] = u[x];
		}
		free(u);
		return (n);
	}
	ASSERT_MSG(off == 0, "Attempted read scalar dataref %s (type: %x) at "
	    "offset other than 0 (%d) (%s:%d: %s)", dr->name, dr->type, off,
	    filename, line, varname);
	if (num != 0)
		ff[0] = dr_getf_impl(dr, filename, line, varname);
	return (1);
}

void
dr_setvf_impl(const dr_t *dr, DR_DEBUG_VARS, double *df, unsigned off,
    unsigned num)
{
	ASSERT(df != NULL);
	DR_WRITE_CHECK(VERIFY);
	for (unsigned x = 0; x < num; x++) {
		ASSERT_MSG(!isnan(df[x]), "%s[%d]", dr->name, x);
		ASSERT_MSG(isfinite(df[x]), "%s[%d]", dr->name, x);
	}
	if (dr->type & xplmType_IntArray) {
		int *i = safe_malloc(num * sizeof (*i));
		for (unsigned x = 0; x < num; x++)
			i[x] = df[x];
		XPLMSetDatavi(dr->dr, i, off, num);
		free(i);
	} else if (dr->type & xplmType_FloatArray) {
		float *f = safe_malloc(num * sizeof (*f));
		for (unsigned x = 0; x < num; x++)
			f[x] = df[x];
		XPLMSetDatavf(dr->dr, f, off, num);
		free(f);
	} else if (dr->type & xplmType_Data) {
		uint8_t *u = safe_malloc(num * sizeof (*u));
		for (unsigned x = 0; x < num; x++)
			u[x] = df[x];
		XPLMSetDatab(dr->dr, u, off, num);
		free(u);
	} else {
		ASSERT_MSG(off == 0, "Attempted write scalar dataref %s "
		    "(type: %x) at offset other than 0 (%d) (%s:%d: %s)",
		    dr->name, dr->type, off, filename, line, varname);
		dr_setf_impl(dr, filename, line, varname, df[0]);
	}
}

void
dr_setvf32_impl(const dr_t *dr, DR_DEBUG_VARS, float *ff, unsigned off,
    unsigned num)
{
	ASSERT(ff != NULL);
	DR_WRITE_CHECK(VERIFY);
	for (unsigned x = 0; x < num; x++) {
		ASSERT_MSG(!isnan(ff[x]), "%s[%d]", dr->name, x);
		ASSERT_MSG(isfinite(ff[x]), "%s[%d]", dr->name, x);
	}
	if (dr->type & xplmType_IntArray) {
		int *i = safe_malloc(num * sizeof (*i));
		for (unsigned x = 0; x < num; x++)
			i[x] = ff[x];
		XPLMSetDatavi(dr->dr, i, off, num);
		free(i);
	} else if (dr->type & xplmType_FloatArray) {
		XPLMSetDatavf(dr->dr, ff, off, num);
	} else if (dr->type & xplmType_Data) {
		uint8_t *u = safe_malloc(num * sizeof (*u));
		for (unsigned x = 0; x < num; x++)
			u[x] = ff[x];
		XPLMSetDatab(dr->dr, u, off, num);
		free(u);
	} else {
		ASSERT_MSG(off == 0, "Attempted write scalar dataref %s "
		    "(type: %x) at offset other than 0 (%d) (%s:%d: %s)",
		    dr->name, dr->type, off, filename, line, varname);
		dr_setf_impl(dr, filename, line, varname, ff[0]);
	}
}

int
dr_gets_impl(const dr_t *dr, DR_DEBUG_VARS, char *str, size_t cap)
{
	int n;

	DR_TYPE_CHECK(VERIFY, xplmType_Data);
	n = XPLMGetDatab(dr->dr, str, 0, cap > 0 ? cap - 1 : 0);
	if (cap != 0)
		str[n] = '\0';	/* make sure it's properly terminated */

	return (n);
}

void
dr_sets_impl(const dr_t *dr, DR_DEBUG_VARS, char *str)
{
	DR_TYPE_CHECK(VERIFY, xplmType_Data);
	DR_WRITE_CHECK(VERIFY);
	XPLMSetDatab(dr->dr, str, 0, strlen(str));
}

int
dr_getbytes_impl(const dr_t *dr, DR_DEBUG_VARS, void *data, unsigned off,
    unsigned num)
{
	DR_TYPE_CHECK(VERIFY, xplmType_Data);
	return (XPLMGetDatab(dr->dr, data, off, num));
}

void
dr_setbytes_impl(const dr_t *dr, DR_DEBUG_VARS, void *data, unsigned off,
    unsigned num)
{
	DR_TYPE_CHECK(VERIFY, xplmType_Data);
	DR_WRITE_CHECK(VERIFY);
	XPLMSetDatab(dr->dr, data, off, num);
}

static int
read_int_cb(void *refcon)
{
	dr_t *dr = refcon;
	int value;
	ASSERT(dr != NULL);
	ASSERT_MSG(dr->type & xplmType_Int, "%s", dr->name);
	if (dr->read_scalar_cb != NULL && dr->read_scalar_cb(dr, &value))
		return (value);
	ASSERT_MSG(dr->value != NULL, "%s", dr->name);
	value = *(int *)dr->value;
	if (dr->read_cb != NULL)
		dr->read_cb(dr, &value);
	return (value);
}

static void
write_int_cb(void *refcon, int value)
{
	dr_t *dr = refcon;
	ASSERT(dr != NULL);
	ASSERT_MSG(dr->type & xplmType_Int, "%s", dr->name);
	ASSERT_MSG(dr->writable, "%s", dr->name);
	if (dr->write_scalar_cb != NULL && dr->write_scalar_cb(dr, &value))
		return;
	ASSERT_MSG(dr->value != NULL, "%s", dr->name);
	if (dr->write_cb != NULL)
		dr->write_cb(dr, &value);
	*(int *)dr->value = value;
}

static float
read_float_cb(void *refcon)
{
	dr_t *dr = refcon;
	ASSERT(dr != NULL);
	ASSERT_MSG(dr->type & xplmType_Float, "%s", dr->name);
	if (dr->wide_type) {
		double value;
		if (dr->read_scalar_cb != NULL &&
		    dr->read_scalar_cb(dr, &value)) {
			return (value);
		}
		ASSERT_MSG(dr->value != NULL, "%s", dr->name);
		value = *(double *)dr->value;
		if (dr->read_cb != NULL)
			dr->read_cb(dr, &value);
		return (value);
	} else {
		float value;
		if (dr->read_scalar_cb != NULL &&
		    dr->read_scalar_cb(dr, &value)) {
			return (value);
		}
		ASSERT_MSG(dr->value != NULL, "%s", dr->name);
		value = *(float *)dr->value;
		if (dr->read_cb != NULL)
			dr->read_cb(dr, &value);
		return (value);
	}
}

static void
write_float_cb(void *refcon, float value)
{
	dr_t *dr = refcon;
	ASSERT(dr != NULL);
	ASSERT_MSG(dr->type & xplmType_Float, "%s", dr->name);
	ASSERT_MSG(dr->writable, "%s", dr->name);
	if (dr->write_scalar_cb != NULL && dr->write_scalar_cb(dr, &value))
		return;
	ASSERT_MSG(dr->value != NULL, "%s", dr->name);
	if (dr->write_cb != NULL)
		dr->write_cb(dr, &value);
	if (dr->wide_type)
		*(double *)dr->value = value;
	else
		*(float *)dr->value = value;
}

#define	DEF_READ_ARRAY_CB(typename, xp_typename, type_sz) \
static int \
read_ ## typename ## _array_cb(void *refcon, typename *out_values, int off, \
    int count) \
{ \
	dr_t *dr = refcon; \
 \
	ASSERT(dr != NULL); \
	ASSERT_MSG(dr->type & xplmType_ ## xp_typename, "%s", dr->name); \
 \
	if (dr->read_array_cb != NULL) { \
		int ret = dr->read_array_cb(dr, out_values, off, count); \
		if (ret >= 0) \
			return (ret); \
	} \
	ASSERT_MSG(dr->value != NULL, "%s", dr->name); \
	if (out_values == NULL) \
		return (dr->count); \
	if (off < dr->count) { \
		count = MIN(count, dr->count - off); \
		if (dr->stride == 0) { \
			memcpy(out_values, dr->value + (off * type_sz), \
			    type_sz * count); \
		} else { \
			for (int i = 0; i < count; i++) { \
				memcpy((void *)out_values + (i * type_sz), \
				    dr->value + ((off + i) * dr->stride), \
				    type_sz); \
			} \
		} \
	} else { \
		return (0); \
	} \
 \
	return (count); \
}

#define	DEF_WRITE_ARRAY_CB(typename, xp_typename, type_sz) \
static void \
write_ ## typename ## _array_cb(void *refcon, typename *in_values, int off, \
    int count) \
{ \
	dr_t *dr = refcon; \
 \
	ASSERT(dr != NULL); \
	ASSERT_MSG(dr->type & xplmType_ ## xp_typename, "%s", dr->name); \
	ASSERT_MSG(dr->writable, "%s", dr->name); \
	ASSERT_MSG(in_values != NULL || count == 0, "%s", dr->name); \
 \
	if (dr->write_array_cb != NULL) { \
		dr->write_array_cb(dr, in_values, off, count); \
		return; \
	} \
	ASSERT_MSG(dr->value != NULL, "%s", dr->name); \
	if (off < dr->count) { \
		count = MIN(count, dr->count - off); \
		if (dr->stride == 0) { \
			memcpy(dr->value + (off * type_sz), in_values, \
			    type_sz * count); \
		} else { \
			for (int i = 0; i < count; i++) { \
				memcpy(dr->value + ((off + i) * dr->stride), \
				    ((void *)in_values) + (i * type_sz), \
				    type_sz); \
			} \
		} \
	} \
}

DEF_READ_ARRAY_CB(int, IntArray, sizeof (int))
DEF_WRITE_ARRAY_CB(int, IntArray, sizeof (int))
DEF_READ_ARRAY_CB(float, FloatArray, sizeof (float))
DEF_WRITE_ARRAY_CB(float, FloatArray, sizeof (float))
DEF_READ_ARRAY_CB(void, Data, sizeof (uint8_t))
DEF_WRITE_ARRAY_CB(void, Data, sizeof (uint8_t))

#undef	DEF_READ_ARRAY_CB
#undef	DEF_WRITE_ARRAY_CB

/*
 * For double arrays we can't use the normal array read/write callbacks,
 * because those use memcpy when stride == 0. For double arrays, we need
 * to convert each value as it enters/exits our interface.
 */
static int
read_double_array_cb(void *refcon, float *out_values, int off, int count)
{
	dr_t *dr = refcon;

	ASSERT(dr != NULL);
	ASSERT_MSG(dr->type & xplmType_FloatArray, "%s", dr->name);

	if (dr->read_array_cb != NULL) {
		int ret = dr->read_array_cb(dr, out_values, off, count);
		if (ret >= 0)
			return (ret);
	}
	ASSERT_MSG(dr->value != NULL, "%s", dr->name);
	if (out_values == NULL)
		return (dr->count);
	if (off < dr->count) {
		size_t stride =
		    (dr->stride != 0 ? dr->stride : sizeof (double));
		count = MIN(count, dr->count - off);
		for (int i = 0; i < count; i++) {
			out_values[i] =
			    *(double *)(dr->value + ((off + i) * stride));
		}
	} else {
		return (0);
	}

	return (count);
}

static void
write_double_array_cb(void *refcon, float *in_values, int off, int count)
{
	dr_t *dr = refcon;

	ASSERT(dr != NULL);
	ASSERT_MSG(dr->type & xplmType_FloatArray, "%s", dr->name);
	ASSERT_MSG(dr->writable, "%s", dr->name);
	ASSERT_MSG(in_values != NULL || count == 0, "%s", dr->name);

	if (dr->write_array_cb != NULL) {
		dr->write_array_cb(dr, in_values, off, count);
		return;
	}
	ASSERT_MSG(dr->value != NULL, "%s", dr->name);
	if (off < dr->count) {
		size_t stride =
		    (dr->stride != 0 ? dr->stride : sizeof (double));
		count = MIN(count, dr->count - off);
		for (int i = 0; i < count; i++) {
			*(double *)(dr->value + ((off + i) * stride)) =
			    in_values[i];
		}
	}
}

void
dr_array_set_stride(dr_t *dr, size_t stride)
{
	ASSERT(dr != NULL);
	dr->stride = stride;
}

static void
dr_create_common(dr_t *dr, XPLMDataTypeID type, void *value,
    dr_cfg_t cfg, bool_t wide_type, const char *fmt, va_list ap)
{
	ASSERT(dr != NULL);
	/*
	 * value can be NULL - the caller might be using callbacks to
	 * override value access.
	 */
	ASSERT(fmt != NULL);

	memset(dr, 0, sizeof (*dr));

	vsnprintf(dr->name, sizeof (dr->name), fmt, ap);

	dr->dr = XPLMRegisterDataAccessor(dr->name, type, cfg.writable,
	    read_int_cb, write_int_cb, read_float_cb, write_float_cb,
	    NULL, NULL, read_int_array_cb, write_int_array_cb,
	    wide_type ? read_double_array_cb : read_float_array_cb,
	    wide_type ? write_double_array_cb : write_float_array_cb,
	    read_void_array_cb, write_void_array_cb, dr, dr);

	VERIFY(dr->dr != NULL);
	dr->type = type;
	dr->writable = cfg.writable;
	dr->value = value;
	dr->count = cfg.count;
	dr->stride = cfg.stride;
	dr->read_cb = cfg.read_cb;
	dr->write_cb = cfg.write_cb;
	dr->read_scalar_cb = cfg.read_scalar_cb;
	dr->write_scalar_cb = cfg.write_scalar_cb;
	dr->read_array_cb = cfg.read_array_cb;
	dr->write_array_cb = cfg.write_array_cb;
	dr->cb_userinfo = cfg.cb_userinfo;
	dr->wide_type = wide_type;

	if (!dre_plug_lookup_done) {
		dre_plug = XPLMFindPluginBySignature(
		    "xplanesdk.examples.DataRefEditor");
		drt_plug = XPLMFindPluginBySignature(
		    "com.leecbaker.datareftool");
		dre_plug_lookup_done = B_TRUE;
	}
	if (dre_plug != XPLM_NO_PLUGIN_ID) {
		XPLMSendMessageToPlugin(dre_plug, DRE_MSG_ADD_DATAREF,
		    (void*)dr->name);
	}
	if (drt_plug != XPLM_NO_PLUGIN_ID) {
		XPLMSendMessageToPlugin(drt_plug, DRE_MSG_ADD_DATAREF,
		    (void*)dr->name);
	}
}

/*
 * Sets up an integer dataref that will read and optionally write to
 * an int*.
 */
void
dr_create_i(dr_t *dr, int *value, bool_t writable, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Int, value,
	    (dr_cfg_t){ .count = 1, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_i_cfg(dr_t *dr, int *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Int, value, cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

/*
 * Sets up a float dataref that will read and optionally write to
 * a float*.
 */
void
dr_create_f(dr_t *dr, float *value, bool_t writable, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Float, value,
	    (dr_cfg_t){ .count = 1, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_f_cfg(dr_t *dr, float *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Float, value, cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

/*
 * Sets up a float dataref that will read and optionally write to
 * a double*.
 */
void
dr_create_f64(dr_t *dr, double *value, bool_t writable, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Float, value,
	    (dr_cfg_t){ .count = 1, .writable = writable }, B_TRUE, fmt, ap);
	va_end(ap);
}

void
dr_create_f64_cfg(dr_t *dr, double *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Float, value, cfg, B_TRUE, fmt, ap);
	va_end(ap);
}

void
dr_create_vi(dr_t *dr, int *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_IntArray, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vi_cfg(dr_t *dr, int *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_IntArray, value,
	    cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf(dr_t *dr, float *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_FloatArray, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf_cfg(dr_t *dr, float *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_FloatArray, value, cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf64(dr_t *dr, double *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_FloatArray, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_TRUE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf64_cfg(dr_t *dr, double *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_FloatArray, value, cfg, B_TRUE, fmt, ap);
	va_end(ap);
}

void
dr_create_vi_autoscalar(dr_t *dr, int *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Int | xplmType_IntArray, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vi_autoscalar_cfg(dr_t *dr, int *value, dr_cfg_t cfg,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Int | xplmType_IntArray, value,
	    cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf_autoscalar(dr_t *dr, float *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Float | xplmType_FloatArray, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf_autoscalar_cfg(dr_t *dr, float *value, dr_cfg_t cfg,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Float | xplmType_FloatArray, value,
	    cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf64_autoscalar(dr_t *dr, double *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Double | xplmType_FloatArray, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_TRUE, fmt, ap);
	va_end(ap);
}

void
dr_create_vf64_autoscalar_cfg(dr_t *dr, double *value, dr_cfg_t cfg,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Double | xplmType_FloatArray, value,
	    cfg, B_TRUE, fmt, ap);
	va_end(ap);
}

void
dr_create_b(dr_t *dr, void *value, size_t n, bool_t writable,
    const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Data, value,
	    (dr_cfg_t){ .count = n, .writable = writable }, B_FALSE, fmt, ap);
	va_end(ap);
}

void
dr_create_b_cfg(dr_t *dr, void *value, dr_cfg_t cfg, const char *fmt, ...)
{
	ASSERT(dr != NULL);
	ASSERT(fmt != NULL);
	va_list ap;
	va_start(ap, fmt);
	dr_create_common(dr, xplmType_Data, value, cfg, B_FALSE, fmt, ap);
	va_end(ap);
}

/*
 * Destroys a dataref previously set up using dr_intf_add_{i,f}.
 */
void
dr_delete(dr_t *dr)
{
	if (dr->dr != NULL) {
		XPLMUnregisterDataAccessor(dr->dr);
		memset(dr, 0, sizeof (*dr));
	}
}

void *
dr_get_cb_userinfo(const dr_t REQ_PTR(dr))
{
	return (dr->cb_userinfo);
}
