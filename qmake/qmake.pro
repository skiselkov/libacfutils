# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END

# Copyright 2024 Saso Kiselkov. All rights reserved.

# Shared library without any Qt functionality
TEMPLATE = lib
QT -= gui core
CONFIG += staticlib

QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64

CONFIG += warn_on plugin debug
CONFIG -= thread exceptions qt rtti release

VERSION = $$system("git describe --abbrev=0 --tags | \
    sed 's/[[:alpha:].]//g; s/^0//g'")

debug = $$[DEBUG]
dll = $$[ACFUTILS_DLL]
noerrors = $$[NOERRORS]
minimal=$$system("test -f ../.minimal-deps; echo $?")
noxplm=$$[ACFUTILS_NOXPLM]

INCLUDEPATH += ../src ../SDK/CHeaders/XPLM
INCLUDEPATH += ../SDK/CHeaders/Widgets
INCLUDEPATH += ../lzma/C
INCLUDEPATH += ../junzip
QMAKE_CFLAGS += -std=c11 -g -W -Wall -Wextra -Werror=vla -fvisibility=hidden
contains(noerrors, 0) {
	QMAKE_CFLAGS += -Werror
}
QMAKE_CFLAGS += -Wunused-result
!macx {
	QMAKE_CFLAGS += -Wno-format-truncation -Wno-cast-function-type
	QMAKE_CFLAGS += -Wno-stringop-overflow -Wno-missing-field-initializers
}

win32 {
	PLAT_LONG = win-64
	GLEWMX_LIB = ../glew/glew-1.13.0-win-64/install/lib/libglew32mx.a
}
linux-g++-64 {
	PLAT_LONG = linux-64
	GLEWMX_LIB = ../glew/glew-1.13.0-linux-64/install/lib64/libGLEWmx.a
}
macx {
	PLAT_LONG = mac-64
	GLEWMX_LIB = ../glew/glew-1.13.0-mac-64/install/lib/libGLEWmx.a
}

# _GNU_SOURCE needed on Linux for getline()
# DEBUG - used by our ASSERT macro
# _FILE_OFFSET_BITS=64 to get 64-bit ftell and fseek on 32-bit platforms.
# _USE_MATH_DEFINES - sometimes helps getting M_PI defined from system headers
DEFINES += _GNU_SOURCE DEBUG _FILE_OFFSET_BITS=64
DEFINES += GL_GLEXT_PROTOTYPES
DEFINES += ACFUTILS_BUILD

# Latest X-Plane APIs. No legacy support needed.
DEFINES += XPLM200 XPLM210 XPLM300 XPLM301

contains(minimal, 1) {
	# We want OpenAL soft extensions
	DEFINES += AL_ALEXT_PROTOTYPES
}

DEFINES += LIBACFUTILS_VERSION=\'\"$$system("git rev-parse --short HEAD")\"\'
DEFINES += GLEW_STATIC

# JSMN library needs this to avoid multiply defined symbols
DEFINES += JSMN_STATIC

TARGET = acfutils
QMAKE_TARGET_COMPANY = Saso Kiselkov
QMAKE_TARGET_PRODUCT = libacfutils
QMAKE_TARGET_DESCRIPTION = libacfutils is library of utility functions
QMAKE_TARGET_DESCRIPTION += for X-Plane addon authors.
QMAKE_TARGET_COPYRIGHT = Copyright (c) 2020 Saso Kiselkov. All rights reserved.

contains(dll, 1) {
	DEFINES += ACFUTILS_DLL=1
}

contains(debug, 0) {
	QMAKE_CFLAGS += -O2
}

contains(noxplm, 1) {
	DEFINES += _LACF_WITHOUT_XPLM
}

win32 {
	# Minimum Windows version is Windows Vista (0x0600)
	DEFINES += APL=0 IBM=1 LIN=0 MSDOS _WIN32_WINNT=0x0600

	# Some older MinGW builds didn't define M_PI in math.h, so
	# to cope with that, we define them all here:
	DEFINES += M_PI=3.14159265358979323846
	DEFINES += M_PI_2=1.57079632679489661923
	DEFINES += M_PI_4=0.785398163397448309616
	DEFINES += M_1_PI=0.318309886183790671538
	DEFINES += M_2_PI=0.636619772367581343076
	DEFINES += M_2_SQRTPI=1.12837916709551257390

	QMAKE_CFLAGS += -Wno-misleading-indentation
	QMAKE_DEL_FILE = rm -f
	LIBS += -static-libgcc
	contains(dll, 1) {
		CONFIG -= staticlib
		CONFIG += dll
		LIBS += -Wl,--output-def,acfutils.def
	}
}

