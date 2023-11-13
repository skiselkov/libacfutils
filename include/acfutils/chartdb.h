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
 * This facility provides the ability to load charts from the following
 * online chart providers:
 * - Navigraph - requires a developer API key from Navigraph, as well as
 *	a user subscription to the service.
 * - Aeronav - a free service covering all of the United States as well
 *	as FAA-governed regions.
 * - Autorouter - a service requiring a free user account, covering most
 *	of Europe and some other countries.
 * @see chartdb_init()
 */

#ifndef	_ACF_UTILS_CHARTDB_H_
#define	_ACF_UTILS_CHARTDB_H_

#include <stdlib.h>

#include <cairo.h>

#include "geom.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAX_CHART_INSETS	16
#define	MAX_CHART_PROCS		24

typedef struct chartdb_s chartdb_t;

typedef enum {
	CHART_TYPE_UNKNOWN = 0,		/* Unknown chart type */
	CHART_TYPE_APD = 1 << 0,	/* Airport Diagram */
	CHART_TYPE_IAP = 1 << 1,	/* Instrument Approach Procedure */
	CHART_TYPE_DP = 1 << 2,		/* Departure Procedure */
	CHART_TYPE_ODP = 1 << 3,	/* Obstacle Departure Procedure */
	CHART_TYPE_STAR = 1 << 4,	/* Standard Terminal Arrival */
	CHART_TYPE_MIN = 1 << 5,	/* Takeoff Minimums */
	CHART_TYPE_INFO = 1 << 6,	/* Airport Information */
	CHART_TYPE_ALL = 0xffffffffu
} chart_type_t;

/**
 * Specifies login and security information for chart providers. This
 * data is supplied during a call to chartdb_init() and is mandatory for
 * the Autorouter and Navigraph providers, and optional for the Aeronav
 * provider.
 */
typedef struct {
	/**
	 * - For Navigraph, must contain the client ID supplied to you
	 *   by Navigraph as part of authorizing API access.
	 * - For Autorouter, must contain the end user's autorouter
	 *   account username.
	 * - For Aeronav, this field is ignored.
	 */
	char	*username;
	/**
	 * - For Navigraph, must contain the client secret supplied to you
	 *   by Navigraph as part of authorizing API access.
	 * - For Autorouter, must contain the end user's autorouter
	 *   account password.
	 * - For Aeronav, this field is ignored.
	 */
	char	*password;
	/**
	 * This is used by all providers to supply a list of trusted
	 * CA certificates for HTTPS server host verification. This must
	 * point to a file on disk which contains a list of CA certificates
	 * generated from the cURL source repository using the "make
	 * ca-bundle" command. This generates a file named "ca-bundle.crt",
	 * which you should ship with your addon and then provide a path to
	 * it in this field.
	 */
	char	*cainfo;
} chart_prov_info_login_t;

/**
 * Defines a rectangular bounding box using the coordinates of opposing
 * corners of the box. Chart coordinates have their origin in the upper
 * left and increase right and downwards.
 */
typedef struct {
	vect2_t		pts[2];
} chart_bbox_t;

/**
 * Chart geo-referencing data. This data consists of two sets of data points
 * - two pixel coordinate points in the `pixels` field
 * - two geographic coordinate points in the `pos` field
 * The points are meant to overlay each other, so `pixels[0]` gives the
 * graphical position of geographic coordinate `pos[0]` and same for
 * index 1.
 *
 * In addition, there's an optional number of chart insets, which are boxes
 * on the chart (their two corners given in pixel coordinates), for which
 * the georeferencing data is invalid. Those are typically things like
 * overlaid legends, or not-to-scale regions. You prevent an airplane symbol
 * from appearing in those insets, as it might confuse the crew as to the
 * position of the aircraft.
 */
typedef struct {
	bool_t		present;	/**< Is this georef data valid? */
	vect2_t		pixels[2];
	geo_pos2_t	pos[2];
	size_t		n_insets;	/**< Number of elements in `insets` */
	chart_bbox_t	insets[MAX_CHART_INSETS];
} chart_georef_t;

/**
 * Bounding boxes for various pre-defined chart views. Zero is referenced
 * to the top left. This is meant to define sections of a Jeppesen chart.
 * This info is only available when using the Navigraph chart provider.
 */
