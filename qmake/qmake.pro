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

# Copyright 2016 Saso Kiselkov. All rights reserved.

# Shared library without any Qt functionality
TEMPLATE = lib
QT -= gui core
CONFIG += staticlib

CONFIG += warn_on plugin debug
CONFIG -= thread exceptions qt rtti release

VERSION = 1.0.0

INCLUDEPATH += ../src ../OpenAL/include ../SDK/CHeaders/XPLM
INCLUDEPATH += ../SDK/CHeaders/Widgets
INCLUDEPATH += ../glew
INCLUDEPATH += ../lzma/C
QMAKE_CFLAGS += -std=c99 -O2 -g -W -Wall -Wextra -Werror -fvisibility=hidden
QMAKE_CFLAGS += -Wunused-result

# _GNU_SOURCE needed on Linux for getline()
# DEBUG - used by our ASSERT macro
# _FILE_OFFSET_BITS=64 to get 64-bit ftell and fseek on 32-bit platforms.
# _USE_MATH_DEFINES - sometimes helps getting M_PI defined from system headers
DEFINES += _GNU_SOURCE DEBUG _FILE_OFFSET_BITS=64 _USE_MATH_DEFINES
DEFINES += GL_GLEXT_PROTOTYPES

# Latest X-Plane APIs. No legacy support needed.
DEFINES += XPLM200 XPLM210

DEFINES += GLEW_BUILD=GLEW_STATIC

DEFINES += LIBACFUTILS_VERSION=\'\"$$system("git rev-parse --short HEAD")\"\'

TARGET = acfutils

win32 {
	# Minimum Windows version is Windows Vista (0x0600)
	DEFINES += APL=0 IBM=1 LIN=0 _WIN32_WINNT=0x0600
	QMAKE_DEL_FILE = rm -f
	QMAKE_CFLAGS -= -Werror

	CONFIG += dll
	LIBS += -static-libgcc -Wl,--output-def,acfutils.def
}

win32:contains(CROSS_COMPILE, x86_64-w64-mingw32-) {
	QMAKE_CFLAGS += $$system("../pkg-config-deps win-64 --cflags")

	LIBS += $$system("../pkg-config-deps win-64 --libs")
	LIBS += -L../SDK/Libraries/Win -lXPLM_64
	LIBS += -L../SDK/Libraries/Win -lXPWidgets_64
	LIBS += -L../OpenAL/libs/Win64 -lOpenAL32
	LIBS += -L../GL_for_Windows/lib -lglu32 -lopengl32
	LIBS += -ldbghelp
}

win32:contains(CROSS_COMPILE, i686-w64-mingw32-) {
	QMAKE_CFLAGS += $$system("../pkg-config-deps win-32 --cflags")

	LIBS += $$system("../pkg-config-deps win-32 --libs")
	LIBS += -L../SDK/Libraries/Win -lXPLM
	LIBS += -L../SDK/Libraries/Win -lXPWidgets
	LIBS += -L../OpenAL/libs/Win32 -lOpenAL32
	LIBS += -L../GL_for_Windows/lib -lglu32 -lopengl32
	LIBS += -ldbghelp
}

linux-g++-64 {
	DEFINES += APL=0 IBM=0 LIN=1
	# The stack protector forces us to depend on libc,
	# but we'd prefer to be static.
	QMAKE_CFLAGS += -fno-stack-protector
	QMAKE_CFLAGS += $$system("../pkg-config-deps linux-64 --cflags")
	INCLUDEPATH += $$[LIBCLIPBOARD]/include
}

linux-g++-32 {
	DEFINES += APL=0 IBM=0 LIN=1
	QMAKE_CFLAGS += -fno-stack-protector
	QMAKE_CFLAGS += $$system("../pkg-config-deps linux-32 --cflags")
	INCLUDEPATH += $$[LIBCLIPBOARD]/include
}

macx {
	DEFINES += APL=1 IBM=0 LIN=0
	QMAKE_CFLAGS += -mmacosx-version-min=10.7
}

macx-clang {
	QMAKE_CFLAGS += $$system("../pkg-config-deps mac-64 --cflags")
}

macx-clang-32 {
	QMAKE_CFLAGS += $$system("../pkg-config-deps mac-32 --cflags")
}

HEADERS += ../src/*.h ../src/acfutils/*.h
SOURCES += ../src/*.c
