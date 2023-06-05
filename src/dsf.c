/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>

#include <acfutils/assert.h>
#include <acfutils/compress.h>
#include <acfutils/dsf.h>
#include <acfutils/helpers.h>
#include <acfutils/safe_alloc.h>

#define	DSF_MAX_VERSION	1
#define	INDENT_DEPTH	4
#define	IDX_UNSET	((uint64_t)-1)

static dsf_atom_t *parse_atom(const uint8_t *buf, size_t bufsz,
    char reason[DSF_REASON_SZ], uint64_t abs_off);
static void free_atom(dsf_atom_t *atom);
static bool_t parse_atom_list(const uint8_t *buf, uint64_t bufsz,
    list_t *atoms, char reason[DSF_REASON_SZ], uint64_t abs_off);
static bool_t parse_prop_atom(dsf_atom_t *atom, char reason[DSF_REASON_SZ]);
static void destroy_prop_atom(dsf_atom_t *atom);
static void destroy_planar_numeric_atom(dsf_atom_t *atom);

typedef int (*cmd_parser_cb_t)(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);

typedef struct {
	dsf_cmd_t	cmd;
	cmd_parser_cb_t	cb;
} cmd_parser_info_t;

static int pool_sel_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int junct_off_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int set_defn_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int road_subt_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int obj_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int net_chain_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int poly_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int terr_patch_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int patch_tria_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);
static int comment_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb);

#define	DSF_CMD_ID_MAX	34
static cmd_parser_info_t cmd_parser_info[DSF_CMD_ID_MAX + 1] = {
	{ 0, NULL },
	{ DSF_POOL_SEL, pool_sel_cb },			/* 1 */
	{ DSF_JUNCT_OFFSET_SEL, junct_off_cb },		/* 2 */
	{ DSF_SET_DEFN8, set_defn_cb },			/* 3 */
	{ DSF_SET_DEFN16, set_defn_cb },		/* 4 */
	{ DSF_SET_DEFN32, set_defn_cb },		/* 5 */
	{ DSF_ROAD_SUBTYPE, road_subt_cb },		/* 6 */
	{ DSF_OBJ, obj_cb },				/* 7 */
	{ DSF_OBJ_RNG, obj_cb },			/* 8 */
	{ DSF_NET_CHAIN, net_chain_cb },		/* 9 */
	{ DSF_NET_CHAIN_RNG, net_chain_cb },		/* 10 */
	{ DSF_NET_CHAIN32, net_chain_cb },		/* 11 */
	{ DSF_POLY, poly_cb },				/* 12 */
	{ DSF_POLY_RNG, poly_cb },			/* 13 */
	{ DSF_NEST_POLY, poly_cb },			/* 14 */
	{ DSF_NEST_POLY_RNG, poly_cb },			/* 15 */
	{ DSF_TERR_PATCH, terr_patch_cb },		/* 16 */
	{ DSF_TERR_PATCH_FLAGS, terr_patch_cb },	/* 17 */
	{ DSF_TERR_PATCH_FLAGS_N_LOD, terr_patch_cb },	/* 18 */
	{ 0, NULL },					/* 19 = unused */
	{ 0, NULL },					/* 20 = unused */
	{ 0, NULL },					/* 21 = unused */
	{ 0, NULL },					/* 22 = unused */
	{ DSF_PATCH_TRIA, patch_tria_cb },		/* 23 */
	{ DSF_PATCH_TRIA_XPOOL, patch_tria_cb },	/* 24 */
	{ DSF_PATCH_TRIA_RNG, patch_tria_cb },		/* 25 */
	{ DSF_PATCH_TRIA_STRIP, patch_tria_cb },	/* 26 */
	{ DSF_PATCH_TRIA_STRIP_XPOOL, patch_tria_cb },	/* 27 */
	{ DSF_PATCH_TRIA_STRIP_RNG, patch_tria_cb },	/* 28 */
	{ DSF_PATCH_TRIA_FAN, patch_tria_cb },		/* 29 */
	{ DSF_PATCH_TRIA_FAN_XPOOL, patch_tria_cb },	/* 30 */
	{ DSF_PATCH_TRIA_FAN_RNG, patch_tria_cb },	/* 31 */
	{ DSF_COMMENT8, comment_cb },			/* 32 */
	{ DSF_COMMENT16, comment_cb },			/* 33 */
	{ DSF_COMMENT32, comment_cb }			/* 34 */
};

static const char *
data_type2str(dsf_data_type_t data_type)
{
	switch (data_type) {
	case DSF_DATA_SINT16:
		return ("s16");
	case DSF_DATA_UINT16:
		return ("u16");
	case DSF_DATA_SINT32:
		return ("s32");
	case DSF_DATA_UINT32:
		return ("u32");
	case DSF_DATA_SINT64:
		return ("s64");
	case DSF_DATA_UINT64:
		return ("u64");
	case DSF_DATA_FP32:
		return ("fp32");
	case DSF_DATA_FP64:
		return ("fp64");
	default:
		VERIFY(0);
	}
}

static inline uint16_t
read_u16(const uint8_t *buf)
{
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return (BSWAP16(*(uint16_t *)buf));
#else
	return (*(uint16_t *)buf);
#endif
}

static inline uint32_t
read_u32(const uint8_t *buf)
{
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return (BSWAP32(*(uint32_t *)buf));
#else
	return (*(uint32_t *)buf);
#endif
}

/**
 * Reads a DSF file from disk and provides a handle to it. This function
 * should be used as the first step to access the DSF data. If the file
 * is compressed on disk, it is decompressed in memory first.
 * @param filename The full file name & path to the DSF file on disk.
 * @return A handle to the open DSF file, if successful. If there was a
 *	failure in reading the file, this returns `NULL` instead. The
 *	reason for the failure is automatically reported using logMsg().
 */
dsf_t *
dsf_init(const char *filename)
{
	dsf_t *dsf = NULL;
	uint8_t *buf = NULL;
	ssize_t bufsz = filesz(filename);
	ssize_t readsz = 0;
	FILE *fp = fopen(filename, "rb");
	char reason[DSF_REASON_SZ];
	static const uint8_t magic[8] = {
	    'X', 'P', 'L', 'N', 'E', 'D', 'S', 'F'
	};

	if (bufsz < 12 + 16 || fp == NULL)
		goto errout;
	buf = safe_malloc(bufsz);

	if (fread(buf, 1, sizeof (magic), fp) != sizeof (magic))
		goto errout;
	readsz = sizeof (magic);

	if (memcmp(buf, magic, sizeof (magic)) == 0) {
		/* raw DSF, just read its contents */
		while (readsz < bufsz) {
			size_t n = fread(&buf[readsz], 1, bufsz - readsz, fp);
			if (n == 0)
				break;
			readsz += n;
		}
		if (!feof(fp) && readsz < bufsz) {
			logMsg("Error reading DSF %s: %s", filename,
			    strerror(errno));
			goto errout;
		}
		fclose(fp);
		fp = NULL;
	} else if (test_7z(buf, sizeof (magic))) {
		fclose(fp);
		fp = NULL;
		free(buf);
		buf = decompress_7z(filename, (size_t *)&bufsz);
		if (buf == NULL)
			goto errout;
	} else {
		goto errout;
	}

	dsf = dsf_parse(buf, bufsz, reason);
	if (dsf == NULL) {
		logMsg("Error parsing DSF %s: %s", filename, reason);
		goto errout;
	}
	/* No need to release `buf', the dsf_t has taken ownership of it. */

	return (dsf);
errout:
	free(buf);
	if (fp != NULL)
		fclose(fp);
	return (NULL);
}

