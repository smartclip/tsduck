#!/bin/bash

# Move to root directory.
cd $(dirname $(dirname $0))

# Build list of files and directories.
INCLUDES=$(
    find src/libtsduck -type d ! -name windows ! -name mac ! -path '*/python*' | \
        sed 's|^|-I|' | \
        tr '\n' ' ' \
)
FILESYSTEM=$(
    ls src/libtsduck/dtv/*.xml src/libtsduck/dtv/*.names | \
        sed -e 's|^\(.*/\)\([^/]*\)$|{"name":"/usr/share/tsduck/\2", "from":"\1\2"}, |' -e '$s|, $||' | \
        tr '\n' ' ' \
)
SOURCES=$(
    find src -name '*.cpp' \
         ! -path '*/windows/*' ! -path '*/mac/*' ! -path '*/python/*' ! -path '*/tstools/*' \
         ! -name tsStaticReferencesDVB.cpp ! -name dependenciesForStaticLib.cpp | \
        sed -e 's|^\(.*\)$|"\1",|' -e '$s|,$||' | \
        tr '\n' ' ' \
)

# Generate tis.config in root directory (a poor choice...)
sed <./.tis/tis.config.template >tis.config \
    -e "s|@@INCLUDES@@|$INCLUDES|" \
    -e "s|@@FILESYSTEM@@|$FILESYSTEM|" \
    -e "s|@@SOURCES@@|$SOURCES|"