typedef enum {
    CHART_VIEW_HEADER,	/**< The "Briefing Strip" part of the chart. */
    CHART_VIEW_PLANVIEW,/**< The top-down mapping part of the chart. */
    CHART_VIEW_PROFILE,	/**< The side profile of an approach chart. */
    CHART_VIEW_MINIMUMS,/**< The minimums block of an approach chart. */
    NUM_CHART_VIEWS
} chart_view_t;

/**
 * List of instrument procedures associated with a particular chart,
 * in ARINC 424 procedure naming format. If present, this data ties
 * a chart to a particular coded instrument procedure, allowing your
 * avionics to auto-select the matching chart to an FMS procedure
 * selection by the flight crew.
 */
typedef struct {
	size_t		n_procs;
	char		procs[MAX_CHART_PROCS][8];
} chart_procs_t;

/**
 * Initializes a new chart database for a specific chart provider.
 * After initialization, the database can be accessed to retrieve
 * various charts asynchronously. The chart databases uses a background
 * thread to perform data fetches, and thus is safe to use from your
 * avionics threads without locking issues.
 *
 * @param cache_path A path to a suitable cache directory that the database
 *	can use to cache frequently accessed charts, or login information.
 *	This would typically be something like
 *	"<X-Plane>/Output/caches/<your-addon>/chartdb". This directory need
 *	not exist, the database will create it when necessary. If you are
 *	initializing multiple chart databases from different providers, you
 *	may reuse the same cache directory. You may NOT reuse the same
 *	cache directory when using multiple chart databases utilizing the
 *	same chart provider, as the providers can overwrite each other's
 *	on-disk state.
 * @param pdftoppm_path An optional path to a compiled binary of the
 *	pdftoppm utility from the Poppler project. This is used to convert
 *	PDF charts to bitmap images suitable for display. PDF charts are
 *	supplied by the Aeronav and Autorouter chart providers. If you
 *	specify `NULL` here, PDF chart support will not be available.
 * @param pdfinfo_path An optional path to a compiled binary of the
 *	pdfinfo utility from the Poppler project. This is used to handle
 *	PDF charts. PDF charts are supplied by the Aeronav and Autorouter
 *	chart providers. If you specify `NULL` here, PDF chart support will
 *	not be available.
 * @param airac The AIRAC cycle for which to initialize the provider. This
 *	information is used by the Aeronav provider, as charts are tied to
 *	a specific AIRAC cycle. Navigraph and Autorouter do not use this.
 * @param provider_name This must be one of:
 *	- "aeronav.faa.gov"
 *	- "autorouter.aero"
 *	- "navigraph.com"
 * @param provider_login When using the Navigraph or Autorouter providers
 *	this MUST be a pointer to a \ref chart_prov_info_login_t structure.
 *	When using the Aeronav provider, this is optional. You may free the
 *	memory associated with this structure after calling chartdb_init().
 *	@see chart_prov_info_login_t.
 * @return The initialized chart database if successful, or NULL otherwise.
 */
API_EXPORT chartdb_t *chartdb_init(const char *cache_path,
    const char *pdftoppm_path, const char *pdfinfo_path,
    unsigned airac, const char *provider_name,
    const chart_prov_info_login_t *provider_login);
/**
 * Frees and shuts down a chart database object, previously returned from
 * chartdb_init().
 */
API_EXPORT void chartdb_fini(chartdb_t *cdb);
/**
 * Front-end to chartdb_test_connection2() with a NULL proxy.
 * @see chartdb_test_connection2()
 */
API_EXPORT bool_t chartdb_test_connection(const char *provider_name,
    const chart_prov_info_login_t *creds);
/**
 * Provides a method for testing the connection credentials for a chart
 * provider. Currently this is only used by the Autorouter provider. This
 * way, you can test whether the supplied credentials are correct, before
 * accepting them from the user to save for future use.
 * @param provider_name The chart provider to use for the connection test.
 *	Currently this must always be "autorouter.aero".
 * @param creds The login credentials to use for the connection test.
 *	All fields of the structure must be populated. See \ref
 *	chart_prov_info_login_t for more information.
 * @param proxy An optional field specifying a proxy to use for the
 *	connection test. See \ref chartdb_set_proxy() for information
 *	on the format of the proxy specification expected here.
 */