static bool_t
parse_prop_atom(dsf_atom_t *atom, char reason[DSF_REASON_SZ])
{
	const char *payload_end =
	    (const char *)(atom->payload + atom->payload_sz);

	ASSERT3U(atom->id, ==, DSF_ATOM_PROP);

	list_create(&atom->prop_atom.props, sizeof (dsf_prop_t),
	    offsetof(dsf_prop_t, prop_node));
	atom->subtype_inited = B_TRUE;

	if (atom->payload_sz < 2) {
		snprintf(reason, DSF_REASON_SZ, "PROP atom too short");
		return (B_FALSE);
	}
	for (const char *name = (const char *)atom->payload;
	    name < payload_end;) {
		size_t name_len = strnlen(name, payload_end - name);
		size_t value_len;
		const char *value;
		dsf_prop_t *prop;

		if (name_len == (size_t)(payload_end - name)) {
			snprintf(reason, DSF_REASON_SZ, "PROP atom contains "
			    "an unterminated name string");
			return (B_FALSE);
		}
		value = name + name_len + 1;
		if (value >= payload_end) {
			snprintf(reason, DSF_REASON_SZ, "Last name-value pair "
			    "in PROP atom is missing the value");
			return (B_FALSE);
		}
		value_len = strnlen(value, payload_end - value);
		if (value_len == (size_t)(payload_end - value)) {
			snprintf(reason, DSF_REASON_SZ, "PROP atom contains "
			    "an unterminated value string");
			return (B_FALSE);
		}
		prop = safe_calloc(1, sizeof (*prop));
		prop->name = name;
		prop->value = value;
		list_insert_tail(&atom->prop_atom.props, prop);

		name = value + value_len + 1;
	}

	return (B_TRUE);
}

#define	CHECK_LEN(x) \
	do { \
		if (start + (x) > end) { \
			snprintf(reason, DSF_REASON_SZ, "planar numeric atom " \
			    "%c%c%c%c at %lx, plane %u contains too little " \
			    "data", DSF_ATOM_ID_PRINTF(atom), \
			    (unsigned long)atom->file_off, plane); \
			goto errout; \
		} \
	} while (0)
#define	REALLOC_INCR		(1 << 14)	/* items */
#define	RESIZE(buf, fill, cap, itemsz, add) \
	do { \
		if (fill + add > cap) { \
			cap += REALLOC_INCR; \
			buf = realloc(buf, cap * (itemsz)); \
		} \
	} while (0)

static unsigned
type2datalen(dsf_data_type_t data_type)
{
	switch (data_type) {
	case DSF_DATA_SINT16:
	case DSF_DATA_UINT16:
		return (2);
	case DSF_DATA_SINT32:
	case DSF_DATA_UINT32:
	case DSF_DATA_FP32:
		return (4);
	case DSF_DATA_SINT64:
	case DSF_DATA_UINT64:
	case DSF_DATA_FP64:
		return (8);
	default:
		VERIFY(0);
	}
}

static void *
rle_decode(dsf_atom_t *atom, unsigned plane, const uint8_t *start,
    const uint8_t *end, dsf_data_type_t data_type, uint32_t num_values,
    size_t *bytes_consumed, char reason[DSF_REASON_SZ])
{
	unsigned datalen = type2datalen(data_type);
	uint8_t *out = NULL;
	size_t out_cap = 0;
	size_t i = 0;

	for (uint32_t num = 0; num < num_values;) {
		unsigned repeat, cnt;

		CHECK_LEN(1);
		repeat = start[i];
		cnt = repeat & 0x7f;
		i++;

		RESIZE(out, num, out_cap, datalen, cnt);
		if (repeat & 0x80) {
			CHECK_LEN(datalen);
			/* repeating case */
			for (unsigned k = 0; k < cnt; k++) {
				memcpy(&out[num * datalen], &start[i], datalen);
				num++;
			}
			i += datalen;
		} else {
			CHECK_LEN(cnt * datalen);
			for (unsigned k = 0; k < cnt; k++) {
				memcpy(&out[num * datalen], &start[i], datalen);
				i += datalen;
				num++;
			}
		}
	}

	if (bytes_consumed != NULL)
		*bytes_consumed = i;
	return (out);
errout:
	free(out);
	return (NULL);
}

static void *
diff_decode(dsf_atom_t *atom, unsigned plane, const uint8_t *start,
    const uint8_t *end, dsf_data_type_t data_type, uint32_t num_values,
    size_t *bytes_consumed, char reason[DSF_REASON_SZ])
{
	unsigned datalen = type2datalen(data_type);
	void *out;

	CHECK_LEN(num_values * datalen);

	out = safe_calloc(datalen, num_values);

#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#error	"TODO: implement big-endian"
#endif

#define	DIFF_DEC(ctype) \
	do { \
		ctype prev = 0; \
		ctype *out_type = out; \
		for (size_t i = 0; start + i * sizeof (ctype) < end; i++) { \
			ctype val = *(ctype *)(start + i * sizeof (ctype)); \
			prev += val; \
			out_type[i] = prev; \
		} \
	} while (0)
	switch (data_type) {
	case DSF_DATA_SINT16:
		DIFF_DEC(int16_t);
		break;
	case DSF_DATA_UINT16:
		DIFF_DEC(uint16_t);
		break;
	case DSF_DATA_SINT32:
		DIFF_DEC(int32_t);
		break;
	case DSF_DATA_UINT32:
		DIFF_DEC(uint32_t);
		break;
	case DSF_DATA_SINT64:
		DIFF_DEC(int64_t);
		break;
	case DSF_DATA_UINT64:
		DIFF_DEC(uint64_t);
		break;
	case DSF_DATA_FP32:
		DIFF_DEC(float);
		break;
	case DSF_DATA_FP64:
		DIFF_DEC(double);
		break;
	default:
		VERIFY(0);
	}
#undef	DIFF_DEC

	if (bytes_consumed != NULL)
		*bytes_consumed = num_values * datalen;
	return (out);
errout:
	return (NULL);
}

