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

default: all

TOP ?= $(shell ../../build/abspath.sh ../..)
MODULE = libs/kfs

INT_LIBS = \
	libkfs

ALL_LIBS = \
	$(INT_LIBS)

include $(TOP)/build/Makefile.env

#-------------------------------------------------------------------------------
# outer targets
#
all std: makedirs
	@ $(MAKE_CMD) $(TARGDIR)/std

$(INT_LIBS): makedirs
	@ $(MAKE_CMD) $(ILIBDIR)/$@

.PHONY: all std $(ALL_LIBS)

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: \
	$(addprefix $(ILIBDIR)/,$(INT_LIBS))

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean

.PHONY: clean

#-------------------------------------------------------------------------------
# kfs
#
$(ILIBDIR)/libkfs: $(addprefix $(ILIBDIR)/libkfs.,$(ILIBEXT))

$(ILIBDIR)/libkfs-nommap: $(addprefix $(ILIBDIR)/libkfs-nommap.,$(LIBX))

KFS_CMN = \
	directory \
	arrayfile \
	file \
	sysdir \
	sysfile \
	sysmmap \
	syslockfile \
	sysdll \
	gzip \
	bzip \
	md5 \
	crc32 \
	arc \
	toc \
	tocentry \
	tocdir \
	tocfile \
	sra \
	tar \
	teefile \
	buffile \
	buffile-read \
	buffile-write \
	subfile \
	nullfile \
	countfile \
	dir_test \
	pagefile \
	pmem \
	readheadfile \
	ramfile \
	quickmount \
	report \
	lockfile \
	syslockfile \
	cacheteefile

KFS_SRC = \
	mmap \
	$(KFS_CMN)


KFS_OBJ = \
	$(addsuffix .$(LOBX),$(KFS_SRC))

KFS_LIB = \
	-dkproc \
	-dklib \
	-ddl \
	-dz \
	-dbz2

$(ILIBDIR)/libkfs.$(LIBX): $(KFS_OBJ)
	$(LD) --slib -o $@ $^ $(KFS_LIB)


#-------------------------------------------------------------------------------
# kff
#
ifeq (linux,$(OS))
$(ILIBDIR)/libkff: $(addprefix $(ILIBDIR)/libkff.,$(ILIBEXT))
else
$(ILIBDIR)/libkff:
	@ echo "not building libkff on "$(OS)
endif

KFF_SRC = \
	fileformat \
	ffext \
	ffkey \
	ffmagic

KFF_OBJ = \
	$(addsuffix .$(LOBX),$(KFF_SRC))

KFF_LIB = \
	-dkfg \
	-dklib

ifneq (win,$(OS))
	KFF_LIB += -lmagic
endif

$(ILIBDIR)/libkff.$(LIBX): $(KFF_OBJ)
	$(LD) --slib -o $@ $^ $(KFF_LIB)
