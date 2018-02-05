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

# Copyright 2017 Saso Kiselkov. All rights reserved.

all : dsfdump shpdump

clean :
	rm -f dsfdump shpdump

dsfdump : dsfdump.c ../../qmake/lin64/libacfutils.a
	$(CC) -g -W -Wall -Werror -o dsfdump dsfdump.c \
	    -I.. -L../../qmake/lin64 -lacfutils \
	    $(shell ../../pkg-config-deps linux-64 --libs) -lm -lpthread -lxcb

shpdump : shpdump.c ../../qmake/lin64/libacfutils.a
	$(CC) -g -W -Wall -Werror -o shpdump shpdump.c \
	    -I.. -L../../qmake/lin64 -lacfutils \
	    $(shell ../../pkg-config-deps linux-64 --cflags --libs) \
	    -lm -lpthread -lxcb
