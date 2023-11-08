\mainpage

# libacfutils

This is a general purpose library of utility functions designed to make
it easier to develop addons for the X-Plane flight simulator. It comes
with everything needed to build it on Linux or macOS. No build support
for Windows currently, sorry. The Linux version produces the Windows
version of the library through a MinGW cross-compile.

## LICENSE

This library is licensed under the CDDL 1.0 license. This lets you
incorporate it into your projects (even closed-source/payware ones)
without disclosing source code. The license only requires that you
disclose modifications that you have done to the source code comprising
**this library**. Any files that are not part of this library are **not**
covered by this requirement.

## How the library is organized

The top level of this source tree includes the dependencies organized
into their individual sub-directories + build scripts. The source code of
the library itself is located under `src`. The documentation is part of
the `.c` files. The headers listing all the functions are under
`src/acfutils`. To make the includes work properly, direct your compiler
to include from the `src` sub-directory. Then include library headers
using the following path:

```
#include <acfutils/filename.h>
```

## What's included

* `acfutils/acf_file.h`: a parser for X-Plane's `.acf' files and support
extracting individual properties.

* `acfutils/airportdb.h`: a global airport database, dynamically
constructed from X-Plane's scenery, including highly accurate runway
locations + automatic calculation of approach sectors. This is useful
for determining your exact location with respect to a runway (e.g. when
implementing a runway-awareness system). You can also use it to enumerate
all nearby airports in a thread-friendly manner, without having to go
through X-Plane's `XPLMNavigation.h` interface.

* `acfutils/assert.h`: a highly flexible assertion checking facility with
support for logging backtraces to the X-Plane `Log.txt` file.

* `acfutils/avl.h`: generic binary search trees for storage of arbitrary
data.

* `acfutils/compress.h`: a convenience frontend to the zlib (deflate) and
7-zip compression algorithms.

* `acfutils/conf.h`: a general purpose configuration file facility with
support for `key=value` pairs and a convenient access interface.

* `acfutils/crc64.h`: an implementation of the CRC64 checksum algorithm,
as well as a high-performance portable random number generator based on
the CRC64 algorithm.

* `acfutils/dr.h`: a simple and highly flexible interface to X-Plane's
dataref system. No need to write custom callbacks anymore or hope you got
your data types right. Simply call one function to read or write datarefs
in any format and `dr.h` automatically handles all data type conversions
for you.

* `acfutils/dsf.h`: a simple set of functions to read & parse X-Plane's
DSF files.

* `acfutils/geom.h`: a large collection of geometry-related convenience
functions, including:

  * Manipulation of 2D- and 3D-vectors. Includes vector geometry
  constructions such as intersections, parallel tests, arithmetic,
  rotations, products, etc.

  * Geographic & geodesic coordinate manipulation. Allows translating
  between geographic coordinates (lat + lon + elev) into 3D vector
  coordinate space. Supports both spherical as well as geodesic coordinates
  (including the WGS84 ellipsoid).

  * Arbitrary flat-plane projections from spherical and geodesic
  coordinates, including some common canned projections such as orthographic,
  stereographic, gnomonic and lambert-conformal projections.

  * Function interpolation for functions defined using a set of quadratic
  bezier curve segments.

* `acfutils/icao2cc.h`: functions to translate an ICAO airport code into
the country code of the containing country as well as the major language
used in the region of the airport.

* `acfutils/intl.h`: a set of simple and portable internationalization
functions. Simply supply a set of files in the `strings.po` format and
you can use the `_()` macro to translate them on-the-fly.

* `acfutils/list.h`: generic doubly-linked lists for storage of arbitrary
data.

* `acfutils/log.h`: convenience front-end to X-Plane `Log.txt` logging
facility, with support for printf-style format strings and automatic
appending of source code file names & line numbers for easy debugging.

* `acfutils/math.h`: a set of useful mathematical functions, such as:

  * a generic quadratic equation solver

  * linear function extrapolation (both single- and multi-segment linear
  functions)

  * weighted averages

  * value clamping

* `acfutils/mt_cairo_render.h`: a double-buffered multi-threaded Cairo
canvas with automatic OpenGL compositing support. Simply supply a
callback to draw the surface and tell it where to draw in your OpenGL
output. You can configure rendering at a fixed FPS, or on-demand. Since
the render takes place on a background thread, it doesn't interlock with
X-Plane's OpenGL renderer. Also includes a convenience FreeType font
loader function to simplify font handling.

* `acfutils/osrand.h`: a simple frontend to an OS-specific high quality
random number generator. Use this to generate secure cryptographic keys.
Uses `/dev/random` on Linux and macOS and `CryptGenRandom` on Windows.

* `acfutils/paste.h`: a platform-independent interface for cut-and-paste
functionality on macOS, Windows and Linux.

* `acfutils/perf.h`: a large collection aerodynamics &
performance-calculation functions. Includes many common physical unit
conversion macros, as well as functions to convert air data such as
impact pressures into airspeeds, mach-to-TAS, TAT-to-SAT, etc.

* `acfutils/png.h`: a simple frontend to libpng for reading and writing
PNG files.

* `acfutils/riff.h`: a general-purpose RIFF file parser. Primarily used
to read WAV files. For a more convenient interface to working with OpenAL
as well as a variety of sound file formats, see `wav.h`.

* `acfutils/shader.h`: a set of shorthand functions to load OpenGL GLSL
program shaders using a single call.

* `acfutils/thread.h`: platform-independent multi-threading primitives.
Provides facilities for starting, stopping and synchronizing multiple
execution threads. Uses the appropriate OS-specific backend underneath.

* `acfutils/time.h`: a platform-independent mechanism for getting
microsecond-accurate timestamps anchored to UNIXTIME (microseconds since
UTC 1970-01-01).

* `acfutils/types.h`: special typedefs used in the library. This should
automatically be included by the appropriate library headers, so you
shouldn't need to include this explicitly.

* `acfutils/wav.h`: a generic OpenAL and sound file interface. Provides
facilities for loading files formatted in WAV, Opus and MP3 file formats.
Also provides convenience functions for manipulation OpenAL's dedicated
contexts and spacial sound parameters.

* `acfutils/widget.h`: helper functions to provide relative positioning
of `XPWidget` objects. Also a generic popup-tooltip facility that can be
attached to any X-Plane widget.

* `acfutils/wmm.h`: convenience functions for accessing
world-magnetic-model files (`WMM.COF`). Simply load a `WMM.COF` file and
translate between magnetic and true headings using just a couple of tiny
functions.

* `acfutils/worker.h`: a general-purpose background worker thread
abstraction object. Provides the ability to simply define a callback
function, which will then be called a fixed interval in a separate
thread, as well as the ability to synchronize with and terminate the
background worker thread without having to deal with keeping track of
individual locks and condition variables.