win32:contains(CROSS_COMPILE, x86_64-w64-mingw32-) {
	contains(minimal, 1) {
		QMAKE_CFLAGS += $$system("../pkg-config-deps win-64 \
		    --static-openal --cflags")

		contains(dll, 1) {
			LIBS += $$system("../pkg-config-deps win-64 \
			    --whole-archive --static-openal \
			    --no-link-acfutils --libs")
		}
		contains(dll, 0) {
			LIBS += $$system("../pkg-config-deps win-64 \
			    --static-openal --libs")
		}
	} else {
		QMAKE_CFLAGS += $$system("../pkg-config-deps win-64 --cflags")
		LIBS += $$system("../pkg-config-deps win-64 --libs")
	}

	LIBS += -L../SDK/Libraries/Win -lXPLM_64
	LIBS += -L../SDK/Libraries/Win -lXPWidgets_64
	LIBS += -lglu32 -lopengl32
	LIBS += -ldbghelp
}

linux-g++-64 {
	DEFINES += APL=0 IBM=0 LIN=1
	# The stack protector forces us to depend on libc,
	# but we'd prefer to be static.
	QMAKE_CFLAGS += -fno-stack-protector
	contains(minimal, 1) {
		QMAKE_CFLAGS += $$system("../pkg-config-deps linux-64 \
		    --static-openal --cflags")
	} else {
		QMAKE_CFLAGS += $$system("../pkg-config-deps linux-64 --cflags")
	}
	QMAKE_CFLAGS += -Wno-misleading-indentation
}

macx {
	DEFINES += APL=1 IBM=0 LIN=0
	DEFINES += LACF_GLEW_USE_NATIVE_TLS=0
	DEFINES += GL_SILENCE_DEPRECATION
	QMAKE_MACOSX_DEPLOYMENT_TARGET=10.13
}

macx-clang {
	contains(minimal, 1) {
		QMAKE_CFLAGS += $$system("../pkg-config-deps mac-64 \
		    --static-openal --cflags")
	} else {
		QMAKE_CFLAGS += $$system("../pkg-config-deps mac-64 --cflags")
	}
}

# Core lib headers & sources
HEADERS += \
    ../src/acfutils/acf_file.h \
    ../src/acfutils/airportdb.h \
    ../src/acfutils/assert.h \
    ../src/acfutils/avl.h \
    ../src/acfutils/avl_impl.h \
    ../src/acfutils/base64.h \
    ../src/acfutils/cmd.h \
    ../src/acfutils/compress.h \
    ../src/acfutils/conf.h \
    ../src/acfutils/core.h \
    ../src/acfutils/crc64.h \
    ../src/acfutils/delay_line.h \
    ../src/acfutils/dr_cmd_reg.h \
    ../src/acfutils/dr.h \
    ../src/acfutils/except.h \
    ../src/acfutils/geom.h \
    ../src/acfutils/helpers.h \
    ../src/acfutils/lacf_getline.h \
    ../src/acfutils/parser_funcs.h \
    ../src/acfutils/hexcode.h \
    ../src/acfutils/hp_filter.h \
    ../src/acfutils/htbl.h \
    ../src/acfutils/icao2cc.h \
    ../src/acfutils/intl.h \
    ../src/acfutils/joystick.h \
    ../src/acfutils/libconfig.h \
    ../src/acfutils/list.h \
    ../src/acfutils/list_impl.h \
    ../src/acfutils/log.h \
    ../src/acfutils/math_core.h \
    ../src/acfutils/math.h \
    ../src/acfutils/mslibs.h \
    ../src/acfutils/osrand.h \
    ../src/acfutils/perf.h \
    ../src/acfutils/pid_ctl.h \
    ../src/acfutils/pid_ctl_parsing.h \
    ../src/acfutils/safe_alloc.h \
    ../src/acfutils/sysmacros.h \
    ../src/acfutils/taskq.h \
    ../src/acfutils/thread.h \
    ../src/acfutils/time.h \
    ../src/acfutils/tls.h \
    ../src/acfutils/tumbler.h \
    ../src/acfutils/types.h \
    ../src/acfutils/vector.h \
    ../src/acfutils/vector_impl.h \
    ../src/acfutils/wmm.h \
    ../src/acfutils/worker.h \
    ../src/acfutils/xpfail.h

