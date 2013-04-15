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
MODULE = libs/ext/curl

include $(TOP)/build/Makefile.env

INT_LIBS = \

EXT_LIBS = \
	libcurl

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

#-------------------------------------------------------------------------------
# outer targets
#
ifneq (rwin,$(BUILD_OS))

all std clean $(ALL_LIBS):
	@true

.PHONY: all std clean $(ALL_LIBS)

else

std: makedirs 
	@ $(MAKE_CMD) $(TARGDIR)/$@

all: makedirs 
	@ $(MAKE_CMD) $(TARGDIR)/$@

$(INT_LIBS): makedirs 
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): makedirs 
	@ $(MAKE_CMD) $(LIBDIR)/$@

.PHONY: all std $(ALL_LIBS)

#-------------------------------------------------------------------------------
# all
#
$(TARGDIR)/all: $(BINDIR)/libcurl.$(DLLX)

.PHONY: $(TARGDIR)/all

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: $(BINDIR)/libcurl.$(DLLX)

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean
	rm -f $(BINDIR)/libcurl.$(DLLX)
    
.PHONY: clean

#-------------------------------------------------------------------------------
# libcurl
#
LIBCURL_VERS := \
	7.29.0

ifeq (dbg,$(BUILD))
    DBG=-dbg
endif
$(BINDIR)/libcurl.$(DLLX): /panfs/pan1/sra-test/TOOLKIT/libcurl/$(LIBCURL_VERS)/win$(BITS)$(DBG)/libcurl.$(DLLX)
	cp $^ $@

endif
    
