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


#-------------------------------------------------------------------------------
# environment
#
TOP ?= $(CURDIR)
include $(TOP)/build/Makefile.shell


#-------------------------------------------------------------------------------
# default
#
SUBDIRS = \
	libs \
	tools

default: $(SUBDIRS)

test: $(SUBDIRS)

$(SUBDIRS) test:
	@ $(MAKE) -C $@

.PHONY: default $(SUBDIRS) test

#-------------------------------------------------------------------------------
# all
#
SUBDIRS_ALL = $(addsuffix _all,$(SUBDIRS))

all: $(SUBDIRS_ALL)

$(SUBDIRS_ALL):
	@ $(MAKE) -C $(subst _all,,$@) all

.PHONY: all $(SUBDIRS_ALL)

#-------------------------------------------------------------------------------
# std
#
SUBDIRS_STD = $(addsuffix _std,$(SUBDIRS))

std: $(SUBDIRS_STD)

$(SUBDIRS_STD):
	@ $(MAKE) -C $(subst _std,,$@) std

.PHONY: std $(SUBDIRS_STD)

#-------------------------------------------------------------------------------
# clean
#
SUBDIRS_CLEAN = $(addsuffix _clean,$(SUBDIRS_ALL))

clean: $(SUBDIRS_CLEAN)

$(SUBDIRS_CLEAN):
	@ $(MAKE) -s -C $(subst _all_clean,,$@) clean

.PHONY: clean $(SUBDIRS_CLEAN)


#-------------------------------------------------------------------------------
# runtests
#
SUBDIRS_RUNTESTS = $(addsuffix _runtests, libs tools test)

runtests: std $(SUBDIRS_RUNTESTS)

$(SUBDIRS_RUNTESTS):
	@ $(MAKE) -C $(subst _runtests,,$@) runtests

.PHONY: runtests $(SUBDIRS_RUNTESTS)

#-------------------------------------------------------------------------------
# pass-through targets
#
COMPILERS = GCC VC++ CLANG
ARCHITECTURES = i386 x86_64 sparc32 sparc64
CONFIG = debug profile release static dynamic
PUBLISH = scm pubtools
REPORTS = bindir targdir osdir config compilers architecture architectures
PASSTHRUS = \
	out \
	CC $(COMPILERS) \
	$(ARCHITECTURES) \
	$(CONFIG) $(PUBLISH)

$(PASSTHRUS):
	@ $(MAKE) -s TOP=$(CURDIR) -f build/Makefile.env $@
	@ $(MAKE) -s TOP=$(CURDIR) -f build/Makefile.env rebuild-dirlinks config

$(REPORTS):
	@ $(MAKE) -s TOP=$(CURDIR) -f build/Makefile.env $@

.PHONY: $(PASSTHRUS) $(REPORTS)


#-------------------------------------------------------------------------------
# configuration help
#
help configure:
	@ echo "Before initial build, run 'make OUTDIR=<dir> out' from"
	@ echo "the project root to set the output directory of your builds."
	@ echo
	@ echo "To select a compiler, run 'make <comp>' where"
	@ echo "comp = { "$(COMPILERS)" }."
	@ echo
	@ echo "For hosts that support cross-compilation ( only Macintosh today ),"
	@ echo "you can run 'make <arch>' where arch = { "$(ARCHITECTURES)" }."
	@ echo
	@ echo "To set a build configuration, run 'make <config>' where"
	@ echo "config = { "$(CONFIG)" }."
	@ echo

.PHONY: help configure