static void *
raw_decode(dsf_atom_t *atom, unsigned plane, const uint8_t *start,
    const uint8_t *end, dsf_data_type_t data_type, uint32_t num_values,
    size_t *bytes_consumed, char reason[DSF_REASON_SZ])
{
	unsigned datalen = type2datalen(data_type);
	void *out;

	CHECK_LEN(num_values * datalen);

	out = safe_calloc(datalen, num_values);
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#error	"TODO: implement big-endian"
#else	/* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */
	memcpy(out, start, datalen * num_values);
#endif	/* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

	if (bytes_consumed != NULL)
		*bytes_consumed = num_values * datalen;
	return (out);
errout:
	return (NULL);
}

static ssize_t
parse_plane(dsf_atom_t *atom, unsigned plane, dsf_data_type_t data_type,
    const uint8_t *start, const uint8_t *end, char reason[DSF_REASON_SZ])
{
	void *out = NULL;
	size_t consumed = 0;
	uint32_t datacnt = atom->planar_atom.data_count;
	unsigned datalen = type2datalen(data_type);
	unsigned enc;

	CHECK_LEN(1);
	enc = start[0];
	start++;
	if (enc > 3) {
		snprintf(reason, DSF_REASON_SZ, "invalid encoding type %u for "
		    "planar numeric atom %c%c%c%c at %lx", enc,
		    DSF_ATOM_ID_PRINTF(atom), (unsigned long)atom->file_off);
		return (-1);
	}

	if (datacnt == 0)
		return (1);

	switch (enc) {
	case DSF_ENC_RAW:
		out = raw_decode(atom, plane, start, end, data_type, datacnt,
		    &consumed, reason);
		break;
	case DSF_ENC_DIFF:
		out = diff_decode(atom, plane, start, end, data_type,
		    datacnt, &consumed, reason);
		break;
	case DSF_ENC_DIFF | DSF_ENC_RLE: {
		void *tmp = rle_decode(atom, plane, start, end, data_type,
		    datacnt, &consumed, reason);
		if (tmp == NULL)
			return (-1);
		out = diff_decode(atom, plane, tmp, tmp + datacnt * datalen,
		    data_type, datacnt, NULL, reason);
		free(tmp);
		break;
	}
	default: {
		void *tmp;
		ASSERT3U(enc, ==, DSF_ENC_RLE);
		tmp = rle_decode(atom, plane, start, end, data_type, datacnt,
		    &consumed, reason);
		if (tmp == NULL)
			return (-1);
		out = raw_decode(atom, plane, start, end, data_type, datacnt,
		    &consumed, reason);
		free(tmp);
		break;
	}
	}

	if (out == NULL)
		return (-1);

	atom->planar_atom.data[plane] = out;

	return (consumed + 1);
errout:
	return (-1);
}

#undef	REALLOC_INCR
#undef	RESIZE
#undef	CHECK_LEN

