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
/**
 * \file
 * This file holds a general-purpose DSF parser. To start parsing a DSF
 * file on disk, use dsf_init() to obtain a handle to the file. When you
 * are done with the DSF file, use dsf_fini() to release the handle.
 */

#ifndef	_ACFUTILS_DSF_H_
#define	_ACFUTILS_DSF_H_

#include <stdlib.h>
#include <stdint.h>

#include "avl.h"
#include "list.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	DSF_ATOM(a, b, c, d)	(((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define	DSF_ATOM_HEAD	DSF_ATOM('H', 'E', 'A', 'D')
#define	DSF_ATOM_PROP	DSF_ATOM('P', 'R', 'O', 'P')

#define	DSF_ATOM_DEFN	DSF_ATOM('D', 'E', 'F', 'N')
#define	DSF_ATOM_TERT	DSF_ATOM('T', 'E', 'R', 'T')
#define	DSF_ATOM_OBJT	DSF_ATOM('O', 'B', 'J', 'T')
#define	DSF_ATOM_POLY	DSF_ATOM('P', 'O', 'L', 'Y')
#define	DSF_ATOM_NEWT	DSF_ATOM('N', 'E', 'T', 'W')

#define	DSF_ATOM_DEMN	DSF_ATOM('D', 'E', 'M', 'N')
#define	DSF_ATOM_GEOD	DSF_ATOM('G', 'E', 'O', 'D')
#define	DSF_ATOM_POOL	DSF_ATOM('P', 'O', 'O', 'L')
#define	DSF_ATOM_SCAL	DSF_ATOM('S', 'C', 'A', 'L')
#define	DSF_ATOM_PO32	DSF_ATOM('P', 'O', '3', '2')
#define	DSF_ATOM_SC32	DSF_ATOM('S', 'C', '3', '2')

#define	DSF_ATOM_DEMS	DSF_ATOM('D', 'E', 'M', 'S')
#define	DSF_ATOM_DEMI	DSF_ATOM('D', 'E', 'M', 'I')
#define	DSF_ATOM_DEMD	DSF_ATOM('D', 'E', 'M', 'D')

#define	DSF_ATOM_CMDS	DSF_ATOM('C', 'M', 'D', 'S')

enum { DSF_REASON_SZ = 256 };

#define	DSF_ATOM_ID_PRINTF(atom) \
	((atom)->id & 0xff000000) >> 24, \
	((atom)->id & 0xff0000) >> 16, \
	((atom)->id & 0xff00) >> 8, \
	((atom)->id & 0xff)

typedef enum {
	DSF_DATA_SINT16,
	DSF_DATA_UINT16,
	DSF_DATA_SINT32,
	DSF_DATA_UINT32,
	DSF_DATA_SINT64,
	DSF_DATA_UINT64,
	DSF_DATA_FP32,
	DSF_DATA_FP64
} dsf_data_type_t;

typedef enum {
	DSF_ENC_RAW = 0,
	DSF_ENC_DIFF = 1 << 0,
	DSF_ENC_RLE = 1 << 1
} dsf_data_plane_enc_t;

typedef struct {
	const char		*name;
	const char		*value;
	list_node_t		prop_node;
} dsf_prop_t;

typedef struct {
	list_t			props;
} dsf_prop_atom_t;

enum {
	DEMI_DATA_FP32 =	0,
	DEMI_DATA_SINT =	1,
	DEMI_DATA_UINT =	2,
	DEMI_DATA_MASK =	3,
	DEMI_POST_CTR =		1 << 2
};

typedef struct {
	unsigned		version;
	unsigned		bpp;
	uint16_t		flags;
	uint32_t		width;
	uint32_t		height;
	float			scale;
	float			offset;
} dsf_demi_atom_t;

typedef struct {
	dsf_data_type_t		data_type;
	uint32_t		data_count;
	unsigned		plane_count;
	union {
		void		**data;
		int16_t		**data_sint16;
		uint16_t	**data_uint16;
		int32_t		**data_sint32;
		uint32_t	**data_uint32;
		int64_t		**data_sint64;
		uint64_t	**data_uint64;
		float		**data_fp32;
		double		**data_fp64;
	};
} dsf_planar_atom_t;

typedef struct {
	uint32_t		id;
	uint32_t		payload_sz;
	const uint8_t		*payload;
	list_t			subatoms;
	unsigned long long	file_off;

	bool_t			subtype_inited;
	union {
		dsf_prop_atom_t		prop_atom;
		dsf_planar_atom_t	planar_atom;
		dsf_demi_atom_t		demi_atom;
	};

	list_node_t		atom_list;
} dsf_atom_t;