API_EXPORT bool_t chartdb_test_connection2(const char *provider_name,
    const chart_prov_info_login_t *creds, const char *proxy);
/**
 * Sets the size of the on-disk cache for the chart database. This is only
 * used by the Aeronav and Autorouter providers (Navigraph never caches
 * anything, as the terms of use of the API prohibit disk caching). If not
 * specified, the default value is 1/32 of the machine's physical memory,
 * or 256MB, whichever is lower.
 * @param bytes The new maximum sizeof the disk cache in bytes.
 */
API_EXPORT void chartdb_set_load_limit(chartdb_t *cdb, uint64_t bytes);
/**
 * Instructs the chart database to immediately purge its disk cache.
 * You should generally not need to ever do this, as cache management
 * is automatic.
 */
API_EXPORT void chartdb_purge(chartdb_t *cdb);
/**
 * Sets the network proxy to use for chart data downloading. By default
 * no proxy is used for chart downloads.
 * @param proxy The proxy specifier string, or NULL when you wish to
 *	switch back to not using a proxy. The format for proxy specification
 *	follows the libcurl format (see below).
 * @see https://curl.se/libcurl/c/CURLOPT_PROXY.html
 */
API_EXPORT void chartdb_set_proxy(chartdb_t *cdb, const char *proxy);
/**
 * Retrieves the current network proxy string used by the database.
 * @param proxy A writable buffer, which will be filled with the proxy
 *	string in use. If no proxy is in use, this will be filled with
 *	an empty string.
 * @param cap The capacity of the `proxy` buffer in bytes. The function
 *	will never write more than `cap` bytes to the `proxy` buffer and
 *	will always properly NUL-terminate the string, even if truncated.
 * @return The size of the proxy string, including the terminating NUL
 *	character. If no proxy is in use, this function instead returns 0.
 */
API_EXPORT size_t chartdb_get_proxy(chartdb_t *cdb, char *proxy, size_t cap);
/**
 * Fetches the list of available charts for a given airport from the chart
 * provider.
 * @param icao The ICAO code of the airport for which to query the provider.
 * @param type The type of chart to retrieve.
 * @param num_charts Mandatory return argument which will be filled with the
 *	number of charts in the returned string list.
 * @return A list of strings containing the unique chart names which match
 *	the search criteria. These chart names are used in other chart
 *	database queries to retrieve information about a particular chart.
 *	The number of charts in this list will be returned in `num_charts`.
 *	If no charts match the search criteria, this function returns NULL
 *	and sets `num_charts` to 0.
 * @return You must free the returned list using chartdb_free_str_list().
 */
API_EXPORT char **chartdb_get_chart_names(chartdb_t *cdb, const char *icao,
    chart_type_t type, size_t *num_charts);
/**
 * Frees a list of charts returned from chartdb_get_chart_names().
 * @param name_list The list of chart names returned from
 *	chartdb_get_chart_names().
 * @param num Number of items in the chart ID list.
 */
API_EXPORT void chartdb_free_str_list(char **name_list, size_t num);
/**
 * Retrieves the chart code name for a particular chart. The meaning of
 * the code name varies by provider.
 * - for Navigraph, this is the Jeppesen chart index number, e.g. "10-9".
 * - for Aeronav, this is the contents of the "faanfd18" field, identifying
 *	the procedure tuple on departure procedures, e.g. "AKUMY4.AKUMY".
 * - for Autorouter, this field is not defined.
 * @param icao The ICAO code of the airport for which the chart exists.
 * @param chart_name The name of the chart as returned from
 *	chartdb_get_chart_names().
 * @return A copy of the chart codename, or NULL if no value is defined.
 *	You must free the returned string using lacf_free().
 */
API_EXPORT char *chartdb_get_chart_codename(chartdb_t *cdb,
    const char *icao, const char *chart_name);
/**
 * @return The type of the chart. The chart MUST exist.
 * @param icao The ICAO code of the airport for which the chart exists.
 * @param chart_name The name of the chart as returned from
 *	chartdb_get_chart_names().
 */
API_EXPORT chart_type_t chartdb_get_chart_type(chartdb_t *cdb,
    const char *icao, const char *chart_name);
