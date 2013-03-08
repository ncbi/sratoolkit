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
MODULE = libs/sra

INT_LIBS = \

EXT_LIBS = \
	libsraschema \
	libwsraschema \
	libsradb \
	libwsradb \
	libsrareader

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

include $(TOP)/build/Makefile.env

VPATH += $(OBJDIR)

#-------------------------------------------------------------------------------
# outer targets
#
all std: vers-includes
	@ $(MAKE_CMD) $(TARGDIR)/std

$(INT_LIBS): vers-includes
	@ $(MAKE_CMD) $(ILIBDIR)/$@

$(EXT_LIBS): vers-includes
	@ $(MAKE_CMD) $(LIBDIR)/$@

.PHONY: all std $(ALL_LIBS)

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: \
	$(addprefix $(ILIBDIR)/,$(INT_LIBS)) \
	$(addprefix $(LIBDIR)/,$(EXT_LIBS))

.PHONY: $(TARGDIR)/all $(TARGDIR)/std

#-------------------------------------------------------------------------------
# vers-includes
#
$(TARGDIR)/vers-includes: $(addsuffix .vers.h,$(EXT_LIBS))

.PHONY: $(TARGDIR)/vers-includes

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean
	@ rm -f $(LIBDIR)/libsra-schema.* $(LIBDIR)/libwsra-schema.* $(LIBDIR)/sra-dflt-schema*
	@ rm -f $(BINDIR)/libsra-schema.* $(BINDIR)/libwsra-schema.*

.PHONY: clean

#-------------------------------------------------------------------------------
# tag
#
tag: \
	$(addsuffix _tag,$(ALL_LIBS))

.PHONY: tag $(addsuffix _tag,$(ALL_LIBS))


#-------------------------------------------------------------------------------
# sradb embedded schema
#
$(LIBDIR)/libsraschema: $(LIBDIR)/libsraschema.$(LIBX)

$(LIBDIR)/libwsraschema: $(LIBDIR)/libwsraschema.$(LIBX)

SRA_DFLT_SCHEMA_LIBS = \
	-skapp \
	-svdb \
	-skdb \
	-svfs \
	-skrypto \
	-skfg \
	-skfs \
	-sklib \
	-sksproc

SRA_DFLT_SCHEMA_OS_LIBS = \
	-ldl \
	-sz \
	-sbz2 \
	-lm

# set workdir for RUN_REMOTELY in rwin mode 
RWORKDIR=$(OBJDIR)

$(LIBDIR)/sra-dflt-schema: sra-dflt-schema.$(OBJX)
	$(LD) --exe -o $@ $^ $(SRA_DFLT_SCHEMA_LIBS) $(SRA_DFLT_SCHEMA_OS_LIBS)

sra-schema.c: $(LIBDIR)/sra-dflt-schema $(SRCDIR)/built-in.vschema $(TOP)/build/sra-dflt-schema.sh
	@ $(TOP)/build/sra-dflt-schema.sh $(BUILD_OS) "$(RUN_REMOTELY) $(LIBDIR)/sra-dflt-schema" -o$@ -I$(TOP)/interfaces -T$(OBJDIR)/$@.d $(SRCDIR)/built-in.vschema

SRA_SCHEMA_SRC = \
	sraschema \
	sra-schema

SRA_SCHEMA_OBJ = \
	$(addsuffix .$(LOBX),$(SRA_SCHEMA_SRC))

WSRA_SCHEMA_OBJ = \
	$(addsuffix .$(LOBX),$(SRA_SCHEMA_SRC))

SRA_SCHEMA_LIB = \
	-dvdb \
	-dklib

WSRA_SCHEMA_LIB = \
	-dwvdb \
	-dklib

