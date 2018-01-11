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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <string.h>

#include <unistd.h>

#include <acfutils/assert.h>
#include <acfutils/dsf.h>
#include <acfutils/log.h>
#include <acfutils/png.h>

static dsf_cmd_cb_t cmd_cbs[NUM_DSF_CMDS];

static void
logfunc(const char *str)
{
	puts(str);
}

static void
cmd_cb(dsf_cmd_t cmd, const void *arg, const dsf_cmd_parser_t *parser)
{
	UNUSED(arg);

	if (!(uintptr_t)parser->userinfo) {
		printf("cmd: %s (%x)\n", dsf_cmd2str(cmd),
		    (int)parser->cmd_file_off);
	}
}

static double
demd_read(const dsf_atom_t *demi, const dsf_atom_t *demd,
    unsigned row, unsigned col)
{
	double v;

#define	DEMD_READ(data_type, val) \
	do { \
		(val) = ((data_type *)demd->payload)[row * \
		    demi->demi_atom.width + col] * \
		    demi->demi_atom.scale + demi->demi_atom.offset; \
	} while (0)

	switch (demi->demi_atom.flags & DEMI_DATA_MASK) {
	case DEMI_DATA_FP32:
		DEMD_READ(float, v);
		break;
	case DEMI_DATA_SINT:
		switch (demi->demi_atom.bpp) {
		case 1:
			DEMD_READ(int8_t, v);
			break;
		case 2:
			DEMD_READ(int16_t, v);
			break;
		case 4:
			DEMD_READ(int32_t, v);
			break;
		}
		break;
	case DEMI_DATA_UINT:
		switch (demi->demi_atom.bpp) {
		case 1:
			DEMD_READ(uint8_t, v);
			break;
		case 2:
			DEMD_READ(uint16_t, v);
			break;
		case 4:
			DEMD_READ(uint32_t, v);
			break;
		}
		break;
	default:
		VERIFY(0);
	}

#undef	DEMD_READ

	return (v);
}

static void
dump_dem(const dsf_atom_t *demi, const dsf_atom_t *demd, int seq)
{
//	uint16_t *buf;
	uint8_t *buf;
	char filename[32];
	double max_val = -10000, min_val = 10000;

	ASSERT3U(demd->payload_sz, ==, demi->demi_atom.width *
	    demi->demi_atom.height * demi->demi_atom.bpp);

	/* we write 16-bit grey-scale */
	buf = malloc(demi->demi_atom.width * demi->demi_atom.height);
//	buf = malloc(2 * demi->demi_atom.width * demi->demi_atom.height);

	for (unsigned row = 0; row < demi->demi_atom.height; row++) {
		for (unsigned col = 0; col < demi->demi_atom.width; col++) {
			double v = demd_read(demi, demd, row, col);
			max_val = MAX(v, max_val);
			min_val = MIN(v, min_val);
		}
	}

	for (unsigned row = 0; row < demi->demi_atom.height; row++) {
		for (unsigned col = 0; col < demi->demi_atom.width; col++) {
			double v = demd_read(demi, demd, row, col);
			unsigned pixel_pos =
			    ((demi->demi_atom.height - row - 1) *
			    demi->demi_atom.width + col);

			buf[pixel_pos] =
			    ((v - min_val) / (max_val - min_val)) * 255;
//			buf[pixel_pos] = v + 32768;
//#if	__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
//			buf[pixel_pos] = BSWAP16(buf[pixel_pos]);
//#endif
		}
	}

	snprintf(filename, sizeof (filename), "DEM_%d.png", seq);
//	png_write_to_file_grey16(filename, demi->demi_atom.width,
//	    demi->demi_atom.height, buf);
	png_write_to_file_grey8(filename, demi->demi_atom.width,
	    demi->demi_atom.height, buf);

	printf("min: %f   max: %f\n", min_val, max_val);

	free(buf);
}

int
main(int argc, char *argv[])
{
	dsf_t *dsf;
	char *dump;
	char reason[DSF_REASON_SZ];
	int opt;
	bool_t quiet = B_FALSE;
	bool_t dump_cmds = B_FALSE;
	bool_t do_dump_dem = B_FALSE;

	memset(cmd_cbs, 0, sizeof (cmd_cbs));
	for (int i = 0; i < NUM_DSF_CMDS; i++)
		cmd_cbs[i] = cmd_cb;

	log_init(logfunc, "dsfdump");

	while ((opt = getopt(argc, argv, "hqcd")) != -1) {
		switch (opt) {
		case 'q':
			quiet = B_TRUE;
			break;
		case 'c':
			dump_cmds = B_TRUE;
			break;
		case 'd':
			do_dump_dem = B_TRUE;
			break;
		case 'h':
			printf("Usage: %s [-q] <dsf-file>\n", argv[0]);
			exit(EXIT_SUCCESS);
		default:
			fprintf(stderr, "Usage: %s [-q] <dsf-file>\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename argument. "
		    "Try -h for help.\n");
		exit(EXIT_FAILURE);
	}

	dsf = dsf_init(argv[optind]);
	if (dsf == NULL)
		return (1);

	dump = dsf_dump(dsf);
	if (!quiet)
		puts(dump);
	free(dump);

	if (dump_cmds &&
	    !dsf_parse_cmds(dsf, cmd_cbs, (void *)(uintptr_t)quiet, reason)) {
		fprintf(stderr, "Error parsing DSF commands: %s\n", reason);
	}
	if (do_dump_dem) {
		const dsf_atom_t *demi;
		const dsf_atom_t *demd;

		for (int i = 0;
		    (demi = dsf_lookup(dsf, DSF_ATOM_DEMS, 0,
		    DSF_ATOM_DEMI, i, 0)) != NULL &&
		    (demd = dsf_lookup(dsf, DSF_ATOM_DEMS, 0,
		    DSF_ATOM_DEMD, i, 0)) != NULL && i < 2; i++) {
			dump_dem(demi, demd, i);
		}
	}

	dsf_fini(dsf);

	return (0);
}
