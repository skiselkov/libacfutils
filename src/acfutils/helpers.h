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

#ifndef	_ACF_UTILS_HELPERS_H_
#define	_ACF_UTILS_HELPERS_H_

#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <math.h>
#if	APL || LIN
#include <dirent.h>	/* to bring in DIR, opendir, readdir & friends */
#include <unistd.h>
#endif

#if	IBM
#include <windows.h>
#endif	/* IBM */

#include "math_core.h"
#include "sysmacros.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* generic parser validator helpers */

#define	IS_VALID_GEO_POS3(pos) \
	(is_valid_lat((pos).lat) && is_valid_lat((pos).lat) && \
	is_valid_elev((pos).elev))
#define	IS_VALID_GEO_POS2(pos) \
	(is_valid_lat((pos).lat) && is_valid_lat((pos).lat))

static inline bool_t
is_valid_lat(double lat)
{
	enum { ARPT_LAT_LIMIT = 80 };
	return (!isnan(lat) && fabs(lat) < ARPT_LAT_LIMIT);
}

static inline bool_t
is_valid_lon(double lon)
{
	return (!isnan(lon) && fabs(lon) <= 180.0);
}

static inline bool_t
is_valid_elev(double elev)
{
	return (elev >= MIN_ELEV && elev <= MAX_ELEV);
}

static inline bool_t
is_valid_alt(double alt)
{
	return (alt >= MIN_ALT && alt <= MAX_ALT);
}

static inline bool_t
is_valid_spd(double spd)
{
	return (spd >= 0.0 && spd <= MAX_SPD);
}

static inline bool_t
is_valid_hdg(double hdg)
{
	return (hdg >= 0.0 && hdg <= 360.0);
}

#define	rel_hdg(h1, h2)	rel_hdg_impl(h1, h2, __FILE__, __LINE__)
#define	rel_hdg_impl	ACFSYM(rel_hdg_impl)
API_EXPORT double rel_hdg_impl(double hdg1, double hdg2, const char *file,
    int line);

static inline double
normalize_hdg(double hdg)
{
	while (hdg < 0.0)
		hdg += 360.0;
	while (hdg >= 360.0)
		hdg -= 360.0;
	/* Necessary to avoid FP rounding errors */
	if (hdg < 0.0 || hdg > 360.0)
		hdg = clamp(hdg, 0, 360);
	return (hdg);
}

static inline bool_t
is_valid_arc_radius(double radius)
{
	return (radius >= MIN_ARC_RADIUS && radius <= MAX_ARC_RADIUS);
}

static inline bool_t
is_valid_bool(bool_t b)
{
	return (b == B_FALSE || b == B_TRUE);
}

#define	is_valid_icao_code		ACFSYM(is_valid_icao_code)
API_EXPORT bool_t is_valid_icao_code(const char *icao);
#define	extract_icao_country_code	ACFSYM(extract_icao_country_code)
API_EXPORT const char *extract_icao_country_code(const char *icao);

#define	is_valid_xpdr_code	ACFSYM(is_valid_xpdr_code)
API_EXPORT bool_t is_valid_xpdr_code(int code);
#define	is_valid_vor_freq	ACFSYM(is_valid_vor_freq)
API_EXPORT bool_t is_valid_vor_freq(double freq_mhz);
#define	is_valid_loc_freq	ACFSYM(is_valid_loc_freq)
API_EXPORT bool_t is_valid_loc_freq(double freq_mhz);
#define	is_valid_ndb_freq	ACFSYM(is_valid_ndb_freq)
API_EXPORT bool_t is_valid_ndb_freq(double freq_khz);
#define	is_valid_tacan_freq	ACFSYM(is_valid_tacan_freq)
API_EXPORT bool_t is_valid_tacan_freq(double freq_mhz);
#define	is_valid_rwy_ID	ACFSYM(is_valid_rwy_ID)
API_EXPORT bool_t is_valid_rwy_ID(const char *rwy_ID);
#define	copy_rwy_ID	ACFSYM(copy_rwy_ID)
API_EXPORT void copy_rwy_ID(const char *src, char dst[4]);

/* AIRAC date functions */
#define	airac_cycle2eff_date	ACFSYM(airac_cycle2eff_date)
API_EXPORT const char *airac_cycle2eff_date(int cycle);
#define	airac_cycle2exp_date	ACFSYM(airac_cycle2exp_date)
API_EXPORT const char *airac_cycle2exp_date(int cycle);

/* CSV file & string processing helpers */
#define	parser_get_next_line		ACFSYM(parser_get_next_line)
API_EXPORT ssize_t parser_get_next_line(FILE *fp, char **linep,
    size_t *linecap, size_t *linenum);
