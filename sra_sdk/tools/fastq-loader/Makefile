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
MODULE = tools/fastq-loader

INT_LIBS = \
	libfastqloader

EXT_LIBS = \

ALL_LIBS = \
	$(INT_LIBS) \
	$(EXT_LIBS)

TEST_TOOLS = \
	wb-test-fastq

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

$(TEST_TOOLS): std makedirs
	@ $(MAKE_CMD) $(TEST_BINDIR)/$@

valgrind: $(TEST_TOOLS)
	@echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo Run $(TEST_BINDIR)/$(TEST_TOOLS)
	valgrind $(RUN_REMOTELY) $(TEST_BINDIR)/$(TEST_TOOLS)

.PHONY: all std $(ALL_LIBS) $(TEST_TOOLS) valgrind

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
INTERM_SRC = \
	$(SRCDIR)/fastq-lex.c \
	$(SRCDIR)/fastq-grammar.c \
	$(SRCDIR)/fastq-tokens.h 

clean: stdclean
	@ rm -f $(addsuffix *,$(addprefix $(TEST_BINDIR)/,$(TEST_TOOLS)))

.PHONY: clean

.PRECIOUS: $(INTERM_SRC)

#-------------------------------------------------------------------------------
# fastq
#
$(ILIBDIR)/libfastqloader: $(ILIBDIR)/libfastqloader.$(LIBX)

FASTQ_SRC = \
    fastq-reader \
	fastq-grammar \
	fastq-lex

$(SRCDIR)/fastq-grammar.c $(SRCDIR)/fastq-tokens.h: $(SRCDIR)/fastq-grammar.y
	$(YACC) -o $(SRCDIR)/fastq-grammar.c --defines=$(SRCDIR)/fastq-tokens.h $^

FASTQ_OBJ = \
	$(addsuffix .$(LOBX),$(FASTQ_SRC))

FASTQ_LIB = \
	-lkfs \
	-lklib \
    -lload \
    -lloader

ifneq (win,$(OS))
    FASTQ_LIB += -lkq
endif

$(ILIBDIR)/libfastqloader.$(SHLX): $(INTERM_SRC) $(FASTQ_OBJ)
	$(LD) --dlib -o $@ $^ $(FASTQ_LIB)

$(ILIBDIR)/libfastqloader.$(LIBX): $(INTERM_SRC) $(FASTQ_OBJ)
	$(LD) --slib -o $@ $^ $(FASTQ_LIB)

#-------------------------------------------------------------------------------
# white-box test
#
FASTQ_TEST_SRC = \
	wb-test-fastq

FASTQ_TEST_OBJ = \
	$(addsuffix .$(OBJX),$(FASTQ_TEST_SRC))

ifneq (win, $(OS))
    QLIB = -skq
endif
FASTQ_TEST_LIB = \
	-sfastqloader \
	-sloader \
	-salign-writer \
	-sktst \
	-skapp \
    -sload \
    -svfs \
    -swvdb \
    -swkdb \
    -skfg \
    -skrypto \
    $(QLIB) \
    -skproc \
    -skfs \
	-sklib


$(TEST_BINDIR)/wb-test-fastq: $(FASTQ_TEST_OBJ)
	$(LP) --exe -o $@ $^ $(FASTQ_TEST_LIB)