static bool_t
parse_planar_numeric_atom(dsf_atom_t *atom, dsf_data_type_t data_type,
    char reason[DSF_REASON_SZ])
{
	dsf_planar_atom_t *pa = &atom->planar_atom;
	const uint8_t *plane_p = &atom->payload[5];
	const uint8_t *end = atom->payload + atom->payload_sz;

	if (atom->payload_sz < 5) {
		snprintf(reason, DSF_REASON_SZ, "invalid planar numeric atom "
		    "%c%c%c%c at %lx: not enough payload",
		    DSF_ATOM_ID_PRINTF(atom), (unsigned long)atom->file_off);
		return (B_FALSE);
	}

	pa->data_type = data_type;
	pa->data_count = read_u32(atom->payload);
	pa->plane_count = atom->payload[4];
	pa->data = safe_calloc(pa->plane_count, sizeof (*pa->data));
	atom->subtype_inited = B_TRUE;

	for (unsigned i = 0; i < pa->plane_count; i++) {
		ssize_t len = parse_plane(atom, i, data_type,
		    plane_p, end, reason);
		if (len < 0) {
			printf("plane parse error\n");
			return (B_FALSE);
		}
		plane_p += len;
	}

	if (plane_p != end) {
		snprintf(reason, DSF_REASON_SZ, "planar numeric atom %c%c%c%c "
		    "at %lx contained trailing garbage",
		    DSF_ATOM_ID_PRINTF(atom), (unsigned long)atom->file_off);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static bool_t
parse_demi_atom(dsf_atom_t *atom, char reason[DSF_REASON_SZ])
{
	const uint8_t *payload = atom->payload;

	if (atom->payload_sz != 20) {
		snprintf(reason, DSF_REASON_SZ, "Invalid payload size of "
		    "DEMI atom (%x, wanted 20)", atom->payload_sz);
		return (B_FALSE);
	}
	atom->demi_atom.version = *payload++;
	if (atom->demi_atom.version != 1) {
		snprintf(reason, DSF_REASON_SZ, "Unsupported DEMI atom "
		    "version %d", atom->demi_atom.version);
		return (B_FALSE);
	}
	atom->demi_atom.bpp = *payload++;
	atom->demi_atom.flags = read_u16(payload);
	payload += 2;
	atom->demi_atom.width = read_u32(payload);
	payload += 4;
	atom->demi_atom.height = read_u32(payload);
	payload += 4;
	atom->demi_atom.scale = *(float *)payload;
	payload += 4;
	atom->demi_atom.offset = *(float *)payload;

	atom->subtype_inited = B_TRUE;

	return (B_TRUE);
}

static void
destroy_planar_numeric_atom(dsf_atom_t *atom)
{
	ASSERT(atom->subtype_inited);
	for (unsigned i = 0; i < atom->planar_atom.plane_count; i++)
		free(atom->planar_atom.data[i]);
	free(atom->planar_atom.data);
	atom->planar_atom.data = NULL;
}

static void
destroy_prop_atom(dsf_atom_t *atom)
{
	dsf_prop_t *prop;

	ASSERT3U(atom->id, ==, DSF_ATOM_PROP);
	ASSERT(atom->subtype_inited);

	while ((prop = list_remove_head(&atom->prop_atom.props)) != NULL)
		free(prop);
	list_destroy(&atom->prop_atom.props);
}

static dsf_atom_t *
parse_atom(const uint8_t *buf, size_t bufsz, char reason[DSF_REASON_SZ],
    uint64_t abs_off)
{
	dsf_atom_t *atom = safe_calloc(1, sizeof (*atom));

	ASSERT(reason != NULL);
	list_create(&atom->subatoms, sizeof (dsf_atom_t),
	    offsetof(dsf_atom_t, atom_list));

	if (bufsz < 8)
		goto errout;
#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	atom->id = BSWAP32(read_u32(buf));
#else
	atom->id = read_u32(buf);
#endif
	atom->payload_sz = read_u32(&buf[4]) - 8;
	atom->payload = &buf[8];
	if (atom->payload_sz + 8 > bufsz) {
		snprintf(reason, DSF_REASON_SZ, "invalid atom at %lx, "
		    "size (%lx) is too large",
		    (unsigned long)abs_off, (unsigned long)atom->payload_sz);
		goto errout;
	}
	atom->file_off = abs_off;

	if (atom->id == DSF_ATOM_HEAD || atom->id == DSF_ATOM_DEFN ||
	    atom->id == DSF_ATOM_GEOD || atom->id == DSF_ATOM_DEMS) {
		if (!parse_atom_list(atom->payload, atom->payload_sz,
		    &atom->subatoms, reason, abs_off + 8))
			goto errout;
	} else if (atom->id == DSF_ATOM_PROP) {
		if (!parse_prop_atom(atom, reason))
			goto errout;
	} else if (atom->id == DSF_ATOM_POOL) {
		if (!parse_planar_numeric_atom(atom, DSF_DATA_UINT16, reason))
			goto errout;
	} else if (atom->id == DSF_ATOM_PO32) {
		if (!parse_planar_numeric_atom(atom, DSF_DATA_UINT32, reason))
			goto errout;
	} else if (atom->id == DSF_ATOM_DEMI) {
		if (!parse_demi_atom(atom, reason))
			goto errout;
	}

	return (atom);
errout:
	free_atom(atom);
	return (NULL);
}

static bool_t
parse_atom_list(const uint8_t *buf, uint64_t bufsz, list_t *atoms,
    char reason[DSF_REASON_SZ], uint64_t abs_off)
{
	ASSERT(reason != NULL);

	for (const uint8_t *atom_buf = buf; atom_buf < buf + bufsz;) {
		dsf_atom_t *atom = parse_atom(atom_buf,
		    (buf + bufsz) - atom_buf, reason,
		    abs_off + (atom_buf - buf));

		if (atom == NULL)
			return (B_FALSE);
		list_insert_tail(atoms, atom);
		atom_buf += atom->payload_sz + 8;
	}

	return (B_TRUE);
}

/**
 * Parses a decompressed DSF file from a memory buffer. You should generally
 * not need to use this function, unless you're streaming DSFs over the net.
 * Use dsf_init() for files store on disk.
 * @param buf A buffer containing the decompressed DSF file data.
 * @param bufsz Number of bytes in `buf`.
 * @param reason A return string, which will be filled with a human-readable
 *	failure reason in case parsing of the data fails. If the parse is
 *	successful, this buffer is left unaltered.
 * @return A handle to the parsed DSF data, if successful. If there was a
 *	failure parsing the data, this returns `NULL` instead. The
 *	reason for the failure is written into the `reason` argument.
 */
dsf_t *
dsf_parse(uint8_t *buf, size_t bufsz, char reason[DSF_REASON_SZ])
{
	dsf_t *dsf = safe_calloc(1, sizeof (*dsf));
	static const uint8_t magic[8] = {
	    'X', 'P', 'L', 'N', 'E', 'D', 'S', 'F'
	};

	VERIFY(reason != NULL);

	list_create(&dsf->atoms, sizeof (dsf_atom_t),
	    offsetof(dsf_atom_t, atom_list));

	if (bufsz < 12 + 16) {
		snprintf(reason, DSF_REASON_SZ,
		    "file is too short (%d) to be a valid DSF", (int)bufsz);
		goto errout;
	}
	if (memcmp(buf, magic, sizeof (magic)) != 0) {
		snprintf(reason, DSF_REASON_SZ,
		    "file premable missing DSF magic number");
		goto errout;
	}
	dsf->version = read_u32(&buf[8]);
	if (dsf->version > DSF_MAX_VERSION) {
		snprintf(reason, DSF_REASON_SZ,
		    "file version (%d) exceeds our max supported version (%d)",
		    dsf->version, DSF_MAX_VERSION);
		goto errout;
	}
	memcpy(dsf->md5sum, &buf[bufsz - 16], 16);
	if (!parse_atom_list(&buf[12], bufsz - (12 + 16), &dsf->atoms, reason,
	    12))
		goto errout;

	/* Set this last, this confirms our ownership of the data buffer. */
	dsf->data = buf;
	dsf->size = bufsz;

	return (dsf);
errout:
	dsf_fini(dsf);
	return (NULL);
}

static void
free_atom(dsf_atom_t *atom)
{
	dsf_atom_t *subatom;

	while ((subatom = list_remove_head(&atom->subatoms)) != NULL)
		free_atom(subatom);
	list_destroy(&atom->subatoms);

	if (atom->subtype_inited) {
		switch (atom->id) {
		case DSF_ATOM_PROP:
			destroy_prop_atom(atom);
			break;
		case DSF_ATOM_POOL:
		case DSF_ATOM_PO32:
			destroy_planar_numeric_atom(atom);
			break;
		case DSF_ATOM_DEMI:
			/* no-op */
			break;
		default:
			VERIFY(0);
		}
		atom->subtype_inited = B_FALSE;
	}

	free(atom);
}

/**
 * Destroys a DSF file handle which was previously created using either
 * dsf_init() or dsf_parse().
 */
void
dsf_fini(dsf_t *dsf)
{
	dsf_atom_t *atom;

	while ((atom = list_remove_head(&dsf->atoms)) != NULL)
		free_atom(atom);
	list_destroy(&dsf->atoms);
	free(dsf->data);
	free(dsf);
}

static void
dump_prop_atom(const dsf_atom_t *atom, char **str, size_t *len, int depth)
{
	char indent[(depth * INDENT_DEPTH) + 1];

	VERIFY3U(atom->id, ==, DSF_ATOM_PROP);

	memset(indent, ' ', depth * INDENT_DEPTH);
	indent[depth * INDENT_DEPTH] = '\0';

	for (dsf_prop_t *prop = list_head(&atom->prop_atom.props); prop != NULL;
	    prop = list_next(&atom->prop_atom.props, prop)) {
		append_format(str, len, "%s\"%s\" = \"%s\"\n",
		    indent, prop->name, prop->value);
	}
}

static void
dump_planar_atom(const dsf_atom_t *atom, char **str, size_t *len, int depth)
{
	char indent[(depth * INDENT_DEPTH) + 1];

	memset(indent, ' ', depth * INDENT_DEPTH);
	indent[depth * INDENT_DEPTH] = '\0';

	append_format(str, len, "%sdata type: %s\n"
	    "%snum items: %d\n"
	    "%snum planes: %d\n",
	    indent, data_type2str(atom->planar_atom.data_type),
	    indent, atom->planar_atom.data_count,
	    indent, atom->planar_atom.plane_count);
}

static void
dump_demi_atom(const dsf_atom_t *atom, char **str, size_t *len, int depth)
{
	char indent[(depth * INDENT_DEPTH) + 1];
	const char *data_type;

	memset(indent, ' ', depth * INDENT_DEPTH);
	indent[depth * INDENT_DEPTH] = '\0';

	switch (atom->demi_atom.flags & DEMI_DATA_MASK) {
	case DEMI_DATA_FP32:
		data_type = "float";
		break;
	case DEMI_DATA_SINT:
		data_type = "sint";
		break;
	case DEMI_DATA_UINT:
		data_type = "uint";
		break;
	default:
		data_type = "<unknown>";
		break;
	}

	append_format(str, len,
	    "%sversion: %d\n"
	    "%sbpp: %d\n"
	    "%stype: %s\n"
	    "%spostctr: %d\n"
	    "%sflags: %x\n"
	    "%swidth: %u\n"
	    "%sheight: %u\n"
	    "%sscale: %f\n"
	    "%soffset: %f\n",
	    indent, atom->demi_atom.version,
	    indent, atom->demi_atom.bpp,
	    indent, data_type,
	    indent, (atom->demi_atom.flags >> 2) & 1,
	    indent, atom->demi_atom.flags,
	    indent, atom->demi_atom.width,
	    indent, atom->demi_atom.height,
	    indent, atom->demi_atom.scale,
	    indent, atom->demi_atom.offset);
}

static void
dump_atom(const dsf_atom_t *atom, char **str, size_t *len, int depth)
{
	char indent[(depth * INDENT_DEPTH) + 1];

	memset(indent, ' ', depth * INDENT_DEPTH);
	indent[depth * INDENT_DEPTH] = '\0';

	append_format(str, len,
	    "%s%c%c%c%c\t%lx\n",
	    indent,
	    (atom->id & 0xff000000) >> 24,
	    (atom->id & 0xff0000) >> 16,
	    (atom->id & 0xff00) >> 8,
	    (atom->id & 0xff),
	    (unsigned long)atom->payload_sz);

	if (atom->id == DSF_ATOM_PROP)
		dump_prop_atom(atom, str, len, depth + 1);
	else if (atom->id == DSF_ATOM_POOL)
		dump_planar_atom(atom, str, len, depth + 1);
	else if (atom->id == DSF_ATOM_PO32)
		dump_planar_atom(atom, str, len, depth + 1);
	else if (atom->id == DSF_ATOM_DEMI)
		dump_demi_atom(atom, str, len, depth + 1);

	for (dsf_atom_t *subatom = list_head(&atom->subatoms); subatom != NULL;
	    subatom = list_next(&atom->subatoms, subatom))
		dump_atom(subatom, str, len, depth + 1);
}

/**
 * Generates a human-readable description of the contents of a DSF file.
 * @param dsf The DSF file to describe.
 * @return A NUL-terminated string describing the DSF file. The caller
 *	is reponsible for freeing this string using lacf_free().
 */
char *
dsf_dump(const dsf_t *dsf)
{
	char *str = NULL;
	size_t len = 0;

	append_format(&str, &len,
	    "Version: %d\n"
	    "Size: %lx\n"
	    "MD5: %02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x\n"
	    "Atoms:\n",
	    dsf->version, (unsigned long)dsf->size,
	    dsf->md5sum[0], dsf->md5sum[1], dsf->md5sum[2], dsf->md5sum[3],
	    dsf->md5sum[4], dsf->md5sum[5], dsf->md5sum[6], dsf->md5sum[7],
	    dsf->md5sum[8], dsf->md5sum[9], dsf->md5sum[10], dsf->md5sum[11],
	    dsf->md5sum[12], dsf->md5sum[13], dsf->md5sum[14], dsf->md5sum[15]);
	for (const dsf_atom_t *atom = list_head(&dsf->atoms); atom != NULL;
	    atom = list_next(&dsf->atoms, atom)) {
		dump_atom(atom, &str, &len, 1);
	}

	/* Cut off the last newline character */
	str[len - 1] = '\0';

	return (str);
}

/**
 * Performs a DSF atom lookup inside of a DSF file. The variadic part of
 * this function must be a list of 32-bit unsigned integers, and MUST be
 * terminated by a zero integer argument. The integers form an path of
 * DSF atom IDs to be searched in the DSF tree structure. If along the
 * way a part of the path sits in a list atom with multiple instances of
 * the same atom ID, this picks the first instance. To search through
 * lists of same-ID atoms, use dsf_lookup_v with a varying index number
 * for the relevant atom path segment.
 * @return The DSF atom at the provided path. If any part of the path was
 *	not found, this function returns `NULL` instead.
 * @see dsf_lookup_v
 */
const dsf_atom_t *
dsf_lookup(const dsf_t *dsf, ...)
{
	va_list ap;
	unsigned num_args = 0;
	uint32_t atom_id;
	dsf_lookup_t *lookup;
	const dsf_atom_t *atom;

	va_start(ap, dsf);
	while ((atom_id = va_arg(ap, uint32_t)) != 0) {
		(void) va_arg(ap, unsigned);
		num_args++;
	}
	va_end(ap);

	lookup = safe_calloc(num_args + 1, sizeof (*lookup));

	va_start(ap, dsf);
	for (unsigned i = 0; i < num_args; i++) {
		lookup[i].atom_id = va_arg(ap, uint32_t);
		lookup[i].idx = va_arg(ap, unsigned);
	}
	va_end(ap);

	atom = dsf_lookup_v(dsf, lookup);
	free(lookup);

	return (atom);
}

/**
 * Same as dsf_lookup(), but instead of taking a variadic list of atoms
 * to form a path, this takes a flat array of \ref dsf_lookup_t structures.
 * @param lookup An array of \ref dsf_lookup_t structures, each identifying
 *	a part of the path. The array MUST be terminated by a \ref
 *	dsf_lookup_t structure with a zero `atom_id` field. Use the `idx`
 *	field of the structures to disambiguate which instance of a subatom
 *	in a list atom the lookup is meant for.
 * @return The DSF atom at the provided path. If any part of the path was
 *	not found, this function returns `NULL` instead.
 * @see dsf_lookup()
 */
const dsf_atom_t *
dsf_lookup_v(const dsf_t *dsf, const dsf_lookup_t *lookup)
{
	const list_t *list = &dsf->atoms;
	dsf_atom_t *atom = NULL;

	for (int i = 0; lookup[i].atom_id != 0; i++) {
		unsigned seen = 0;
		const dsf_lookup_t *l = &lookup[i];

		for (atom = list_head(list); atom != NULL;
		    atom = list_next(list, atom)) {
			if (atom->id == l->atom_id) {
				if (l->idx == seen)
					break;
				seen++;
			}
		}
		/* Required atom not found. */
		if (atom == NULL)
			return (NULL);
		list = &atom->subatoms;
	}

	return (atom);
}

/**
 * Iterator to allow you to traverse a list of subatoms of a DSF atom.
 * @param parent The parent atom to use as the root of the iteration.
 *	We iterate through this atom's subatoms.
 * @param atom_id Atom ID to filter out. This function skips subatoms
 *	which do not have an ID which matches this atom.
 * @param prev The previous atom in the iteration. On first call, you should
 *	pass a `NULL` here, to initialize the search.
 * @return The next subatom inside of `parent` which matches the `atom_id`.
 *	If no matching subatoms are present in the parent, returns `NULL`.
 *
 * Example of iterating through all subatoms matching a particular ID:
 *```
 *	for (const dsf_atom_t *subatom = dsf_iter(parent, MY_ATOM_ID, NULL);
 *	    subatom != NULL; atom = dsf_iter(parent, MY_ATOM_ID, subatom)) {
 *		... work with subatom ...
 *	}
 *```
 */
const dsf_atom_t *
dsf_iter(const dsf_atom_t *parent, uint32_t atom_id, const dsf_atom_t *prev)
{
	for (const dsf_atom_t *atom = (prev != NULL ?
	    list_next(&parent->subatoms, prev) :
	    list_head(&parent->subatoms)); atom != NULL;
	    atom = list_next(&parent->subatoms, atom)) {
		if (atom->id == atom_id)
			return (atom);
	}
	return (NULL);
}

/**
 * Given a DSF file and callback list, iterates through all encoded commands
 * in the DSF file. You can use this to extract the command list in the DSF.
 * @param dsf The DSF file to operate on. This file must contain a CMDS atom.
 * @param user_cbs An array of callbacks, with the position in the array
 *	denoting what type of command this callback will be called for. You
 *	may leave positions in the array set to `NULL` if you are not
 *	interested in receiving a callback for a particular command type.
 * @param userinfo An optional pointer, which will be stored in the
 *	\ref dsf_cmd_parser_t structure in the `userinfo` field. You can
 *	extract the userinfo pointer from there.
 * @param reason A failure reason buffer, which will be filled with a
 *	human-readable failure description, if a parsing failure occurs.
 * @return `B_TRUE` if parsing of the command section was successful, or
 *	`B_FALSE` if not.
 */
bool_t
dsf_parse_cmds(const dsf_t *dsf, dsf_cmd_cb_t user_cbs[NUM_DSF_CMDS],
    void *userinfo, char reason[DSF_REASON_SZ])
{
	dsf_cmd_parser_t parser;
	const dsf_atom_t *cmds_atom;
	const uint8_t *payload, *payload_end;
	char subreason[DSF_REASON_SZ] = { 0 };

	memset(&parser, 0, sizeof (parser));

	parser.dsf = dsf;
	parser.junct_off = IDX_UNSET;
	parser.defn_idx = IDX_UNSET;
	parser.road_subt = IDX_UNSET;
	parser.userinfo = userinfo;
	parser.reason = subreason;

	cmds_atom = dsf_lookup(dsf, DSF_ATOM_CMDS, 0);
	if (cmds_atom == NULL) {
		if (reason != NULL)
			snprintf(reason, DSF_REASON_SZ, "CMDS atom not found");
		return (B_FALSE);
	}

	payload = cmds_atom->payload;
	payload_end = cmds_atom->payload + cmds_atom->payload_sz;
	while (payload < payload_end) {
		int cmd_id = *payload;
		int n;
		dsf_cmd_t cmd;

		parser.cmd_file_off = (payload - cmds_atom->payload) +
		    cmds_atom->file_off + 8;

		ASSERT(cmd_id <= DSF_CMD_ID_MAX &&
		    cmd_parser_info[cmd_id].cb != NULL);

		if (cmd_id > DSF_CMD_ID_MAX ||
		    cmd_parser_info[cmd_id].cb == NULL) {
			if (reason != NULL) {
				snprintf(reason, DSF_REASON_SZ,
				    "invalid command ID %x at offset %lx",
				    cmd_id, (long)(payload -
				    cmds_atom->payload + cmds_atom->file_off));
			}
			return (B_FALSE);
		}
		cmd = cmd_parser_info[cmd_id].cmd;
		n = cmd_parser_info[cmd_id].cb(&parser, cmd, payload + 1,
		    payload_end - payload - 1, user_cbs[cmd]);
		if (n < 0) {
			if (reason != NULL) {
				snprintf(reason, DSF_REASON_SZ,
				    "malformed command %s at offset %lx: %s",
				    dsf_cmd2str(cmd_parser_info[cmd_id].cmd),
				    (long)parser.cmd_file_off, subreason);
			}
			return (B_FALSE);
		}
		payload += 1 + n;
		ASSERT3P(payload, <=, payload_end);
	}

	return (B_TRUE);
}

/**
 * Utility function to translate a DSF command type into a human-readable
 * description.
 */
const char *
dsf_cmd2str(dsf_cmd_t cmd)
{
	VERIFY3U(cmd, <, NUM_DSF_CMDS);

	switch (cmd) {
	case DSF_POOL_SEL:
		return ("POOL_SEL");
	case DSF_JUNCT_OFFSET_SEL:
		return ("JUNCT_OFFSET_SEL");
	case DSF_SET_DEFN8:
		return ("SET_DEFN8");
	case DSF_SET_DEFN16:
		return ("SET_DEFN16");
	case DSF_SET_DEFN32:
		return ("SET_DEFN32");
	case DSF_ROAD_SUBTYPE:
		return ("ROAD_SUBTYPE");
	case DSF_OBJ:
		return ("OBJ");
	case DSF_OBJ_RNG:
		return ("OBJ_RNG");
	case DSF_NET_CHAIN:
		return ("NET_CHAIN");
	case DSF_NET_CHAIN_RNG:
		return ("NET_CHAIN_RNG");
	case DSF_NET_CHAIN32:
		return ("NET_CHAIN32");
	case DSF_POLY:
		return ("POLY");
	case DSF_POLY_RNG:
		return ("POLY_RNG");
	case DSF_NEST_POLY:
		return ("NEST_POLY");
	case DSF_NEST_POLY_RNG:
		return ("NEST_POLY_RNG");
	case DSF_TERR_PATCH:
		return ("TERR_PATCH");
	case DSF_TERR_PATCH_FLAGS:
		return ("TERR_PATCH_FLAGS");
	case DSF_TERR_PATCH_FLAGS_N_LOD:
		return ("TERR_PATCH_FLAGS_N_LOD");
	case DSF_PATCH_TRIA:
		return ("PATCH_TRIA");
	case DSF_PATCH_TRIA_XPOOL:
		return ("PATCH_TRIA_XPOOL");
	case DSF_PATCH_TRIA_RNG:
		return ("PATCH_TRIA_RNG");
	case DSF_PATCH_TRIA_STRIP:
		return ("PATCH_TRIA_STRIP");
	case DSF_PATCH_TRIA_STRIP_XPOOL:
		return ("PATCH_TRIA_STRIP_XPOOL");
	case DSF_PATCH_TRIA_STRIP_RNG:
		return ("PATCH_TRIA_STRIP_RNG");
	case DSF_PATCH_TRIA_FAN:
		return ("PATCH_TRIA_FAN");
	case DSF_PATCH_TRIA_FAN_XPOOL:
		return ("PATCH_TRIA_FAN_XPOOL");
	case DSF_PATCH_TRIA_FAN_RNG:
		return ("PATCH_TRIA_FAN_RNG");
	case DSF_COMMENT8:
		return ("COMMENT8");
	case DSF_COMMENT16:
		return ("COMMENT16");
	case DSF_COMMENT32:
		return ("COMMENT32");
	default:
		VERIFY_FAIL();
	}
}

#define	CHECK_LEN(b)	\
	do { \
		if ((b) > (int)len) { \
			snprintf(parser->reason, DSF_REASON_SZ, \
			    "not enough data in command (wanted %d bytes, " \
			    "have %d bytes)", (b), (int)len); \
			return (-1); \
		} \
	} while (0)

static int
parse_idx_rng_common(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	if (parser->pool == NULL) {
		snprintf(parser->reason, DSF_REASON_SZ,
		    "no current POOL/SCAL selected");
		return (-1);
	}

	CHECK_LEN(4);
	if (cb != NULL) {
		dsf_idx_rng_arg_t arg = { read_u16(data), read_u16(data + 2) };
		if (arg.first >= parser->pool->planar_atom.data_count) {
			snprintf(parser->reason, DSF_REASON_SZ,
			    "range start index (%x) out of bounds for "
			    "corresponding POOL atom (max: %x)", arg.first,
			    parser->pool->planar_atom.data_count);
			return (-1);
		}
		if (arg.last_plus_one > parser->pool->planar_atom.data_count) {
			snprintf(parser->reason, DSF_REASON_SZ,
			    "range end index (%x) out of bounds for "
			    "corresponding POOL atom (max: %x)",
			    arg.last_plus_one,
			    parser->pool->planar_atom.data_count);
			return (-1);
		}
		if (arg.first >= arg.last_plus_one) {
			snprintf(parser->reason, DSF_REASON_SZ, "range start "
			    "index (%x) not lower than end index (%x)",
			    arg.first, arg.last_plus_one);
			return (-1);
		}
		cb(cmd, &arg, parser);
	}
	return (4);
}

static int
parse_idx_list_common(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	dsf_indices_arg_t arg;

	if (parser->pool == NULL) {
		snprintf(parser->reason, DSF_REASON_SZ,
		    "no current POOL/SCAL selected");
		return (-1);
	}

	CHECK_LEN(1);
	arg.num_coords = *data;
	CHECK_LEN(1 + arg.num_coords * 2);
	if (cb != NULL) {
		for (int i = 0; i < arg.num_coords; i++) {
			arg.indices[i] = read_u16(&data[1 + i * 2]);
			if (arg.indices[i] >=
			    parser->pool->planar_atom.data_count) {
				snprintf(parser->reason, DSF_REASON_SZ,
				    "index %d (%x) out of bounds of "
				    "corresponding POOL atom", i,
				    arg.indices[i]);
				return (-1);
			}
		}
		cb(cmd, &arg, parser);
	}
	return (1 + arg.num_coords * 2);
}

static int
parse_idx_list_xpool_common(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	dsf_indices_xpool_arg_t arg;

	if (parser->pool == NULL) {
		snprintf(parser->reason, DSF_REASON_SZ,
		    "no current POOL/SCAL selected");
		return (-1);
	}

	CHECK_LEN(1);
	arg.num_coords = *data;
	CHECK_LEN(1 + 2 * arg.num_coords * 2);
	if (cb != NULL) {
		for (int i = 0; i < arg.num_coords; i++) {
			int seq = read_u16(&data[1 + i * 4]);
			arg.indices[i].pool = dsf_lookup(parser->dsf,
			    DSF_ATOM_GEOD, 0, DSF_ATOM_POOL, seq, 0);
			arg.indices[i].scal = dsf_lookup(parser->dsf,
			    DSF_ATOM_GEOD, 0, DSF_ATOM_SCAL, seq, 0);
			if (arg.indices[i].pool == NULL ||
			    arg.indices[i].scal == NULL) {
				snprintf(parser->reason, DSF_REASON_SZ,
				    "index %d POOL or SCAL atom %d not found",
				    i, seq);
				return (-1);
			}
			arg.indices[i].idx = read_u16(&data[1 + i * 4 + 2]);
			if (arg.indices[i].idx >=
			    arg.indices[i].pool->planar_atom.data_count) {
				snprintf(parser->reason, DSF_REASON_SZ,
				    "index %d (%x) out of bounds of "
				    "corresponding POOL atom", i,
				    arg.indices[i].idx);
				return (-1);
			}
		}
		cb(cmd, &arg, parser);
	}
	return (1 + 2 * arg.num_coords * 2);
}

static int
pool_sel_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	int pool_idx;

	CHECK_LEN(2);
	pool_idx = read_u16(data);

	parser->pool = dsf_lookup(parser->dsf, DSF_ATOM_GEOD, 0,
	    DSF_ATOM_POOL, pool_idx, 0);
	parser->scal = dsf_lookup(parser->dsf, DSF_ATOM_GEOD, 0,
	    DSF_ATOM_SCAL, pool_idx, 0);
	if (parser->pool == NULL || parser->scal == NULL) {
		snprintf(parser->reason, DSF_REASON_SZ,
		    "POOL or SCAL atom with index %d not found", pool_idx);
		return (-1);
	}

	if (cb != NULL)
		cb(cmd, NULL, parser);

	return (2);
}

static int
junct_off_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	CHECK_LEN(4);
	parser->junct_off = read_u32(data);
	if (cb != NULL)
		cb(cmd, NULL, parser);
	return (4);
}