/**
 * @return Chart geo-referencing data for the specified chart. The chart MUST
 *	exist. If the chart has no geo-referencing data, the returned info
 *	structure will have its `present` field set to `B_FALSE`.
 * @param icao The ICAO code of the airport for which the chart exists.
 * @param chart_name The name of the chart as returned from
 *	chartdb_get_chart_names().
 */
API_EXPORT chart_georef_t chartdb_get_chart_georef(chartdb_t *cdb,
    const char *icao, const char *chart_name);
/**
 * @return Chart view information for the specified chart and chart view.
 *	If the chart doesn't have the requested view specified, the function
 *	returns a \ref chart_bbox_t with both points set to zero.
 * @param icao The ICAO code of the airport for which the chart exists.
 * @param chart_name The name of the chart as returned from
 *	chartdb_get_chart_names().
 * @param view The chart view which to retrieve.
 */
API_EXPORT chart_bbox_t chartdb_get_chart_view(chartdb_t *cdb,
    const char *icao, const char *chart_name, chart_view_t view);
/**
 * @return Information about associated coded instrument flight procedures
 *	corresponding to a particular chart. If no flight procedures are
 *	defined for the chart, the retuend \ref chart_procs_t structure will
 *	have its `n_procs` field set to 0.
 * @param icao The ICAO code of the airport for which the chart exists.
 * @param chart_name The name of the chart as returned from
 *	chartdb_get_chart_names().
 */
API_EXPORT chart_procs_t chartdb_get_chart_procs(chartdb_t *cdb,
    const char *icao, const char *chart_name);
/**
 * Retrieves chart pixel data. Please note that while this function can
 * initiate a background chart download and rasterization, it will never
 * block. If the chart is not yet ready, the function will return `B_FALSE`
 * to let you know that the chart isn't ready. You should keep retrying for
 * a certain amount of time (e.g. 20 seconds) before giving up and
 * presenting an error message to the user. Charts returned from
 * chartdb_get_chart_names() *should* exist and be retrievable, so long as
 * there are no network or disk access errors. If any of the `page`,
 * `zoom` or `night` parameters changes, the database will load a new
 * image surface and return that instead. You shouldn't need to cache the
 * returned image data in your application, the chart database takes care
 * of all caching needs automatically.
 *
 * @param icao The ICAO code of the airport for which the chart exists.
 * @param chart_name The name of the chart as returned from
 *	chartdb_get_chart_names().
 * @param page If the chart is a PDF with multiple pages, this specifies
 *	the page number to return (starting at 0 and up to `num_pages` -
 *	see below).
 * @param zoom Relative zoom value to use when rasterizing vector image
 *	charts (PDFs), in the range of 0.1 - 10. A zoom value of 1.0
 *	corresponds to roughly 72 DPI. This has no effect when dealing with
 *	raster charts (Navigraph).
 * @param night Specifies whether to return a day-light or night-light
 *	optimized chart. For chart providers which supply different charts
 *	for day and night operations (Navigraph), this retrieves the
 *	appropriate chart image. Otherwise, the chart database automatically
 *	inverts the colors on the chart to reduce the overall brightness
 *	of black-on-white charts.
 * @param surf A mandatory return argument which, if this function returns
 *	`B_TRUE`, will be filled with a cairo_surface_t image buffer
 *	containing the pixel data suitable for compositing into a cairo_t
 *	context, or which you can read the raw pixels from.
 *	Once created, the surface is immutable, so you can use it for
 *	rendering at a later date without having to worry about
 *	synchronizing data access. Please note that when you are done with
 *	the surface, you MUST release it by calling cairo_surface_destroy(),
 *	or using the \ref CAIRO_SURFACE_DESTROY() macro. If this function
 *	returns `B_FALSE`, the `surf` pointer will be set to `NULL`.
 * @param num_pages An optional return argument which will be filled with
 *	the number of pages available, if this is a multi-page chart. Only
 *	PDF charts from Aeronav and Autorouter can be multi-page. Navigraph
 *	charts are always single-page.
 * @return `B_TRUE` if the request succeeded, in which case `surf` will
 *	be filled with a reference to the image surface data.
 */
API_EXPORT bool_t chartdb_get_chart_surface(chartdb_t *cdb,
    const char *icao, const char *chart_name, int page, double zoom,
    bool_t night, cairo_surface_t **surf, int *num_pages);
