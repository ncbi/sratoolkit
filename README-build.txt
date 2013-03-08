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


The NCBI SRA ( Sequence Read Archive ) SDK ( Software Development Kit )


Contact: sra-tools@ncbi.nlm.nih.gov
http://trace.ncbi.nlm.nih.gov/Traces/sra/std


This version of the NCBI SRA SDK generates tools with their respective libraries
for building new and accessing existing runs. It may be built with GCC.


REQUIREMENTS:

This software release was designed to run under Linux, MacOSX and Windows
operating systems on Intel x86-compatible 32 and 64 bit architectures.

  ar                # tested with version 2.22
  bash              # certain scripts require bash
  make              # GNU make version 3.81 or later
  gcc, g++          # tested with 4.4.2, but should work with others
  libxml2           # tested with version 2.7.6
  libcurl           # tested with version 7.27

WINDOWS BUILD:

The Windows build uses the same make files as Linux and Mac, and has been tested
under Cygwin. You need to execute Cygwin AFTER sourcing the Microsoft batch file
from Visual Studio.


CONTENTS:

  Makefile          # drives configuration and sub-target builds
  README
  README-WINDOWS.txt
  build             # holds special makefiles and configuration
  interfaces        # contains module interfaces, schema, plus
                      compiler and platform specific includes
  libs              # sdk library code
  tools             # toolkit code


CONFIGURATION:

There are three configurable parameters:
  1) BUILD  = 'debug', 'release' etc.
  2) COMP   = 'GCC' etc.
  3) OUTDIR = <path-to-binaries-libs-objfiles>

The target architecture is chosen to match your build host. At this
time, only the Macintosh build will support cross-compilation. In the
instructions below, x86_64 is the assumed architecture. If your host
is i386 (32-bit), then you would substitute 32 for paths that contain
64.

Running "make help" will list more details of how your build may
be configured:

  Before initial build, run 'make OUTDIR=<dir> out' from
  the project root to set the output directory of your builds.

  To select a compiler, run 'make <comp>' where
  comp = { GCC VC++ CLANG }.

  For hosts that support cross-compilation ( only Macintosh today ),
  you can run 'make <arch>' where arch = { i386 x86_64 sparc32 sparc64 }.

  To set a build configuration, run 'make <config>' where
  config = { debug profile release static dynamic }.

Running "make config" will show the current configuration, e.g.:

  current build is linux static rel x86_64 build using gcc tools
  output target directory is '$PATH_TO_OUTPUT/linux/gcc/stat/x86_64/rel'

where "$PATH_TO_OUTPUT" is local to your system, of course.


BUILD INSTRUCTIONS:

## create output directories and symlinks for first time

  $ OUTDIR=<path-to-output>
  $ make OUTDIR="$OUTDIR" out

The path in OUTDIR MUST be a full path - relative paths may fail.

## decide upon STATIC or DYNAMIC builds
#  VDB.2 was designed to make use of dynamic libraries, but
#  in many environments static builds are more convenient or may even be
#  required, due to installation restrictions.
#
#  THE BUILD DOES NOT CURRENTLY SUPPORT PARALLEL DYNAMIC AND STATIC MODES
#  if you switch between them, you should perform a "make clean" first.

  $ make static
-OR_
  $ make dynamic

## if you are using a dynamic build, update LD_LIBRARY_PATH - probably want to put
# in shell startup ( ensure that libz, libbz2 and libxml2 can be found in your path )

  $ export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$OUTDIR/lib64"

## the build uses "TOP" as an internal variable
#  if you have it defined in your shell environment, unset it before making

## build the libraries and binaries

  $ make GCC debug     # optionally set compiler and/or build
  $ make               # builds libraries and binary executables

The standard make will cause default libraries and tools to be built. Binary
executables and both shared and static libraries can be found in their
respective output directories.


STATIC BUILD RESULTS:

