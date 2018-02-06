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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <shapefil.h>
#include <cairo.h>

#include <acfutils/assert.h>
#include <acfutils/math.h>
#include <acfutils/log.h>
#include <acfutils/png.h>

#define	WIDTH	981
#define	HEIGHT	1111

static void
logfunc(const char *str)
{
	puts(str);
}

int
main(int argc, char *argv[])
{
	int opt;
	SHPHandle shp;
	int n_ent, shp_type;
	cairo_t *cr;
	cairo_surface_t *surf;
	int lat, lon;
	char lat_buf[8], lon_buf[8];
	char *last_part;
	char *out_filename = "shp.png";
	bool_t verbose = B_FALSE;

	log_init(logfunc, "dsfdump");

	while ((opt = getopt(argc, argv, "hvo:")) != -1) {
		switch (opt) {
		case 'h':
			printf("Usage: %s [-hv] [-o outfile.png] <shp-file>\n",
			    argv[0]);
			exit(EXIT_SUCCESS);
		case 'o':
			out_filename = optarg;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		default:
			fprintf(stderr, "Usage: %s [-hv] [-o outfile.png] "
			    "<shp-file>\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename argument. "
		    "Try -h for help.\n");
		exit(EXIT_FAILURE);
	}

	shp = SHPOpen(argv[optind], "rb");
	if (shp == NULL) {
		fprintf(stderr, "Error opening shp file: %s\n",
		    strerror(errno));
		return (1);
	}

	last_part = strrchr(argv[optind], '/');
	if (last_part == NULL)
		last_part = argv[optind];
	else
		last_part++;
	if (strlen(last_part) < 7) {
		fprintf(stderr, "Bad filename: %s\n", argv[optind]);
		return (1);
	}

	memset(lat_buf, 0, sizeof (lat_buf));
	memset(lon_buf, 0, sizeof (lon_buf));
	memcpy(lat_buf, last_part, 3);
	memcpy(lon_buf, &last_part[3], 4);
	lat = atoi(lat_buf);
	lon = atoi(lon_buf);
	if (verbose)
		printf("lat: %d lon: %d\n", lat, lon);

	surf = cairo_image_surface_create(CAIRO_FORMAT_A8, WIDTH, HEIGHT);
	cr = cairo_create(surf);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	cairo_scale(cr, WIDTH, HEIGHT);

	cairo_set_source_rgb(cr, 1, 1, 1);

	SHPGetInfo(shp, &n_ent, &shp_type, NULL, NULL);
	if (verbose)
		printf("n_ent: %d    shp_type: %d\n", n_ent, shp_type);
	for (int i = 0; i < n_ent; i++) {
		SHPObject *obj = SHPReadObject(shp, i);

		if (obj == NULL) {
			fprintf(stderr, "Error reading shape %d\n", i);
			continue;
		}
		if (verbose) {
			printf("  nVertices: %d  nParts: %d\n",
			    obj->nVertices, obj->nParts);
		}
		cairo_new_path(cr);
		for (int j = 0; j < obj->nParts; j++) {
			int start_k, end_k;

			start_k = obj->panPartStart[j];
			if (j + 1 < obj->nParts)
				end_k = obj->panPartStart[j + 1];
			else
				end_k = obj->nVertices;
			if (verbose) {
				printf("    part: %d   (%d - %d)\n", j,
				    start_k, end_k);
			}

			cairo_new_sub_path(cr);
			/*
			 * Note that since `lat' is always the floor of the
			 * latitude, but cairo & PNG address the image from
			 * the top left, to get the PNG to look right, we
			 * need to flip the Y coordinates to make the image
			 * look right when drawn. This is not done in OpenGWPS,
			 * since it instead wants to address the image from the
			 * bottom left corner.
			 */
			cairo_move_to(cr, obj->padfX[start_k] - lon,
			    (lat + 1) - obj->padfY[start_k]);
			for (int k = start_k + 1; k < end_k; k++) {
				if (verbose) {
					printf("      %d: %f x %f\n", k,
					    obj->padfX[k], obj->padfY[k]);
				}
				cairo_line_to(cr, obj->padfX[k] - lon,
				    (lat + 1) - obj->padfY[k]);
			}
		}
		cairo_fill(cr);
		SHPDestroyObject(obj);
	}
	SHPClose(shp);

	cairo_surface_write_to_png(surf, out_filename);

	cairo_destroy(cr);
	cairo_surface_destroy(surf);

	return (0);
}
