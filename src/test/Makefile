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

# Copyright 2023 Saso Kiselkov. All rights reserved.

CFLAGS := -g -O2 -W -Wall -Wextra -Werror -I.. -D_LACF_WITHOUT_XPLM -DDEBUG \
    -DIBM=0 -DLIN=1 -DAPL=0 -D_GNU_SOURCE -std=c11 \
    $(shell ../../pkg-config-deps linux-64 --cflags)
LDFLAGS := -L../../qmake/lin64 -lacfutils \
    $(shell ../../pkg-config-deps linux-64 --libs) \
    -lm -lpthread -lxcb
LIBACFUTILS := ../../qmake/lin64/libacfutils.a

all : dsfdump shpdump rwmutex

clean :
	rm -f dsfdump shpdump rwmutex

dsfdump : dsfdump.c $(LIBACFUTILS)
	$(CC) $(CFLAGS) -o dsfdump dsfdump.c $(LDFLAGS)

shpdump : shpdump.c $(LIBACFUTILS)
	$(CC) $(CFLAGS) -o shpdump shpdump.c $(LDFLAGS)

rwmutex : rwmutex.c $(LIBACFUTILS)
	$(CC) $(CFLAGS) -o rwmutex rwmutex.c $(LDFLAGS)
