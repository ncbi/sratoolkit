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


The NCBI SRA ( Sequence Read Archive )


Contact: sra-tools@ncbi.nlm.nih.gov
http://trace.ncbi.nlm.nih.gov/Traces/sra/std

About configuration-assistant:

  configuration-assistant.perl will help you to configure the SRA tools to be able
  to access the local reference repository and to download the correct references
  for a given cSRA file.

To run configuration-assistant on Linux or Mac:
  - open a shell
  - change directory to the folder where you extracted the toolkit (cd .../sratoolkit...),
  - type 'perl configuration-assistant.perl' (without the quotation marks).


To run configuration-assistant on Windows:

  - open Command Prompt (Start->Run->cmd.exe),
  - change directory to the folder where you extracted the toolkit (cd ...\sratoolkit.x.x.x-win64),
  - type 'perl configuration-assistant.perl' (without the quotation marks).


If the following message shows up:

  "'perl' is not recognized as an internal or external command,
    operable program or batch file."

that means perl is not installed or not installed properly.

Download Perl at ActiveState.com (http://www.activestate.com/activeperl/downloads)