$(LIBDIR)/libsra-schema.$(SHLX): $(SRA_SCHEMA_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(SRA_SCHEMA_LIB)

$(LIBDIR)/libwsra-schema.$(SHLX): $(WSRA_SCHEMA_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(WSRA_SCHEMA_LIB)

SRASCHEMA_SRC = \
	sraschema-stub

WSRASCHEMA_SRC = \
	wsraschema-stub

SRASCHEMA_SRC += $(SRA_SCHEMA_SRC)
WSRASCHEMA_SRC += $(SRA_SCHEMA_SRC)

SRASCHEMA_OBJ = \
	$(addsuffix .$(LOBX),$(SRASCHEMA_SRC))

WSRASCHEMA_OBJ = \
	$(addsuffix .$(LOBX),$(WSRASCHEMA_SRC))

$(LIBDIR)/libsraschema.$(LIBX): $(SRASCHEMA_OBJ) $(SRASCHEMA_DEPS)
	$(LD) --slib -o $@ $(SRASCHEMA_OBJ) $(SRA_SCHEMA_LIB)

$(LIBDIR)/libwsraschema.$(LIBX): $(WSRASCHEMA_OBJ) $(WSRASCHEMA_DEPS)
	$(LD) --slib -o $@ $(WSRASCHEMA_OBJ) $(WSRA_SCHEMA_LIB)

libsraschema_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libsraschema $(SRASCHEMA_OBJ)


#-------------------------------------------------------------------------------
# sradb library
#
$(LIBDIR)/libsradb: $(addprefix $(LIBDIR)/libsradb.,$(LIBEXT))

SRADBCMN_SRC = \
	sramgr-cmn \
	sratbl-cmn \
	sracol \
	types

SRADB_SRC = \
	$(SRADBCMN_SRC) \
	sramgr \
	sratbl

SRADB_OBJ = \
	$(addsuffix .$(LOBX),$(SRADB_SRC))

SRADB_LIB = \
	-ssraschema \
	-dvdb \
	-dkdb \
	-dvfs \
	-lkfg \
	-dkfs \
	-dklib

$(LIBDIR)/libsradb.$(SHLX): $(SRADB_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ -ssrapath $(SRADB_LIB)

$(LIBDIR)/libsradb.$(LIBX): $(SRADB_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(SRADB_LIB)

libsradb_tag: $(SRADB_TAGS)
	@ $(TOP)/build/tag-module.sh $(MODULE) libsradb $(SRADB_OBJ)


#-------------------------------------------------------------------------------
# update version
#
$(LIBDIR)/libwsradb: $(addprefix $(LIBDIR)/libwsradb.,$(LIBEXT))

WSRADB_SRC = \
	$(SRADBCMN_SRC) \
	wsramgr \
	wsratbl

WSRADB_OBJ = \
	$(addsuffix .$(LOBX),$(WSRADB_SRC))

WSRADB_LIB = \
	-ssraschema \
	-dwvdb \
	-dwkdb \
	-dvfs \
	-lkfg \
	-dkfs \
	-dklib

$(LIBDIR)/libwsradb.$(SHLX): $(WSRADB_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ -ssrapath $(WSRADB_LIB)

$(LIBDIR)/libwsradb.$(LIBX): $(WSRADB_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(WSRADB_LIB)

libwsradb_tag: $(WSRADB_TAGS)
	@ $(TOP)/build/tag-module.sh $(MODULE) libwsradb $(WSRADB_OBJ)


#-------------------------------------------------------------------------------
# libsrareader
#
$(LIBDIR)/libsrareader: $(addprefix $(LIBDIR)/libsrareader.,$(LIBEXT))

SRAREADER_SRC = \
	reader-cmn \
	reader-fastq \
	reader-sff \
	reader-illumina \
	reader-abi

SRAREADER_OBJ = \
	$(addsuffix .$(LOBX),$(SRAREADER_SRC))

SRAREADER_LIB = \
	-dsradb \
	-dkdb \
	-dklib

$(LIBDIR)/libsrareader.$(SHLX): $(SRAREADER_OBJ)
	$(LD) --dlib --vers $(SRCDIR) -o $@ $^ $(SRAREADER_LIB)

$(LIBDIR)/libsrareader.$(LIBX): $(SRAREADER_OBJ)
	$(LD) --slib --vers $(SRCDIR) -o $@ $^ $(SRAREADER_LIB)

libsrareader_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) libsrareader $(SRAREADER_OBJ)
