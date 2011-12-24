# Copyright (C) 2004-2008 Peter Urbanec
# $Id: Makefile,v 1.8 2008/04/10 05:44:21 purbanec Exp $

ifdef CROSS
ifeq ($(CROSS),gearbox)

ARCH=mipsel-linux-uclibc
STAGING= /opt/toolchains/buildroot/build_mipsel/staging_dir
CROSS_HOME=${STAGING}
CROSS_PATH=${CROSS_HOME}/bin
LDFLAGS=-L${STAGING}/lib -Wl,-rpath-link,${STAGING}/lib -Wl,-rpath,/usr/lib -Wl,-O2
LFLAGS=${LDFLAGS}
CFLAGS+=-I${STAGING}/include

else # CROSS == "linksys"

ARCH=armv5b-softfloat-linux
CROSS_HOME=/home/slug/sourceforge/unslung/toolchain/${ARCH}/gcc-3.3.5-glibc-2.2.5
CROSS_PATH=${CROSS_HOME}/bin
LDFLAGS=-L${CROSS_HOME}/lib -Wl,-rpath-link,${CROSS_HOME}/lib -Wl,-rpath,/usr/lib -Wl,-O2

endif

AR=${CROSS_PATH}/${ARCH}-ar
CPP=${CROSS_PATH}/${ARCH}-gcc -E
CC=${CROSS_PATH}/${ARCH}-gcc
CXX=${CROSS_PATH}/${ARCH}-g++
LD=${CROSS_PATH}/${ARCH}-ld
CCLD=${CROSS_PATH}/${ARCH}-gcc
STRIP=${CROSS_PATH}/${ARCH}-strip
RANLIB=${CROSS_PATH}/${ARCH}-ranlib

else

LDFLAGS+=-Wl,-O2

endif

CFLAGS+=-std=gnu99 -Wall -W -Wshadow -Wstrict-prototypes -pedantic -fexpensive-optimizations -fomit-frame-pointer -frename-registers -O2

puppy: puppy.o crc16.o mjd.o tf_bytes.o usb_io.o

strip: puppy
	${STRIP} puppy

clean:
	-rm -f *.o
	-rm -f *~
	-rm -f puppy

install: puppy
	@echo "\npuppy does not require installation.\nJust copy the file 'puppy' to wherever you like!"


crc16.o: crc16.c crc16.h
mjd.o: mjd.c mjd.h tf_bytes.h
puppy.o: puppy.c usb_io.h mjd.h tf_bytes.h
tf_bytes.o: tf_bytes.c tf_bytes.h
usb_io.o: usb_io.c usb_io.h mjd.h tf_bytes.h crc16.h