SOURCES += \
    ../src/acf_file.c \
    ../src/airportdb.c \
    ../src/avl.c \
    ../src/base64.c \
    ../src/cmd.c \
    ../src/compress_7z.c \
    ../src/compress_zip.c \
    ../src/compress_zlib.c \
    ../src/conf.c \
    ../src/core.c \
    ../src/crc64.c \
    ../src/dr.c \
    ../src/dr_cmd_reg.c \
    ../src/except.c \
    ../src/GeomagnetismLibrary.c \
    ../src/geom.c \
    ../src/helpers.c \
    ../src/hexcode.c \
    ../src/htbl.c \
    ../src/icao2cc.c \
    ../src/intl.c \
    ../src/list.c \
    ../src/log.c \
    ../src/math.c \
    ../src/osrand.c \
    ../src/perf.c \
    ../src/taskq.c \
    ../src/time.c \
    ../src/thread.c \
    ../src/tumbler.c \
    ../src/vector.c \
    ../src/wmm.c \
    ../src/worker.c

# Dependency headers & sources
HEADERS +=  \
    ../junzip/junzip.h \
    ../ucpp/nhash.h \
    ../ucpp/tune.h \
    ../ucpp/arith.h \
    ../ucpp/hash.h \
    ../ucpp/mem.h \
    ../ucpp/config.h \
    ../ucpp/ucppi.h

SOURCES += \
    ../junzip/junzip.c \
    ../ucpp/mem.c \
    ../ucpp/nhash.c \
    ../ucpp/cpp.c \
    ../ucpp/lexer.c \
    ../ucpp/assert.c \
    ../ucpp/macro.c \
    ../ucpp/eval.c

exists("../libpng/libpng-$$PLAT_LONG/lib/libpng16.a") {
	HEADERS += ../src/acfutils/png.h
	SOURCES += ../src/png.c
}
exists("../cairo/cairo-$$PLAT_LONG/lib/libcairo.a") {
	HEADERS += ../src/acfutils/cairo_utils.h
	SOURCES += ../src/cairo_utils.c
}
exists("../cairo/cairo-$$PLAT_LONG/lib/libcairo.a") : exists("$$GLEWMX_LIB") {
	HEADERS += \
	    ../src/acfutils/mt_cairo_render.h \
	    ../src/acfutils/widget.h
	SOURCES += \
	    ../src/mt_cairo_render.c \
	    ../src/widget.c
}

exists("../lzma/qmake/$$PLAT_LONG/liblzma.a") {
	HEADERS += ../src/acfutils/dsf.h
	SOURCES += ../src/dsf.c
}

# Optional lib components when building a non-minimal library
contains(minimal, 1) {
	HEADERS += \
	    ../src/acfutils/apps.h \
	    ../src/acfutils/chartdb.h \
	    ../src/acfutils/cursor.h \
	    ../src/acfutils/font_utils.h \
	    ../src/acfutils/glctx.h \
	    ../src/acfutils/glew.h \
	    ../src/acfutils/glew_os.h \
	    ../src/acfutils/glutils.h \
	    ../src/acfutils/lacf_gl_pic.h \
	    ../src/acfutils/odb.h \
	    ../src/acfutils/paste.h \
	    ../src/acfutils/riff.h \
	    ../src/acfutils/shader.h \
	    ../src/acfutils/wav.h \
	    ../src/acfutils/jsmn/*.h

	SOURCES += \
	    ../src/apps.c \
	    ../src/chartdb.c \
	    ../src/chart_prov_autorouter.c \
	    ../src/chart_prov_common.c \
	    ../src/chart_prov_faa.c \
	    ../src/chart_prov_navigraph.c \
	    ../src/font_utils.c \
	    ../src/glctx.c \
	    ../src/glew.c \
	    ../src/glew_os.c \
	    ../src/glutils.c \
	    ../src/lacf_gl_pic.c \
	    ../src/minimp3.c \
	    ../src/odb.c \
	    ../src/paste.c \
	    ../src/riff.c \
	    ../src/shader.c \
	    ../src/wav.c

	win32 {
		SOURCES += ../src/platform/cursor-win.c
	}
	linux-g++-64 {
		SOURCES += ../src/platform/cursor-lin.c
	}
	macx {
		SOURCES += ../src/platform/cursor-mac.m
	}
}