typedef struct {
	int			version;
	list_t			atoms;
	uint8_t			*data;
	uint64_t		size;
	uint8_t			md5sum[16];
} dsf_t;

typedef struct {
	uint32_t		atom_id;
	unsigned		idx;
} dsf_lookup_t;

typedef enum {
	DSF_POOL_SEL,
	DSF_JUNCT_OFFSET_SEL,
	DSF_SET_DEFN8,
	DSF_SET_DEFN16,
	DSF_SET_DEFN32,
	DSF_ROAD_SUBTYPE,
	DSF_OBJ,
	DSF_OBJ_RNG,
	DSF_NET_CHAIN,
	DSF_NET_CHAIN_RNG,
	DSF_NET_CHAIN32,
	DSF_POLY,
	DSF_POLY_RNG,
	DSF_NEST_POLY,
	DSF_NEST_POLY_RNG,
	DSF_TERR_PATCH,
	DSF_TERR_PATCH_FLAGS,
	DSF_TERR_PATCH_FLAGS_N_LOD,
	DSF_PATCH_TRIA,
	DSF_PATCH_TRIA_XPOOL,
	DSF_PATCH_TRIA_RNG,
	DSF_PATCH_TRIA_STRIP,
	DSF_PATCH_TRIA_STRIP_XPOOL,
	DSF_PATCH_TRIA_STRIP_RNG,
	DSF_PATCH_TRIA_FAN,
	DSF_PATCH_TRIA_FAN_XPOOL,
	DSF_PATCH_TRIA_FAN_RNG,
	DSF_COMMENT8,
	DSF_COMMENT16,
	DSF_COMMENT32,
	NUM_DSF_CMDS
} dsf_cmd_t;

typedef struct {
	const dsf_t		*dsf;

	size_t			cmd_file_off;

	uint64_t		junct_off;
	uint64_t		defn_idx;
	uint64_t		road_subt;

	const dsf_atom_t	*pool;
	const dsf_atom_t	*scal;

	void			*userinfo;

	char			*reason;
} dsf_cmd_parser_t;

typedef struct {
	unsigned	first;
	unsigned	last_plus_one;
} dsf_idx_rng_arg_t;

typedef struct {
	int		num_coords;
	unsigned	indices[255];
} dsf_indices_arg_t;

typedef struct {
	int				num_coords;
	struct {
		const dsf_atom_t	*pool;
		const dsf_atom_t	*scal;
		unsigned		idx;
	} indices[255];
} dsf_indices_xpool_arg_t;

typedef struct {
	unsigned	param;
	int		num_coords;
	unsigned	indices[255];
} dsf_poly_arg_t;

typedef struct {
	unsigned	param;
	unsigned	first;
	unsigned	last_plus_one;
} dsf_poly_rng_arg_t;

typedef struct {
	uint8_t		flags;
	float		near_lod;
	float		far_lod;
} dsf_flags_n_lod_arg_t;

typedef struct {
	size_t		len;
	const uint8_t	*data;
} dsf_comment_arg_t;

typedef void (*dsf_cmd_cb_t)(dsf_cmd_t cmd, const void *cmd_args,
    const dsf_cmd_parser_t *parser);

API_EXPORT dsf_t *dsf_init(const char *filename);
API_EXPORT dsf_t *dsf_parse(uint8_t *buf, size_t bufsz,
    char reason[DSF_REASON_SZ]);
API_EXPORT void dsf_fini(dsf_t *dsf);
API_EXPORT char *dsf_dump(const dsf_t *dsf);

API_EXPORT const dsf_atom_t *dsf_lookup(const dsf_t *dsf, ...);
API_EXPORT const dsf_atom_t *dsf_lookup_v(const dsf_t *dsf,
    const dsf_lookup_t *lookup);
API_EXPORT const dsf_atom_t *dsf_iter(const dsf_atom_t *parent,
    uint32_t atom_id, const dsf_atom_t *prev);
API_EXPORT bool_t dsf_parse_cmds(const dsf_t *dsf,
    dsf_cmd_cb_t user_cbs[NUM_DSF_CMDS],
    void *userinfo, char reason[DSF_REASON_SZ]);

API_EXPORT const char *dsf_cmd2str(dsf_cmd_t cmd);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_DSF_H_ */
