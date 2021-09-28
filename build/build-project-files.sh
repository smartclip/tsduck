#!/usr/bin/env bash
#-----------------------------------------------------------------------------
#
#  TSDuck - The MPEG Transport Stream Toolkit
#  Copyright (c) 2005-2021, Thierry Lelegard
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#
#  1. Redistributions of source code must retain the above copyright notice,
#     this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
#  THE POSSIBILITY OF SUCH DAMAGE.
#
#-----------------------------------------------------------------------------
#
#  This script builds the various project files for the TSDuck library.
#  This script is useful when source files are added to or removed from the
#  directory src/libtsduck.
#
#  The following files are rebuilt:
#
#  - src/libtsduck/tsduck.h
#  - src/libtsduck/dtv/private/tsRefType.h
#
#  See the PowerShell script build-project-files.ps1 for a Windows equivalent.
#
#-----------------------------------------------------------------------------

# Optional file to build.
TARGET=$(basename "$1")

# Get the project directories.
ROOTDIR=$(cd $(dirname ${BASH_SOURCE[0]})/..; pwd)
SRCDIR="$ROOTDIR/src/libtsduck"

# On macOS, make sure that commands which were installed by Homebrew packages are in the path.
[[ $(uname -s) == Darwin ]] && export PATH="$PATH:/usr/local/bin:/opt/homebrew/bin"

# Enforce LANG to get the same sort order everywhere.
export LANG=C
export LC_ALL=$LANG

# Get all libtsduck files by type.
# Syntax: GetSources [additional-find-arguments]
GetSources()
{(
    cd "$SRCDIR"
    find . -type f -name '*.h' "$@" |
        sed -e 's|.*/||' -e 's|^|#include "|' -e 's|$|"|' |
        sort --ignore-case
)}

# Generate the main TSDuck header file.
GenerateMainHeader()
{
    cat "$ROOTDIR/src/HEADER.txt"
    echo ''
    echo '#pragma once'
    GetSources \
        ! -path '*/linux/*' ! -path '*/mac/*' ! -path '*/unix/*' ! -path '*/windows/*' ! -path '*/private/*' \
        ! -name "tsStaticReferences*" ! -name "*Template.h" ! -name "tsduck.h"
    echo ''
    echo '#if defined(TS_LINUX)'
    GetSources \( -path '*/unix/*' -o -path '*/linux/*' \) ! -name "*Template.h"
    echo '#endif'
    echo ''
    echo '#if defined(TS_MAC)'
    GetSources \( -path '*/unix/*' -o -path '*/mac/*' \) ! -name "*Template.h"
    echo '#endif'
    echo ''
    echo '#if defined(TS_WINDOWS)'
    GetSources -path '*/windows/*' ! -name "*Template.h"
    echo '#endif'
}

# Generate includes based on doxygen group name (as in "@ingroup name").
# Syntax: GetGroup name
GetGroup()
{(
    local group="$1"
    cd "$SRCDIR"
    find . -name 'ts*.h' ! -path '*/tsAbstract*.h' ! -name tsVCT.h |
        xargs grep -l "@ingroup *$group" |
        sed -e 's|^.*/ts\(.*\)\.h$|\1|' -e 's|^|    REF_TYPE(|' -e 's|$|);|' |
        sort --ignore-case
)}

# Generate the header file containing static references to all tables and descriptors.
GenerateRefType()
{
    GetGroup table
    echo ''
    GetGroup descriptor
}

# Generate the files.
[[ -z "$TARGET" || "$TARGET" == "tsduck.h"    ]] && GenerateMainHeader >"$SRCDIR/tsduck.h"
[[ -z "$TARGET" || "$TARGET" == "tsRefType.h" ]] && GenerateRefType >"$SRCDIR/dtv/private/tsRefType.h"

exit 0