static int
set_defn_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	int cmdlen;
	if (cmd == DSF_SET_DEFN8) {
		CHECK_LEN(1);
		parser->defn_idx = *data;
		cmdlen = 1;
	} else if (cmd == DSF_SET_DEFN16) {
		CHECK_LEN(2);
		parser->defn_idx = read_u16(data);
		cmdlen = 2;
	} else {
		ASSERT3U(cmd, ==, DSF_SET_DEFN32);
		CHECK_LEN(4);
		parser->defn_idx = read_u32(data);
		cmdlen = 4;
	}
	if (cb != NULL)
		cb(cmd, NULL, parser);
	return (cmdlen);
}

static int
road_subt_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	CHECK_LEN(1);
	parser->road_subt = *data;
	if (cb != NULL)
		cb(cmd, NULL, parser);
	return (1);
}

static int
obj_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	if (cmd == DSF_OBJ) {
		uint16_t idx;
		CHECK_LEN(2);
		idx = read_u16(data);
		if (cb != NULL)
			cb(cmd, (void *)(uintptr_t)idx, parser);
		return (2);
	} else {
		ASSERT3U(cmd, ==, DSF_OBJ_RNG);
		return (parse_idx_rng_common(parser, cmd, data, len, cb));
	}
}

static int
net_chain_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	if (cmd == DSF_NET_CHAIN) {
		return (parse_idx_list_common(parser, cmd, data, len, cb));
	} else if (cmd == DSF_NET_CHAIN_RNG) {
		return (parse_idx_rng_common(parser, cmd, data, len, cb));
	} else {
		dsf_indices_arg_t arg;

		ASSERT3U(cmd, ==, DSF_NET_CHAIN32);
		CHECK_LEN(1);
		arg.num_coords = *data;
		CHECK_LEN(1 + arg.num_coords * 4);
		if (cb != NULL) {
			for (int i = 0; i < arg.num_coords; i++)
				arg.indices[i] = read_u32(&data[1 + i * 4]);
			cb(cmd, &arg, parser);
		}
		return (1 + arg.num_coords * 4);
	}
}

