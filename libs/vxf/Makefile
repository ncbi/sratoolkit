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
MODULE = libs/vxf

MOD_LIBS = \
	libvxf \
	libwvxf

ALL_LIBS = \
	$(MOD_LIBS)

include $(TOP)/build/Makefile.env

#-------------------------------------------------------------------------------
# outer targets
#
all std: makedirs
	@ $(MAKE_CMD) $(TARGDIR)/std

$(MOD_LIBS): makedirs
	@ $(MAKE_CMD) $(TARGDIR)/$@

.PHONY: vers-includes all std $(ALL_LIBS)

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
# vdb transform library
#
$(TARGDIR)/libvxf: $(addprefix $(TARGDIR)/libvxf.,$(MODEXT))

$(TARGDIR)/libvxf.$(SHLX): $(MODDIR)/libvxf.$(SHLX)

VXF_SRC = \
	ceil \
	clip \
	cut \
	echo \
	exists \
	floor \
	paste \
	round \
	trunc \
	unzip \
	map \
	funzip \
	vec-sum \
	min \
	max \
	rldecode \
	pack \
	unpack \
	izip \
	iunzip \
	diff \
	sum \
	bit_or \
	trim \
	deriv \
	integral \
	delta \
	undelta \
	add-row-id \
	subtract-row-id \
	irzip \
	rand_4na_2na \
	outlier-encoder \
	outlier-decoder \
	bunzip \
	simple-sub-select \
	extract_token \
	strtonum \
	sprintf \
	wgs-tokenize-accession \
	lower-case-tech-reads

VXF_OBJ = \
	$(addsuffix .$(LOBX),$(VXF_SRC))

VXF_LIB = \
	-dvdb \
	-dklib \
	-dm \
	-dz \
	-dbz2

$(MODDIR)/libvxf.$(SHLX): $(VXF_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(VXF_LIB)

$(TARGDIR)/libvxf.$(LIBX): $(ILIBDIR)/libvxf.$(LIBX)

$(ILIBDIR)/libvxf.$(LIBX): $(VXF_OBJ)
	$(LD) --slib -o $@ $^ $(VXF_LIB)

#-------------------------------------------------------------------------------
# wvdb transform library
#

$(TARGDIR)/libwvxf: $(addprefix $(TARGDIR)/libwvxf.,$(MODEXT))

$(TARGDIR)/libwvxf.$(SHLX): $(WMODDIR)/libwvxf.$(SHLX)

WVXF_SRC = \
	$(VXF_SRC) \
	zip \
	bzip \
	fzip \
	rlencode \
	checksum

WVXF_OBJ = \
	$(addsuffix .$(LOBX),$(WVXF_SRC))

# temporary
WVXF_LIB = \
	-dwvdb \
	-dwkdb \
	-dklib \
	-dm \
	-dz \
	-dbz2

$(WMODDIR)/libwvxf.$(SHLX): $(WVXF_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(WVXF_LIB)

$(TARGDIR)/libwvxf.$(LIBX): $(ILIBDIR)/libwvxf.$(LIBX)

$(ILIBDIR)/libwvxf.$(LIBX): $(WVXF_OBJ)
	$(LD) --slib -o $@ $^ $(WVXF_LIB)

libwvxf.vers.h:
	@ true

.PHONY: $(TARGDIR)/libwvxf $(TARGDIR)/libwvxf.$(SHLX) $(TARGDIR)/libwvxf.$(LIBX)
