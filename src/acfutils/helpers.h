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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_HELPERS_H_
#define	_ACF_UTILS_HELPERS_H_

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>
#if	APL || LIN
#include <sys/stat.h>
#include <dirent.h>	/* to bring in DIR, opendir, readdir & friends */
#include <unistd.h>
#endif

#if	IBM
#include <windows.h>
#endif	/* IBM */

#define	_LACF_GETLINE_INCLUDED
#include "lacf_getline_impl.h"
#define	_LACF_PARSER_FUNCS_INCLUDED
#include "parser_funcs.h"
#include "math_core.h"
#include "sysmacros.h"
#include "safe_alloc.h"
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
	enum { ARPT_LAT_LIMIT = 83 };
	return (!isnan(lat) && fabs(lat) < ARPT_LAT_LIMIT);
}

static inline bool_t
is_valid_lat_polar(double lat)
{
	return (!isnan(lat) && fabs(lat) <= 90);
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

static inline double
normalize_lon(double lon)
{
	while (lon > 180.0)
		lon -= 360.0;
	while (lon < -180.0)
		lon += 360.0;
	return (clamp(lon, -180, 180));
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
#define	is_valid_iata_code		ACFSYM(is_valid_iata_code)
API_EXPORT bool_t is_valid_iata_code(const char *iata);
#define	extract_icao_country_code	ACFSYM(extract_icao_country_code)
API_EXPORT const char *extract_icao_country_code(const char *icao);

#define	is_valid_xpdr_code	ACFSYM(is_valid_xpdr_code)
API_EXPORT bool_t is_valid_xpdr_code(int code);
#define	is_valid_vor_freq	ACFSYM(is_valid_vor_freq)
API_EXPORT bool_t is_valid_vor_freq(double freq_mhz);
static inline bool_t is_valid_vor_freq_hz(uint32_t freq_hz)
{
	return (is_valid_vor_freq(freq_hz / 1000000.0));
}
#define	is_valid_loc_freq	ACFSYM(is_valid_loc_freq)
API_EXPORT bool_t is_valid_loc_freq(double freq_mhz);
static inline bool_t is_valid_loc_freq_hz(uint32_t freq_hz)
{
	return (is_valid_loc_freq(freq_hz / 1000000.0));
}
#define	is_valid_ndb_freq	ACFSYM(is_valid_ndb_freq)
API_EXPORT bool_t is_valid_ndb_freq(double freq_khz);
static inline bool_t is_valid_ndb_freq_hz(uint32_t freq_hz)
{
	return (is_valid_ndb_freq(freq_hz / 1000.0));
}
#define	is_valid_tacan_freq	ACFSYM(is_valid_tacan_freq)
API_EXPORT bool_t is_valid_tacan_freq(double freq_mhz);
#define	is_valid_rwy_ID	ACFSYM(is_valid_rwy_ID)
API_EXPORT bool_t is_valid_rwy_ID(const char *rwy_ID);
#define	copy_rwy_ID	ACFSYM(copy_rwy_ID)
API_EXPORT void copy_rwy_ID(const char *src, char dst[4]);

/* AIRAC date functions */
#define	airac_cycle2eff_date	ACFSYM(airac_cycle2eff_date)
API_EXPORT const char *airac_cycle2eff_date(int cycle);
#define	airac_cycle2eff_date2	ACFSYM(airac_cycle2eff_date2)
API_EXPORT time_t airac_cycle2eff_date2(int cycle);
#define	airac_cycle2exp_date	ACFSYM(airac_cycle2exp_date)
API_EXPORT bool_t airac_cycle2exp_date(int cycle, char buf[16],
    time_t *cycle_end_p);
#define	airac_time2cycle	ACFSYM(airac_time2cycle)
API_EXPORT int airac_time2cycle(time_t t);

/* CSV file & string processing helpers */
/*
 * Grabs the next non-empty, non-comment line from a file, having stripped
 * away all leading and trailing whitespace. Any tab characters are also
 * replaced with spaces.
 *
 * @param fp File from which to retrieve the line.
 * @param linep Line buffer which will hold the new line. If the buffer pointer
 *	is set to NULL, it will be allocated. If it is not long enough, it
 *	will be expanded.
 * @param linecap The capacity of *linep. If set to zero a new buffer is
 *	allocated.
 * @param linenum The current line number. Will be advanced by 1 for each
 *	new line read.
 *
 * @return The number of characters in the line (after stripping whitespace)
 *	without the terminating NUL.
 */
UNUSED_ATTR static ssize_t
parser_get_next_line(FILE *fp, char **linep, size_t *linecap, unsigned *linenum)
{
#ifdef	ACFUTILS_BUILD
	return (parser_get_next_line_impl(fp, linep, linecap, linenum,
	    B_FALSE));
#else	/* !defined(ACFUTILS_BUILD) */
	return (parser_get_next_line_impl(fp, linep, linecap, linenum));
#endif	/* !defined(ACFUTILS_BUILD) */
}

#ifdef	ACFUTILS_BUILD
/*
 * Same as parser_get_next_line, but for gzip-compressed files.
 */
UNUSED_ATTR static ssize_t
parser_get_next_gzline(void *gz_fp, char **linep, size_t *linecap,
    unsigned *linenum)
{
	return (parser_get_next_line_impl(gz_fp, linep, linecap, linenum,
	    B_TRUE));
}
#endif	/* defined(ACFUTILS_BUILD) */

UNUSED_ATTR static char *
parser_get_next_quoted_str(FILE *fp)
{
	return (parser_get_next_quoted_str2(fp, NULL));
}

#define	explode_line			ACFSYM(explode_line)
API_EXPORT ssize_t explode_line(char *line, char delim, char **comps,
    size_t capacity);
#define	append_format			ACFSYM(append_format)
API_EXPORT void append_format(char **str, size_t *sz,
    PRINTF_FORMAT(const char *format), ...) PRINTF_ATTR(3);
static inline void
normalize_whitespace(char *str)
{
	for (int i = 0, n = strlen(str); i < n; i++) {
		if (isspace(str[i]))
			str[i] = ' ';
	}
}

/* string processing helpers */
#define	strsplit			ACFSYM(strsplit)
API_EXPORT char **strsplit(const char *input, const char *sep,
    bool_t skip_empty, size_t *num);
#define	DESTROY_STRLIST(comps, len) \
	do { \
		free_strlist((comps), (len)); \
		(comps) = NULL; \
		(len) = 0; \
	} while (0)
#define	free_strlist			ACFSYM(free_strlist)
API_EXPORT void free_strlist(char **comps, size_t len);
#define	unescape_percent		ACFSYM(unescape_percent)
API_EXPORT void unescape_percent(char *str);

#define	mkpathname			ACFSYM(mkpathname)
API_EXPORT char *mkpathname(const char *comp, ...) SENTINEL_ATTR;
#define	mkpathname_v			ACFSYM(mkpathname_v)
API_EXPORT char *mkpathname_v(const char *comp, va_list ap);
#define	fix_pathsep			ACFSYM(fix_pathsep)
API_EXPORT void fix_pathsep(char *str);

#define	path_last_comp_subst		ACFSYM(path_last_comp_subst)
API_EXPORT char *path_last_comp_subst(const char *path, const char *replace);
#define	path_last_comp			ACFSYM(path_last_comp)
API_EXPORT char *path_last_comp(const char *path);
#define	path_ext_subst			ACFSYM(path_ext_subst)
API_EXPORT char *path_ext_subst(const char *path, const char *ext);

#define	file2str			ACFSYM(file2str)
API_EXPORT char *file2str(const char *comp, ...) SENTINEL_ATTR;
#define	file2str_ext			ACFSYM(file2str_ext)
API_EXPORT char *file2str_ext(long *len_p, const char *comp, ...) SENTINEL_ATTR;
#define	file2str_name			ACFSYM(file2str_name)
API_EXPORT char *file2str_name(long *len_p, const char *filename);
#define	file2buf			ACFSYM(file2buf)
API_EXPORT void *file2buf(const char *filename, size_t *bufsz);
#define	filesz				ACFSYM(filesz)
API_EXPORT ssize_t filesz(const char *filename);

/*
 * strlcpy is a BSD function not available on Windows, so we roll a simple
 * version of it ourselves. Also, on BSD it's SLOOOOW. Inline for max perf.
 */
#if	IBM || LIN
#define	strlcpy				lacf_strlcpy
#endif
static inline void
lacf_strlcpy(char *restrict dest, const char *restrict src, size_t cap)
{
	size_t l;

	ASSERT(cap != 0);
	/*
	 * We MUSTN'T use strlen here, because src may be SIGNIFICANTLY
	 * larger than dest and we don't want to measure the ENTIRE body
	 * of src. We only care for length UP TO the destination capacity.
	 */
	for (l = 0; l + 1 < cap && src[l] != '\0'; l++)
		;
	/*
	 * Due to a bug in GCC, we can't use strncpy, as it sometimes throws
	 * "call to __builtin___strncpy_chk will always overflow destination
	 * buffer", even when it's absolutely NOT the case.
	 */
	memcpy(dest, src, MIN(cap - 1, l + 1));
	/* Insure the string is ALWAYS terminated */
	dest[cap - 1] = '\0';
}

static inline const char *
lacf_basename(const char *str)
{
	const char *sep = strrchr(str, DIRSEP);

#if	IBM
	const char *sep2 = strrchr(str, '/');
	if (sep2 > sep)
		sep = sep2;
#endif	/* IBM */

	if (sep == NULL)
		return (str);
	return (&sep[1]);
}

/*
 * C getline is a POSIX function, so on Windows, we need to roll our own.
 */
UNUSED_ATTR static ssize_t
lacf_getline(char **line_p, size_t *cap_p, FILE *fp)
{
#ifdef	ACFUTILS_BUILD
	return (lacf_getline_impl(line_p, cap_p, fp, B_FALSE));
#else
	return (lacf_getline_impl(line_p, cap_p, fp));
#endif
}

#if	IBM && (defined(_GNU_SOURCE) || defined(_POSIX_C_SOURCE))
#define	getline				lacf_getline
#endif	/* IBM && (defined(_GNU_SOURCE) || defined(_POSIX_C_SOURCE)) */

#define	strtolower			ACFSYM(strtolower)
API_EXPORT void strtolower(char *str);
#define	strtoupper			ACFSYM(strtoupper)
API_EXPORT void strtoupper(char *str);

#define	sprintf_alloc			ACFSYM(sprintf_alloc)
static inline char *sprintf_alloc(PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(1);

#define	vsprintf_alloc			ACFSYM(vsprintf_alloc)
static inline char *vsprintf_alloc(const char *fmt, va_list ap);

static inline char *
sprintf_alloc(const char *fmt, ...)
{
	va_list ap;
	char *str;

	ASSERT(fmt != NULL);

	va_start(ap, fmt);
	str = vsprintf_alloc(fmt, ap);
	va_end(ap);

	return (str);
}

static inline char *
vsprintf_alloc(const char *fmt, va_list ap)
{
	va_list ap2;
	int l;
	char *str;

	ASSERT(fmt != NULL);

	va_copy(ap2, ap);
	l = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);

	ASSERT(l >= 0);
	str = (char *)safe_malloc(l + 1);
	VERIFY3S(vsnprintf(str, l + 1, fmt, ap), ==, l);

	return (str);
}

static inline int
fixed_decimals(double x, int digits)
{
	if (x > -1e-10 && x < 1e-10)
		return (MAX(digits - 1, 0));
	if (x < 0)
		x *= -1;
	/* This avoids the leading "0." not counting to the digit number */
	if (x < 1)
		digits = MAX(digits - 1, 0);
	return (clampi(digits - ceil(log10(x)), 0, digits));
}

#define	utf8_charlen	ACFSYM(utf8_charlen)
API_EXPORT size_t utf8_charlen(const char *str);
#define	utf8_strlen	ACFSYM(utf8_strlen)
API_EXPORT size_t utf8_strlen(const char *str);

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

API_EXPORT char *lacf_dirname(const char *filename);

#if	IBM && (defined(_GNU_SOURCE) || defined(_POSIX_C_SOURCE))

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
	time_t		st_atime;
	time_t		st_mtime;
};
API_EXPORT int stat(const char *pathname, struct stat *buf);
#endif	/* !defined(_MSC_VER) */

#define	sleep(x)	SleepEx((x) * 1000, FALSE)
#define	usleep(x)	SleepEx((x) / 1000, FALSE)

#endif	/* IBM && (defined(_GNU_SOURCE) || defined(_POSIX_C_SOURCE)) */

#if	IBM

#define	win_perror	ACFSYM(win_perror)
API_EXPORT void win_perror(DWORD err, PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(2);

#endif	/* IBM */

API_EXPORT void lacf_qsort_r(void *base, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *, void *), void *arg);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_HELPERS_H_ */