static int
parse_nest_poly_wind(dsf_cmd_parser_t *parser, const uint8_t *data,
    size_t len, dsf_cmd_cb_t cb, dsf_poly_arg_t *arg)
{
	CHECK_LEN(1);
	arg->num_coords = *data;
	data++;
	len--;

	CHECK_LEN(arg->num_coords * 2);
	if (cb == NULL)
		return (1 + arg->num_coords * 2);

	for (int i = 0; i < arg->num_coords; i++) {
		arg->indices[i] = read_u16(&data[i * 2]);
		if (arg->indices[i] >= parser->pool->planar_atom.data_count) {
			snprintf(parser->reason, DSF_REASON_SZ,
			    "index %d (%x) out of bounds of corresponding "
			    "POOL atom", i, arg->indices[i]);
			return (-1);
		}
	}
	cb(DSF_NEST_POLY, &arg, parser);

	return (1 + arg->num_coords * 2);
}

static int poly_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	if (cmd == DSF_POLY || cmd == DSF_NEST_POLY_RNG) {
		dsf_poly_arg_t arg;

		CHECK_LEN(3);
		arg.param = read_u16(data);
		arg.num_coords = data[2];
		if (cmd == DSF_NEST_POLY_RNG)
			/* The spec lies, there's an extra uint16 here */
			arg.num_coords++;
		CHECK_LEN(3 + arg.num_coords * 2);
		if (cb != NULL) {
			for (int i = 0; i < arg.num_coords; i++)
				arg.indices[i] = read_u16(3 + &data[i * 2]);
			cb(cmd, &arg, parser);
		}
		return (3 + arg.num_coords * 2);
	} else if (cmd == DSF_POLY_RNG) {
		CHECK_LEN(6);
		if (cb != NULL) {
			dsf_poly_rng_arg_t arg = {
			    read_u16(data),
			    read_u16(data + 2),
			    read_u16(data + 4)
			};
			cb(cmd, &arg, parser);
		}
		return (6);
	} else {
		dsf_poly_arg_t arg;
		int n_wind;
		size_t total = 0;

		ASSERT3U(cmd, ==, DSF_NEST_POLY);
		CHECK_LEN(3);
		arg.param = read_u16(data);
		n_wind = data[2];
		data += 3;
		len -= 3;
		total += 3;
		for (int i = 0; i < n_wind; i++) {
			int l = parse_nest_poly_wind(parser, data, len, cb,
			    &arg);
			if (l < 0)
				return (-1);
			total += l;
		}
		return (total);
	}
}

