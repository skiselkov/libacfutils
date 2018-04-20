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
 * Copyright 2015 Saso Kiselkov. All rights reserved.
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

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	NO_ACF_TYPE		0
#define	FF_A320_ACF_TYPE	1

#if	IBM
#define	DIRSEP		'\\'
#define	DIRSEP_S	"\\"	/* DIRSEP as a string */
#else	/* !IBM */
#define	DIRSEP		'/'
#define	DIRSEP_S	"/"	/* DIRSEP as a string */
#ifndef	MAX_PATH
#define	MAX_PATH	512
#endif
#endif	/* !IBM */

#if	defined(__GNUC__) || defined(__clang__)
#define	ALIGN(__var__, __bytes__)	\
	__var__ __attribute__((aligned (__bytes__)))
#else	/* !defined(__GNUC__) && !defined(__clang__) */
#define	ALIGN(__var__, __bytes__)	__declspec(align(__bytes__)) __var__
#endif	/* !defined(__GNUC__) && !defined(__clang__) */

#define	ALIGN4(__var__)		ALIGN(__var__, 4)
#define	ALIGN8(__var__)		ALIGN(__var__, 8)
#define	ALIGN16(__var__)	ALIGN(__var__, 16)
#define	ALIGN32(__var__)	ALIGN(__var__, 32)
#define	ALIGN64(__var__)	ALIGN(__var__, 64)

#if	defined(__GNUC__) || defined(__clang__)
#define	PRINTF_ATTR(x)		__attribute__((format(printf, x, x + 1)))
#define	PRINTF_ATTR2(x,y)	__attribute__((format(printf, x, y)))
#ifndef	BSWAP32
#define	BSWAP16(x)	__builtin_bswap16((x))
#define	BSWAP32(x)	__builtin_bswap32((x))
#define	BSWAP64(x)	__builtin_bswap64((x))
#endif	/* BSWAP32 */
#else	/* !__GNUC__ && !__clang__ */
#define	PRINTF_ATTR(x)
#ifndef	BSWAP32
#define	BSWAP16(x)	\
	((((x) & 0xff00u) >> 8) | \
	(((x) & 0x00ffu) << 8))
#define	BSWAP32(x)	\
	((((x) & 0xff000000u) >> 24) | \
	(((x) & 0x00ff0000u) >> 8) | \
	(((x) & 0x0000ff00u) << 8) | \
	(((x) & 0x000000ffu) << 24))
#define	BSWAP64(x)	\
	((((x) & 0x00000000000000ffllu) >> 56) | \
	(((x) & 0x000000000000ff00llu) << 40) | \
	(((x) & 0x0000000000ff0000llu) << 24) | \
	(((x) & 0x00000000ff000000llu) << 8) | \
	(((x) & 0x000000ff00000000llu) >> 8) | \
	(((x) & 0x0000ff0000000000llu) >> 24) | \
	(((x) & 0x00ff000000000000llu) >> 40) | \
	(((x) & 0xff00000000000000llu) << 56))
#endif	/* BSWAP32 */
#endif	/* !__GNUC__ && !__clang__ */

#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define	BE64(x)	(x)
#define	BE32(x)	(x)
#define	BE16(x)	(x)
#else	/* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */
#define	BE64(x)	BSWAP64(x)
#define	BE32(x)	BSWAP32(x)
#define	BE16(x)	BSWAP16(x)
#endif	/* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

#define	DESTROY(x)	do { free(x); (x) = NULL; } while (0)

#ifdef	WINDOWS
#define	PATHSEP	"\\"
#else
#define	PATHSEP	"/"
#endif

/* Minimum/Maximum allowable elevation AMSL of anything */
#define	MIN_ELEV	-2000.0
#define	MAX_ELEV	30000.0

/* Minimum/Maximum allowable altitude AMSL of anything */
#define	MIN_ALT		-2000.0
#define	MAX_ALT		100000.0

/* Maximum valid speed of anything */
#define	MAX_SPD		1000.0

