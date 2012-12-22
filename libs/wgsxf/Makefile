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
MODULE = libs/wgsxf

MOD_LIBS = \
	libwgsxf \
	libwwgsxf

TEST_TOOLS = \

include $(TOP)/build/Makefile.env

RWORKDIR = $(BINDIR)

ALL_LIBS = \
	$(MOD_LIBS)

#-------------------------------------------------------------------------------
# outer targets
#
all std: makedirs
	@ $(MAKE_CMD) $(TARGDIR)/std

$(MOD_LIBS): makedirs
	@ $(MAKE_CMD) $(TARGDIR)/$@

$(TEST_TOOLS): makedirs
	@ $(MAKE_CMD) $(TEST_BINDIR)/$@

.PHONY: all std $(ALL_LIBS) $(TEST_TOOLS)

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: \
	$(addprefix $(TARGDIR)/,$(MOD_LIBS))

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean

.PHONY: clean

#-------------------------------------------------------------------------------
# wgs-vdb transform library
#
$(TARGDIR)/libwgsxf: $(addprefix $(TARGDIR)/libwgsxf.,$(MODEXT))

$(TARGDIR)/libwgsxf.$(SHLX): $(MODDIR)/libwgsxf.$(SHLX)

WGSXF_SRC = \
	build_scaffold \
	build_read_type

WGSXF_OBJ = \
	$(addsuffix .$(LOBX),$(WGSXF_SRC))

WGSXF_LIB = \
	-dvdb \
	-dklib \
	-dm \
	-dz \
	-dbz2

$(MODDIR)/libwgsxf.$(SHLX): $(WGSXF_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(WGSXF_LIB)

$(TARGDIR)/libwgsxf.$(LIBX): $(ILIBDIR)/libwgsxf.$(LIBX)

$(ILIBDIR)/libwgsxf.$(LIBX): $(WGSXF_OBJ)
	$(LD) --slib -o $@ $^ $(WGSXF_LIB)

.PHONY: $(TARGDIR)/libwgsxf $(TARGDIR)/libwgsxf.$(SHLX) $(TARGDIR)/libwgsxf.$(LIBX)

#-------------------------------------------------------------------------------
# update-side wgs transform library
#

$(TARGDIR)/libwwgsxf: $(addprefix $(TARGDIR)/libwwgsxf.,$(MODEXT))

$(TARGDIR)/libwwgsxf.$(SHLX): $(WMODDIR)/libwwgsxf.$(SHLX)

WWGSXF_SRC = \
	$(WGSXF_SRC)

WWGSXF_OBJ = \
	$(addsuffix .$(LOBX),$(WWGSXF_SRC))

# temporary
WWGSXF_LIB = \
	-dwvdb \
	-dwkdb \
	-dklib \
	-dm \
	-dz \
	-dbz2

$(WMODDIR)/libwwgsxf.$(SHLX): $(WWGSXF_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(WWGSXF_LIB)

$(TARGDIR)/libwwgsxf.$(LIBX): $(ILIBDIR)/libwwgsxf.$(LIBX)

$(ILIBDIR)/libwwgsxf.$(LIBX): $(WWGSXF_OBJ)
	$(LD) --slib -o $@ $^ $(WWGSXF_LIB)

libwwgsxf.vers.h:
	@ true

.PHONY: $(TARGDIR)/libwwgsxf $(TARGDIR)/libwwgsxf.$(SHLX) $(TARGDIR)/libwwgsxf.$(LIBX)
