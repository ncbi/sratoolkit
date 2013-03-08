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
MODULE = libs/kproc

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

include $(TOP)/build/Makefile.env

EXT_LIBS = \
	libksproc \
	libkproc

ifneq (win,$(OS))
EXT_LIBS += \
	libkq
endif

#-------------------------------------------------------------------------------
# outer targets
#
all std: makedirs
	@ $(MAKE_CMD) $(TARGDIR)/std

$(INT_LIBS): makedirs
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): makedirs
	@ $(MAKE_CMD) $(LIBDIR)/$@

.PHONY: all std $(ALL_LIBS)

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

.PHONY: clean

#-------------------------------------------------------------------------------
# tag
#
tag: \
	$(addsuffix _tag,$(ALL_LIBS))

.PHONY: tag $(addsuffix _tag,$(ALL_LIBS))

#-------------------------------------------------------------------------------
# multi-threaded process support
#
$(LIBDIR)/libkproc: $(addprefix $(LIBDIR)/libkproc.,$(LIBEXT))

PROC_CMN = \
	task \
	procmgr

PROC_SRC = \
	$(PROC_CMN)

ifneq (win,$(OS))
PROC_SRC += \
	systimeout \
	syslock \
	systhread \
	syscond \
	sem
else
PROC_SRC += \
	systimeout \
	syslock \
	systhread \
	syscond
endif

PROC_OBJ = \
	$(addsuffix .$(LOBX),$(PROC_SRC))

ifneq (win,$(OS))
PROC_LIB = \
	-dpthread
else
PROC_LIB = \
	-dklib
endif

$(LIBDIR)/libkproc.$(SHLX): $(PROC_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(PROC_LIB)

$(LIBDIR)/libkproc.$(LIBX): $(PROC_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(PROC_LIB)

libkproc_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libkproc $(PROC_OBJ)


#-------------------------------------------------------------------------------
# single-threaded process support
#
$(LIBDIR)/libksproc: $(addprefix $(LIBDIR)/libksproc.,$(LIBEXT))

SPROC_SRC = \
	$(PROC_CMN) \
	sttimeout \
	stlock \
	stcond \
	stsem \
	stthread \
	stbarrier

SPROC_OBJ = \
	$(addsuffix .$(LOBX),$(SPROC_SRC))

SPROC_LIB = \
	-dklib

$(LIBDIR)/libksproc.$(SHLX): $(SPROC_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(SPROC_LIB)

$(LIBDIR)/libksproc.$(LIBX): $(SPROC_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(SPROC_LIB)

libksproc_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libksproc $(SPROC_OBJ)


#-------------------------------------------------------------------------------
# cross-thread reference queue
#
$(LIBDIR)/libkq: $(addprefix $(LIBDIR)/libkq.,$(LIBEXT))

Q_SRC = \
	queue

Q_OBJ = \
	$(addsuffix .$(LOBX),$(Q_SRC))

Q_LIB = \
	-dklib \
	-dkproc

$(LIBDIR)/libkq.$(SHLX): $(Q_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(Q_LIB)

$(LIBDIR)/libkq.$(LIBX): $(Q_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(Q_LIB)

libkq_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libkq $(Q_OBJ)
