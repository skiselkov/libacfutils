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

#define	BUILD_DIRSEP	'/'	/* we only build on Unix-like OSes */
#if	IBM
#define	DIRSEP		'\\'
#define	DIRSEP_S	"\\"	/* DIRSEP as a string */
#else	/* !IBM */
#define	DIRSEP		'/'
#define	DIRSEP_S	"/"	/* DIRSEP as a string */
#define	MAX_PATH	512
#endif	/* !IBM */

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
	((((x) & 0xff00) >> 8) | \
	(((x) & 0x00ff) << 8))
#define	BSWAP32(x)	\
	((((x) & 0xff000000) >> 24) | \
	(((x) & 0x00ff0000) >> 8) | \
	(((x) & 0x0000ff00) << 8) | \
	(((x) & 0x000000ff) << 24))
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
#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y)	\
	typedef char __compile_time_assertion__ ## y [(x) ? 1 : -1]

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
double rel_hdg_impl(double hdg1, double hdg2, const char *file, int line);

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

bool_t is_valid_icao_code(const char *icao);
const char *extract_icao_country_code(const char *icao);

bool_t is_valid_vor_freq(double freq_mhz);
bool_t is_valid_loc_freq(double freq_mhz);
bool_t is_valid_ndb_freq(double freq_khz);
bool_t is_valid_tacan_freq(double freq_mhz);
bool_t is_valid_rwy_ID(const char *rwy_ID);
void copy_rwy_ID(const char *src, char dst[4]);

/* AIRAC date functions */
const char *airac_cycle2eff_date(int cycle);
const char *airac_cycle2exp_date(int cycle);

/* CSV file & string processing helpers */
ssize_t parser_get_next_line(FILE *fp, char **linep, size_t *linecap,
    size_t *linenum);
char *parser_get_next_quoted_str(FILE *fp);
ssize_t explode_line(char *line, char delim, char **comps, size_t capacity);
void strip_space(char *line);
void append_format(char **str, size_t *sz, const char *format, ...)
    PRINTF_ATTR(3);

/* string processing helpers */
char **strsplit(const char *input, char *sep, bool_t skip_empty, size_t *num);
void free_strlist(char **comps, size_t len);
void unescape_percent(char *str);

char *mkpathname(const char *comp, ...);
char *mkpathname_v(const char *comp, va_list ap);
void fix_pathsep(char *str);

char *file2str(const char *comp, ...);
char *file2str_ext(long *len_p, const char *comp, ...);
char *file2str_name(long *len_p, const char *filename);
ssize_t filesz(const char *filename);

#if	IBM || LIN
void strlcpy(char *restrict dest, const char *restrict src, size_t cap);
#endif	/* IBM || LIN */
#if	IBM
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif	/* IBM */

#if	defined(__GNUC__) || defined(__clang__)
#define	highbit64(x)	(64 - __builtin_clzll(x) - 1)
#define	highbit32(x)	(32 - __builtin_clzll(x) - 1)
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

/* file/directory manipulation */
bool_t file_exists(const char *path, bool_t *isdir);
bool_t create_directory(const char *dirname);
bool_t create_directory_recursive(const char *dirname);
bool_t remove_directory(const char *dirname);
bool_t remove_file(const char *filename, bool_t notfound_ok);

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

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
void closedir(DIR *dirp);

/* A minimally compatible POSIX-style file stat reading implementation */
struct stat {
	uint64_t	st_size;
	uint64_t	st_atime;
	uint64_t	st_mtime;
};
int stat(const char *pathname, struct stat *buf);

void win_perror(DWORD err, const char *fmt, ...) PRINTF_ATTR(2);

#define	sleep(x)	SleepEx((x) * 1000, FALSE)

#endif	/* IBM */

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_HELPERS_H_ */
