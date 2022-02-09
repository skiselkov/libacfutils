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

CONFIG += warn_on plugin release
CONFIG -= thread exceptions qt rtti debug

QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64

VERSION = 1.0.0

INCLUDEPATH += ../C

DEFINES += _7ZIP_ST

QMAKE_DEL_FILE = rm -f

TARGET = lzma

win32 {
#	DEFINES += _WIN32_WINNT=0x0600
}

HEADERS += $$files(../C/*.h)
SOURCES += $$files(../C/*.c)

# Remove the multi-threaded versions, which depend on Win32 threads
HEADERS -= \
    ../C/DllSecur.h \
    ../C/LzFindMt.h \
    ../C/MtCoder.h \
    ../C/Threads.h

SOURCES -= \
    ../C/DllSecur.c \
    ../C/LzFindMt.c \
    ../C/MtCoder.c \
    ../C/Threads.c \
