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
MODULE = libs/kapp

include $(TOP)/build/Makefile.env

INT_LIBS = \
	libkapp

EXT_LIBS = \
    libload

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

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
# OS specific application support
#
$(ILIBDIR)/libkapp: $(addprefix $(ILIBDIR)/libkapp.,$(LIBX))

KAPP_SRC = \
	main \
	args \
	tokenizer \
	sysmain

KAPP_OBJ = \
	$(addsuffix .$(OBJX),$(KAPP_SRC))

KAPP_LIB = \
	-dvfs \
	-dklib

$(ILIBDIR)/libkapp.$(LIBX): $(KAPP_OBJ)
	$(LD) --slib -o $@ $^ $(KAPP_LIB)

libkapp_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libkapp $(KAPP_OBJ)


#-------------------------------------------------------------------------------
# loaders common libarary
#
$(LIBDIR)/libload: $(addprefix $(LIBDIR)/libload.,$(LIBX))

LOADLIB_SRC = \
	progressbar \
	loader-file \
	loader-meta \
	log-xml

ifneq (win,$(OS))
	LOADLIB_SRC += queue-file
endif

LOADLIB_OBJ = \
	$(addsuffix .$(LOBX),$(LOADLIB_SRC))

LOADLIB_LIB = \
	-dkfs \
	-dklib

ifneq (win,$(OS))
	LOADLIB_LIB += -dkq
endif

$(LIBDIR)/libload.$(LIBX): $(LOADLIB_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(LOADLIB_LIB)

libload_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libload $(LOADLIB_OBJ)
