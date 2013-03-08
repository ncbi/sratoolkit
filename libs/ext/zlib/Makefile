# ===========================================================================
#
#                            PUBLIC DOMAIN NOTICE
#               National Center for Biotechnology Information
#
#  This software/database is a "United States Government Work" under the
#  terms of the United States Copyright Act.  It was written as part of
#  the author's official duties as a United States Government employee and
#  thus cannot be copyrighted.  This software/database is freely available
#  to the public for use. The National Library of Medicine and the U.S.
#  Government have not placed any restriction on its use or reproduction.
#
#  Although all reasonable efforts have been taken to ensure the accuracy
#  and reliability of the software and data, the NLM and the U.S.
#  Government do not and cannot warrant the performance or results that
#  may be obtained by using this software or data. The NLM and the U.S.
#  Government disclaim all warranties, express or implied, including
#  warranties of performance, merchantability or fitness for any particular
#  purpose.
#
#  Please cite the author in any work or product based on this material.
#
# ===========================================================================


default: std

TOP ?= $(shell ../../../build/abspath.sh ../../..)
MODULE = libs/ext/zlib

include $(TOP)/build/Makefile.env

INT_LIBS = \

EXT_LIBS = \
	libz

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

#-------------------------------------------------------------------------------
# outer targets
#
std: makedirs download
	@ $(MAKE_CMD) $(TARGDIR)/$@

all: makedirs download
	@ $(MAKE_CMD) $(TARGDIR)/$@

$(INT_LIBS): makedirs download
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): makedirs download
	@ $(MAKE_CMD) $(LIBDIR)/$@

.PHONY: all std $(ALL_LIBS)

#-------------------------------------------------------------------------------
# all
#
$(TARGDIR)/all: $(LIBDIR)/libz

.PHONY: $(TARGDIR)/all

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: $(LIBDIR)/libz

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean
	@ rm -f libz.vers

.PHONY: clean

#-------------------------------------------------------------------------------
# libz
#
ifeq (win,$(OS))
$(LIBDIR)/libz: $(addprefix $(LIBDIR)/libz.,$(LIBEXT))
else
$(LIBDIR)/libz: $(addprefix $(LIBDIR)/libz.,$(LIBX))
endif

LIBZ_SRC = \
	adler32 \
	compress \
	crc32 \
	deflate \
	gzclose \
	gzlib \
	gzread \
	gzwrite \
	infback \
	inffast \
	inflate \
	inftrees \
	trees \
	uncompr \
	zutil


LIBZ_OBJ = \
	$(addsuffix .$(LOBX),$(LIBZ_SRC))

LIBZ_LIB = \

$(LIBDIR)/libz.$(SHLX): $(LIBZ_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(LIBZ_LIB)

$(LIBDIR)/libz.$(LIBX): $(LIBZ_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(LIBZ_LIB)


#-------------------------------------------------------------------------------
# libz download
#
ZLIB_VERS := \
	1.2.7

ZLIB_TAR = \
	zlib-$(ZLIB_VERS).tar

ZLIB_TAR_GZ = \
	$(ZLIB_TAR).gz

ifeq (win,$(OS))
ECHO_VERS = echo -ne $(ZLIB_VERS) "\n"
else
ECHO_VERS = echo $(ZLIB_VERS)
endif

ifeq (sun,$(OS))
	ZLIB_TAR_GET = $(ZLIB_TAR)
	TAR_CMD = tar -xf
else
	ZLIB_TAR_GET = $(ZLIB_TAR_GZ)
	TAR_CMD = tar --no-same-owner -x -z -f
endif

ifeq (mac,$(OS))
RETRIEVE = curl -O
else
RETRIEVE = wget
endif

$(ZLIB_TAR_GZ):
	@ echo downloading the sources
	$(RETRIEVE) http://zlib.net/$@
	@ touch $@

$(ZLIB_TAR): $(ZLIB_TAR_GZ)
	gzcat $^ > $@

libz.vers: $(ZLIB_TAR_GET)
	@ echo unpacking the sources
	$(TAR_CMD) $^
	mv zlib-$(ZLIB_VERS)/*.[ch] .
	mv zlib-$(ZLIB_VERS)/win32/zlib.def libz.def
	-rm -rf zlib-$(ZLIB_VERS) >/dev/null
	@ $(ECHO_VERS) > $@

download: libz.vers

.PHONY: download