/* Minimum/Maximum allowable arc radius on any procedure */
#define	MIN_ARC_RADIUS	0.1
#define	MAX_ARC_RADIUS	100.0

#define	UNUSED_ATTR		__attribute__((unused))
#define	UNUSED(x)		(void)(x)

/*
 * Compile-time assertion. The condition 'x' must be constant.
 */
#if	defined(__GNUC__) || defined(__clang__)
#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y)	\
	typedef char __compile_time_assertion__ ## y [(x) ? 1 : -1] \
	    __attribute__((unused))
#else	/* !defined(__GNUC__) && !defined(__clang__) */
#define	CTASSERT(x)
#endif	/* !defined(__GNUC__) && !defined(__clang__) */

/* generic parser validator helpers */

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
	while (hdg < 0)
		hdg += 360;
	while (hdg >= 360)
		hdg -= 360;
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

#if	defined(__GNUC__) || defined(__clang__)

#define	highbit64(x)	(64 - __builtin_clzll(x) - 1)
#define	highbit32(x)	(32 - __builtin_clzll(x) - 1)

#elif  defined(_MSC_VER)

static inline unsigned
highbit32(unsigned int x)
{
	unsigned long idx;
	_BitScanReverse(&idx, x);
	return (idx);
}

static inline unsigned
highbit64(unsigned long long x)
{
	unsigned long idx;
	_BitScanReverse64(&idx, x);
	return (idx);
}
#else
#error	"Compiler platform unsupported, please add highbit definition"
#endif

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

#if	!defined(MIN) && !defined(MAX) && !defined(AVG)
#define	MIN(x, y)	((x) < (y) ? (x) : (y))
#define	MAX(x, y)	((x) > (y) ? (x) : (y))
#define	AVG(x, y)	(((x) + (y)) / 2)
#endif	/* MIN or MAX */
/*
 * Provides a gradual method of integrating an old value until it approaches
 * a new target value. This is used in iterative processes by calling the
 * FILTER_IN macro repeatedly a certain time intervals (d_t = delta-time).
 * As time progresses, old_val will gradually be made to approach new_val.
 * The lag serves to make the approach slower or faster (e.g. a value of
 * '2' and d_t in seconds makes old_val approach new_val with a ramp that
 * is approximately 2 seconds long).
 */
#define	FILTER_IN(old_val, new_val, d_t, lag) \
	do { \
		__typeof__(old_val) o = (old_val); \
		__typeof__(new_val) n = (new_val); \
		(old_val) += (n - o) * ((d_t) / (lag)); \
		/* Prevent an overshoot */ \
		if ((o < n && (old_val) > n) || \
		    (o > n && (old_val) < n)) \
			(old_val) = n; \
	} while (0)
/*
 * Same as FILTER_IN, but handles NAN values for old_val and new_val properly.
 * If new_val is NAN, old_val is set to NAN. Otherwise if old_val is NAN,
 * it is set to new_val directly (without gradual filtering). Otherwise this
 * simply calls the FILTER_IN macro as normal.
 */
#define	FILTER_IN_NAN(old_val, new_val, d_t, lag) \
	do { \
		__typeof__(old_val) o = (old_val); \
		__typeof__(new_val) n = (new_val); \
		if (isnan(n)) \
			(old_val) = NAN; \
		else if (isnan(o)) \
			(old_val) = (new_val); \
		else \
			FILTER_IN(old_val, new_val, d_t, lag); \
	} while (0)

/*
 * Linearly interpolates old_val until it is equal to tgt. The current
 * time delta is d_t (in seconds). The interpolation speed is step/second.
 */
#define	FILTER_IN_LIN(old_val, tgt, d_t, step) \
	do { \
		double o = (old_val); \
		double t = (tgt); \
		double s; \
		if (o < t) \
			s = (d_t) * (step); \
		else \
			s = (d_t) * (-(step)); \
		if ((o < t && o + s > t) || (o > t && o + s < t)) \
			(old_val) = t; \
		else \
			(old_val) += s; \
	} while (0)

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