#define	parser_get_next_quoted_str	ACFSYM(parser_get_next_quoted_str)
API_EXPORT char *parser_get_next_quoted_str(FILE *fp);
#define	explode_line			ACFSYM(explode_line)
API_EXPORT ssize_t explode_line(char *line, char delim, char **comps,
    size_t capacity);
#define	strip_space			ACFSYM(strip_space)
API_EXPORT void strip_space(char *line);
#define	append_format			ACFSYM(append_format)
API_EXPORT void append_format(char **str, size_t *sz, const char *format, ...)
    PRINTF_ATTR(3);

/* string processing helpers */
#define	strsplit			ACFSYM(strsplit)
API_EXPORT char **strsplit(const char *input, char *sep, bool_t skip_empty,
    size_t *num);
#define	free_strlist			ACFSYM(free_strlist)
API_EXPORT void free_strlist(char **comps, size_t len);
#define	unescape_percent		ACFSYM(unescape_percent)
API_EXPORT void unescape_percent(char *str);

#define	mkpathname			ACFSYM(mkpathname)
API_EXPORT char *mkpathname(const char *comp, ...);
#define	mkpathname_v			ACFSYM(mkpathname_v)
API_EXPORT char *mkpathname_v(const char *comp, va_list ap);
#define	fix_pathsep			ACFSYM(fix_pathsep)
API_EXPORT void fix_pathsep(char *str);

#define	file2str			ACFSYM(file2str)
API_EXPORT char *file2str(const char *comp, ...);
#define	file2str_ext			ACFSYM(file2str_ext)
API_EXPORT char *file2str_ext(long *len_p, const char *comp, ...);
#define	file2str_name			ACFSYM(file2str_name)
API_EXPORT char *file2str_name(long *len_p, const char *filename);
#define	file2buf			ACFSYM(file2buf)
API_EXPORT void *file2buf(const char *filename, size_t *bufsz);
#define	filesz				ACFSYM(filesz)
API_EXPORT ssize_t filesz(const char *filename);

#if	IBM || LIN
#define	strlcpy				ACFSYM(strlcpy)
API_EXPORT void strlcpy(char *restrict dest, const char *restrict src,
    size_t cap);
#endif	/* IBM || LIN */
#if	IBM
#define	getline				ACFSYM(getline)
API_EXPORT ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif	/* IBM */

#define	strtolower			ACFSYM(strtolower)
API_EXPORT void strtolower(char *str);

/*
 * return x rounded up to the nearest power-of-2.
 */
#define	P2ROUNDUP(x)	(-(-(x) & -(1 << highbit64(x))))
/* Round `x' to the nearest multiple of `y' */
static inline double
roundmul(double x, double y)
{
	return (round(x / y) * y);
}
/* Round `x' DOWN to the nearest multiple of `y' */
static inline double
floormul(double x, double y)
{
	return (floor(x / y) * y);
}

/* file/directory manipulation */
#define	file_eixsts			ACFSYM(file_exists)
API_EXPORT bool_t file_exists(const char *path, bool_t *isdir);
#define	create_directory		ACFSYM(create_directory)
API_EXPORT bool_t create_directory(const char *dirname);
#define	create_directory_recursive	ACFSYM(create_directory_recursive)
API_EXPORT bool_t create_directory_recursive(const char *dirname);
#define	remove_directory		ACFSYM(remove_directory)
API_EXPORT bool_t remove_directory(const char *dirname);
#define	remove_file			ACFSYM(remove_file)
API_EXPORT bool_t remove_file(const char *filename, bool_t notfound_ok);

#if	IBM

/* A minimally compatible POSIX-style directory reading implementation */
struct dirent {
	char		d_name[256];
};
typedef struct {
	HANDLE		handle;
	WIN32_FIND_DATA	find_data;
	bool_t		first;
	struct dirent	de;
} DIR;

#define	opendir		ACFSYM(opendir)
API_EXPORT DIR *opendir(const char *path);
#define	readdir		ACFSYM(readdir)
API_EXPORT struct dirent *readdir(DIR *dirp);
#define	closedir	ACFSYM(closedir)
API_EXPORT void closedir(DIR *dirp);

#if	!defined(_MSC_VER)
/* A minimally compatible POSIX-style file stat reading implementation */
#define	stat		ACFSYM(stat)
struct stat {
	uint64_t	st_size;
	uint64_t	st_atime;
	uint64_t	st_mtime;
};
API_EXPORT int stat(const char *pathname, struct stat *buf);
#endif	/* !defined(_MSC_VER) */

#define	win_perror	ACFSYM(win_perror)
API_EXPORT void win_perror(DWORD err, const char *fmt, ...) PRINTF_ATTR(2);

#define	sleep(x)	SleepEx((x) * 1000, FALSE)

#endif	/* IBM */

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_HELPERS_H_ */
