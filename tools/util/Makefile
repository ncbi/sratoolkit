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
MODULE = tools/util

include $(TOP)/build/Makefile.env

INT_TOOLS = \

EXT_TOOLS = \
	rcexplain \
	kdbmeta \
	vdb-lock \
	vdb-unlock \
	test-sra \
	vdb-passwd

ALL_TOOLS = \
	$(INT_TOOLS) \
	$(EXT_TOOLS)

#-------------------------------------------------------------------------------
# outer targets
#
all std: vers-includes
	@ $(MAKE_CMD) $(TARGDIR)/$@

$(ALL_TOOLS): vers-includes
	@ $(MAKE_CMD) $(BINDIR)/$@

.PHONY: all std $(ALL_TOOLS)

#-------------------------------------------------------------------------------
# all
#
$(TARGDIR)/all: \
        $(addprefix $(BINDIR)/,$(ALL_TOOLS))

.PHONY: $(TARGDIR)/all

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: \
	$(addprefix $(BINDIR)/,$(EXT_TOOLS))

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# vers-includes
#
$(TARGDIR)/vers-includes: $(addsuffix .vers.h,$(EXT_TOOLS))

.PHONY: $(TARGDIR)/vers-includes

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean

.PHONY: clean

#-------------------------------------------------------------------------------
# rcexplain - explain a return code observed in debugger
#
RCEXPLAIN_SRC = \
	rcexplain

RCEXPLAIN_OBJ = \
	$(addsuffix .$(OBJX),$(RCEXPLAIN_SRC))

RCEXPLAIN_LIB = \
	-skapp \
	-lncbi-vdb \

$(BINDIR)/rcexplain: $(RCEXPLAIN_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(RCEXPLAIN_LIB)


#-------------------------------------------------------------------------------
# kdbmeta - query and update metadata files
#
KDBMETA_SRC = \
	kdbmeta

KDBMETA_OBJ = \
	$(addsuffix .$(OBJX),$(KDBMETA_SRC))

KDBMETA_LIB = \
	-skapp \
	-lncbi-wvdb \
	-lxml2 \
	-lm

$(BINDIR)/kdbmeta: $(KDBMETA_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(KDBMETA_LIB)


#-------------------------------------------------------------------------------
# vdb-lock
#  lock a vdb object
#
VDB_LOCK_SRC = \
	vdb-lock

VDB_LOCK_OBJ = \
	$(addsuffix .$(OBJX),$(VDB_LOCK_SRC))

VDB_LOCK_LIB = \
	-skapp \
	-lncbi-wvdb \
	-lxml2 \
	-lm

$(BINDIR)/vdb-lock: $(VDB_LOCK_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(VDB_LOCK_LIB)


#-------------------------------------------------------------------------------
# vdb-unlock
#  unlock a vdb object
#
VDB_UNLOCK_SRC = \
	vdb-unlock

VDB_UNLOCK_OBJ = \
	$(addsuffix .$(OBJX),$(VDB_UNLOCK_SRC))

VDB_UNLOCK_LIB = \
	-skapp \
	-lncbi-wvdb \
	-lxml2 \
	-lm

$(BINDIR)/vdb-unlock: $(VDB_UNLOCK_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(VDB_UNLOCK_LIB)

#-------------------------------------------------------------------------------
# test-sra: print config, resolve sra, check dependencies
#

T_SRC = \
	PrintOS \
	test-sra \

T_OBJ = \
	$(addsuffix .$(OBJX),$(T_SRC))

T_LIB = \
	-lkapp \
	-lncbi-vdb \
	-lxml2 \
	-lm

$(BINDIR)/test-sra: $(T_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(T_LIB)

#----------------------------------------------------------------
# vdb-passwd
#
VDB_PASSWD_SRC = \
	vdb-passwd \
	syspass

VDB_PASSWD_OBJ = \
	$(addsuffix .$(OBJX),$(VDB_PASSWD_SRC))

VDB_PASSWD_LIB = \
	-skapp \
	-lncbi-vdb \


$(BINDIR)/vdb-passwd: $(VDB_PASSWD_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(VDB_PASSWD_LIB)
