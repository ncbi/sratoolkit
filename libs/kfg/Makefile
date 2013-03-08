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
MODULE = libs/kfg

INT_LIBS = \

EXT_LIBS = \
	libkfg

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

TEST_TOOLS = \
	wb-test-kfg

include $(TOP)/build/Makefile.env

#-------------------------------------------------------------------------------
# outer targets
#
all std: makedirs
	@ $(MAKE_CMD) $(TARGDIR)/std

$(INT_LIBS): makedirs
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): makedirs
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
# kfg
#
$(LIBDIR)/libkfg: $(NCBIDIR)/config.kfg $(addprefix $(LIBDIR)/libkfg.,$(LIBEXT))

$(NCBIDIR)/config.kfg: $(SRCDIR)/config.$(OS).kfg
	sed -e "s|\$$"'(TOP)|'$(TOP)'|g' -e 's|/cygdrive||g' $^ > $@

KFG_SRC = \
	config-grammar \
	config-lex \
	config \
	repository \
	report 

KFG_OBJ = \
	$(addsuffix .$(LOBX),$(KFG_SRC))

KFG_LIB = \
	-dkfs \
	-dklib

$(LIBDIR)/libkfg.$(SHLX): $(KFG_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(KFG_LIB)

$(LIBDIR)/libkfg.$(LIBX): $(KFG_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(KFG_LIB)

libkfg_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libkfg $(KFG_OBJ)

#-------------------------------------------------------------------------------
# white-box test
#
TEST_SRC = \
	wb-test-kfg

TEST_OBJ = \
	$(addsuffix .$(OBJX),$(TEST_SRC))

TEST_LIB = \
	-skapp \
	-skfg \
	-svdb \
	-svfs \
	-skdb \
	-skfs \
	-skrypto \
	-skproc \
	-sktst \
	-sklib 

$(TEST_BINDIR)/wb-test-kfg: $(TEST_OBJ)
	$(LP) --exe -o $@ $^ $(TEST_LIB) 