<OUTDIR>/bin64/         # (bin32 for 32-bit builds)
  abi-dump              # dump to ABI-native format
  abi-load              # load from ABI-native format
  align-info            # retrieve information from SRA alignment object (cSRA)
  bam-load              # load from BAM format into SRA (cSRA)
  cg-load               # load from Complete Genomics native format
  fastq-dump            # dump to FASTQ format
  fastq-load            # load from FASTQ format
  helicos-load          # load from Helicos native format
  illumina-dump         # dump to Illumina-native format
  illumina-load         # load from Illumina-native format
  kar                   # single file archive utility
  kdbmeta               # access db, table or column metadata
  latf-load             # generic FASTQ loader
  prefetch              # tool to download runs from NCBI by accession
  rcexplain             # return code display utility
  sam-dump              # dump to SAM format from cSRA
  sff-dump              # dumps 454 runs in SFF format
  sff-load              # load from SFF format
  sra-kar               # sra-specific kar tool
  sra-pileup            # produces SAM-style pileup information from SRA (cSRA)
  sra-sort              # canonically orders SRA (cSRA) data for improved access
  sra-stat              # gather run statistics and print to stdout
  srapath               # returns the full location of an object by accession
  srf-load              # load from SRF format
  test-sra              # test user's environment for configuration and libraries
  vdb-config            # display and/or modify user configuration
  vdb-copy              # tool to copy tables
  vdb-decrypt           # tool to decrypt files from dbGap
  vdb-dump              # dump rows in a textual format
  vdb-encrypt           # to to (re)encrypt files using dbGap encryption key
  vdb-lock              # locks an object against modification
  vdb-unlock            # unlocks an object

<OUTDIR>/lib64/         # (lib32 for 32-bit builds)
  libalign-access       # BAM format reading API
  libalign-reader       # SRA (cSRA) alignment reading API
  libbz2                # bzip2 library
  libkdb                # physical layer reading library
  libkfg                # configuration library
  libkfs                # physical file system library
  libklib               # support library
  libkproc              # process synchronization library
  libkq                 # cross-thread queue library
  libkrypto             # cryptographic library
  libsproc              # single-threaded stub library
  libksrch              # search algorithm library
  libkurl               # interface to libcurl
  libkxfs               # XML to filesystem library
  libkxml               # XML container support library
  libload               # loader tool utility library
  libsradb              # API for accessing sra
  libsrareader          # reader library
  libsraschema          # version of SRA schema for reading
  libvdb                # virtual layer reading library
  libvfs                # virtual file system library
  libwkdb               # physical layer update library
  libwsradb             # update API for SRA
  libwsraschema         # version of SRA schema for update
  libwvdb               # virtual layer update library
  libz                  # gzip library

<OUTDIR>/mod64/         # (mod32 for 32-bit builds)
  -- empty --

<OUTDIR>/wmod64/        # (wmod32 for 32-bit builds)
  -- empty --



DYNAMIC BUILD RESULTS:

<OUTDIR>/bin64/         # (bin32 for 32-bit builds)
  -- same as static --

<OUTDIR>/lib64/         # (lib32 for 32-bit builds)
  libalign-access       # BAM format reading API
  libalign-reader       # SRA (cSRA) alignment reading API
  libbz2                # static bzip2 library
  libkdb                # physical layer reading library
  libkfg                # configuration library
  libkfs                # physical file system library
  libklib               # support library
  libkproc              # process synchronization library
  libkq                 # cross-thread queue library
  libkrypto             # cryptographic library
  libsproc              # single-threaded stub library
  libksrch              # search algorithm library
  libkurl               # interface to libcurl
  libkxfs               # XML to filesystem library
  libkxml               # XML container support library
  libload               # loader tool utility library
  libsradb              # API for accessing sra
  libsrareader          # reader library
  libsraschema          # static version of SRA schema for reading
  libvdb                # virtual layer reading library
  libvfs                # virtual file system library
  libwkdb               # physical layer update library
  libwsradb             # update API for SRA
  libwsraschema         # static version of SRA schema for update
  libwvdb               # virtual layer update library
  libz                  # static gzip library

<OUTDIR>/mod64/         # (mod32 for 32-bit builds)
  libaxf                # cSRA-specific VDB external functions
  libsraxf              # SRA-specific VDB external functions
  libvxf                # generic VDB external functions
  libwgsxf              # WGS-specific VDB external functions

<OUTDIR>/wmod64/        # (wmod32 for 32-bit builds)
  libwaxf               # cSRA-specific VDB external functions for update
  libwsraxf             # SRA-specific VDB external functions for update
  libwvxf               # update VDB external functions
  libwwgsxf             # WGS-specific VDB external functions for update