static int
terr_patch_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	if (cmd == DSF_TERR_PATCH) {
		if (cb != NULL)
			cb(cmd, NULL, parser);
		return (0);
	} else if (cmd == DSF_TERR_PATCH_FLAGS) {
		CHECK_LEN(1);
		if (cb != NULL)
			cb(cmd, (void *)(uintptr_t)*data, parser);
		return (1);
	} else {
		ASSERT3U(cmd, ==, DSF_TERR_PATCH_FLAGS_N_LOD);
		CHECK_LEN(9);
		if (cb != NULL) {
			dsf_flags_n_lod_arg_t arg = {
			    *data, *(float *)(data + 1), *(float *)(data + 5)
			};
			cb(cmd, &arg, parser);
		}
		return (9);
	}
}

static int
patch_tria_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	if (cmd == DSF_PATCH_TRIA) {
		return (parse_idx_list_common(parser, cmd, data, len, cb));
	} else if (cmd == DSF_PATCH_TRIA_XPOOL) {
		return (parse_idx_list_xpool_common(parser, cmd, data, len,
		    cb));
	} else if (cmd == DSF_PATCH_TRIA_RNG) {
		return (parse_idx_rng_common(parser, cmd, data, len, cb));
	} else if (cmd == DSF_PATCH_TRIA_STRIP) {
		return (parse_idx_list_common(parser, cmd, data, len, cb));
	} else if (cmd == DSF_PATCH_TRIA_STRIP_XPOOL) {
		return (parse_idx_list_xpool_common(parser, cmd, data, len,
		    cb));
	} else if (cmd == DSF_PATCH_TRIA_STRIP_RNG) {
		return (parse_idx_rng_common(parser, cmd, data, len, cb));
	} else if (cmd == DSF_PATCH_TRIA_FAN) {
		return (parse_idx_list_common(parser, cmd, data, len, cb));
	} else if (cmd == DSF_PATCH_TRIA_FAN_XPOOL) {
		return (parse_idx_list_xpool_common(parser, cmd, data, len,
		    cb));
	} else {
		ASSERT3U(cmd, ==, DSF_PATCH_TRIA_FAN_RNG);
		return (parse_idx_rng_common(parser, cmd, data, len, cb));
	}
}

static int
comment_cb(dsf_cmd_parser_t *parser, dsf_cmd_t cmd,
    const uint8_t *data, size_t len, dsf_cmd_cb_t cb)
{
	dsf_comment_arg_t arg;
	size_t hdrlen = 0;

	if (cmd == DSF_COMMENT8) {
		CHECK_LEN(1);
		hdrlen = 1;
		arg.len = *data;
	} else if (cmd == DSF_COMMENT16) {
		CHECK_LEN(2);
		hdrlen = 2;
		arg.len = read_u16(data);
	} else if (cmd == DSF_COMMENT32) {
		CHECK_LEN(4);
		hdrlen = 4;
		arg.len = read_u32(data);
	}
	if (cb != NULL) {
		arg.data = &data[1];
		cb(cmd, &arg, parser);
	}

	return (hdrlen + arg.len);
}

#undef	CHECK_LEN
