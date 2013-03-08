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
MODULE = libs/vfs

INT_LIBS = \

EXT_LIBS = \
	libsrapath \
	libvfs

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

TEST_TOOLS = \
	wb-test-srapath

include $(TOP)/build/Makefile.env

RWORKDIR=$(TOP)/$(MODULE)

#-------------------------------------------------------------------------------
# outer targets
#
all std: vers-includes
	@ $(MAKE_CMD) $(TARGDIR)/std

$(INT_LIBS): vers-includes
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): vers-includes
	@ $(MAKE_CMD) $(LIBDIR)/$@

$(TEST_TOOLS): makedirs
	@ $(MAKE_CMD) $(TEST_BINDIR)/$@

.PHONY: all std $(ALL_LIBS) $(TEST_TOOLS)

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
	@ rm -f $(addsuffix *,$(addprefix $(TEST_BINDIR)/,$(TEST_TOOLS)))

.PHONY: clean

#-------------------------------------------------------------------------------
# tag
#
tag: \
	$(addsuffix _tag,$(ALL_LIBS))

.PHONY: tag $(addsuffix _tag,$(ALL_LIBS))

#-------------------------------------------------------------------------------
# srapath library
#
$(LIBDIR)/libsrapath: $(LIBDIR)/libsrapath.$(LIBX)

SRAPATH_SRC = \
	srapath-stub \
	srapath

SRAPATH_OBJ = \
	$(addsuffix .$(LOBX),$(SRAPATH_SRC))

$(LIBDIR)/libsrapath.$(LIBX): $(SRAPATH_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^

#-------------------------------------------------------------------------------
# vfs
#
$(LIBDIR)/libvfs: $(addprefix $(LIBDIR)/libvfs.,$(LIBEXT))

VFS_SRC = \
	path \
	syspath \
	manager  \
	resolver

VFS_OBJ = \
	$(addsuffix .$(LOBX),$(VFS_SRC))

VFS_LIB = \
	-skurl \
	-dkrypto \
	-dkfg \
	-dkfs \
	-dklib

$(LIBDIR)/libvfs.$(SHLX): $(VFS_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ -ssrapath $(VFS_LIB)

$(LIBDIR)/libvfs.$(LIBX): $(VFS_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(VFS_LIB)

libvfs_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libvfs $(VFS_OBJ)

#-------------------------------------------------------------------------------
# white-box test
#
TEST_SRC = \
	$(VFS_SRC) \
	srapath-stub \
	wb-test-srapath

TEST_OBJ = \
	$(addsuffix .$(OBJX),$(TEST_SRC)) 

TEST_LIB = \
    -skapp \
    -svdb \
    -skdb \
	-skfs \
	-skurl \
	-skrypto \
	-skfg \
	-skproc \
	-sktst \
	-sklib 

$(TEST_BINDIR)/wb-test-srapath: $(TEST_OBJ)
	$(LP) --exe -o $@ $^ $(TEST_LIB) 


