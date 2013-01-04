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

TOP ?= $(shell ../../build/abspath.sh ../..)
MODULE = libs/kdb

INT_LIBS = \

EXT_LIBS = \
	libkdb \
	libwkdb

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

include $(TOP)/build/Makefile.env

#-------------------------------------------------------------------------------
# outer targets
#
all std: vers-includes
	@ $(MAKE_CMD) $(TARGDIR)/std

$(INT_LIBS): vers-includes
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): vers-includes
	@ $(MAKE_CMD) $(LIBDIR)/$@

.PHONY: all std $(ALL_LIBS) $(ALL_DEFS)


#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: \
	$(addprefix $(ILIBDIR)/,$(INT_LIBS)) \
	$(addprefix $(LIBDIR)/,$(EXT_LIBS))

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# vers-includes
#
$(TARGDIR)/vers-includes: $(addsuffix .vers.h,$(EXT_LIBS))

.PHONY: $(TARGDIR)/vers-includes

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean

.PHONY: clean

#-------------------------------------------------------------------------------
# tag
#
tag: \
	$(addsuffix _tag,$(ALL_LIBS))

.PHONY: tag $(addsuffix _tag,$(ALL_LIBS))

#-------------------------------------------------------------------------------
# kdb
#
$(LIBDIR)/libkdb: $(addprefix $(LIBDIR)/libkdb.,$(LIBEXT))

KDB_CMN = \
	btree \
	dbmgr-cmn \
#	database-cmn

KDB_SRC = \
	$(KDB_CMN) \
	kdb \
	dbmgr \
	dbcc-cmn \
	database \
	database-cc \
	table \
	table-cc \
	column \
	column-cc \
	coldata \
	colidx \
	colidx0 \
	colidx1 \
	colidx2 \
	idxblk \
	meta \
	index \
	idstats \
	trieidx-v1 \
	trieval-v1 \
	ptrieval-v1 \
	trieidx-v2 \
	trieval-v2 \
	ptrieval-v2 \
	u64idx-v3

KDB_OBJ = \
	$(addsuffix .$(LOBX),$(KDB_SRC))

KDB_LIB = \
	-dvfs \
	-dkfs \
	-dkrypto \
	-dklib

$(LIBDIR)/libkdb.$(SHLX): $(KDB_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(KDB_LIB)

$(LIBDIR)/libkdb.$(LIBX): $(KDB_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(KDB_LIB)

libkdb_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libkdb $(KDB_OBJ)


#-------------------------------------------------------------------------------
# wkdb
#
$(LIBDIR)/libwkdb: $(addprefix $(LIBDIR)/libwkdb.,$(LIBEXT))

WKDB_SRC = \
	$(KDB_CMN) \
	wkdb \
	wdbmgr \
	wdatabase \
	wtable \
	wcolumn \
	wcoldata \
	wcolidx \
	wcolidx0 \
	wcolidx1 \
	wcolidx2 \
	idxblk \
	widxblk \
	wmeta \
	windex \
	wtrieidx-v1 \
	wtrieidx-v2 \
	wu64idx-v3

WKDB_OBJ = \
	$(addsuffix .$(LOBX),$(WKDB_SRC))

WKDB_LIB = \
	-dvfs \
	-dkfs \
	-dkrypto \
	-dklib

$(LIBDIR)/libwkdb.$(SHLX): $(WKDB_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(WKDB_LIB)

$(LIBDIR)/libwkdb.$(LIBX): $(WKDB_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(WKDB_LIB)

libwkdb_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libwkdb $(WKDB_OBJ)