/**
 * Queries the chart database whether it's ready to start processing chart
 * requests. Some chart providers need to perform disk or network I/O before
 * they can start to operate.
 * @return `B_TRUE` if the chart database is ready to start processing chart
 *	requests, `B_FALSE` otherwise. You should postpone reporting issues
 *	about starts not being available until this returns `B_TRUE`.
 */
API_EXPORT bool_t chartdb_is_ready(chartdb_t *cdb);
/**
 * Queries the database on whether it has charts available for a given airport.
 * @param icao The ICAO code of the airport which to check chart availability
 *	for.
 * @return `B_TRUE` if the given airport has at least one chart available,
 *	`B_FALSE` otherwise. If this returns true, you can use
 *	chartdb_get_chart_names() to retrieve the list of available charts
 *	for this airport.
 */
API_EXPORT bool_t chartdb_is_arpt_known(chartdb_t *cdb, const char *icao);
/**
 * Queries the human-readable airport name from the chart database about
 * a particular airport.
 * @param icao The ICAO code of the airport for which to retrieve the info.
 * @return The airport name associated with the airport, or `NULL` unknown.
 *	You must free the returned string using lacf_free().
 */
API_EXPORT char *chartdb_get_arpt_name(chartdb_t *cdb, const char *icao);
/**
 * Queries the human-readable city name from the chart database about
 * a particular airport.
 * @param icao The ICAO code of the airport for which to retrieve the info.
 * @return The city name associated with the airport, or `NULL` if unknown.
 *	You must free the returned string using lacf_free().
 */
API_EXPORT char *chartdb_get_arpt_city(chartdb_t *cdb, const char *icao);
/**
 * Queries the human-readable state name name from the chart database about
 * a particular airport.
 * @param icao The ICAO code of the airport for which to retrieve the info.
 * @return The state name associated with the airport, or `NULL` unknown.
 *	You must free the returned string using lacf_free().
 */
API_EXPORT char *chartdb_get_arpt_state(chartdb_t *cdb, const char *icao);
/**
 * Utility function to retrieve the latest METAR from the
 * [aviationweather.gov/metar](https://aviationweather.gov/metar) service. This
 * function doesn't block for the download. Instead, the download is queued
 * on the chart database's background thread and the METAR is returned
 * if/when it becomes available.
 * @param cdb The chart database to use for the download. The chart provider
 *	of the database doesn't matter. This function always queries the
 *	[aviationweather.gov](https://aviationweather.gov/) service.
 * @param icao The ICAO code of the station for which to download the METAR.
 * @return The raw METAR text data. If the download hasn't succeeded yet
 *	(e.g. due to not being available, being in progress, or due to a
 *	network error), this function returns `NULL` instead. You should keep
 *	calling this function at regular intervals, to retry the download.
 *	Also, regular calls should be being made even if the download
 *	succeded, as a new METAR might have been issued in the mean time.
 *	The downloader implements an automatic rate limiter to prevent
 *	overloading the network server with continuous requests.
 * @return The returned string must be freed using lacf_free().
 * @see chartdb_get_taf()
 */
API_EXPORT char *chartdb_get_metar(chartdb_t *cdb, const char *icao);
/**
 * Same as chartdb_get_metar(), but retrieves TAF data from the
 * [aviationweather.gov/taf](https://aviationweather.gov/taf) service.
 * @see chartdb_get_metar()
 */
API_EXPORT char *chartdb_get_taf(chartdb_t *cdb, const char *icao);
/**
 * Queries the chart database whether external account setup is pending.
 * This is specifically used by the Navigraph chart provider, which
 * requires the user to log in and authorize the simulator through an
 * external web browser. While the chart provider should launch the browser
 * automatically, it is helpful to show a popup window in the simulator,
 * letting the user know that they need to perform the authorization step
 * before being able to proceed.
 * @return `B_TRUE` when the chart provider of the database is awaiting
 *	external account setup completion, `B_FALSE` if it isn't. This
 *	will typically only be required once, as the chart provider will
 *	cache any access tokens for automatic connection setup in the
 *	future.
 */
API_EXPORT bool_t chartdb_pending_ext_account_setup(chartdb_t *cdb);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CHARTDB_H_ */
