#!/bin/bash

# Config
DirectorY="$1"
OutDir="$2"
FileName="$3"
BuiltDLP="$4"
MD5Sum="$5"

# Make sure we are in the right place
cd "${SRCROOT}"
if [ ! -d "external" ]; then
    mkdir external
fi
if [ ! -d "prebuilt" ]; then
    mkdir prebuilt
fi

# Checks
if [ "${ACTION}" = "clean" ]; then
    # Force cleaning when directed
    rm -fRv "prebuilt/${DirectorY}" "external/${OutDir}"
    MD5SumLoc=`md5 -q "prebuilt/${FileName}"`
    if [ "${MD5SumLoc}" != "${MD5Sum}" ]; then
        rm -fRv "prebuilt/${FileName}"
    fi
    exit 0
elif [ -d "external/${OutDir}" ]; then
    # Do not do more work then we have to
    echo "${OutDir} exists, skipping"
    exit 0
elif [ -d "prebuilt/${DirectorY}" ]; then
    # Clean if dirty
    echo "error: ${DirectorY} exists, probably from an earlier failed run" >&2
    #rm -frv "prebuilt/${DirectorY}"
    exit 1
fi

# Download
cd prebuilt
if [ ! -f "${FileName}" ]; then
    echo "Fetching ${FileName}"
    if ! curl -L -O --connect-timeout "30" "${BuiltDLP}"; then
        echo "error: Unable to fetch ${BuiltDLP}" >&2
        exit 1
    fi
else
    echo "${FileName} already exists, skipping"
fi

# MD5 check
MD5SumLoc=`md5 -q "${FileName}"`
if [ -z "${MD5SumLoc}" ]; then
    echo "error: Unable to compute md5 for ${FileName}" >&2
    exit 1
elif [ "${MD5SumLoc}" != "${MD5Sum}" ]; then
    echo "error: MD5 does not match for ${FileName}" >&2
    exit 1
fi

# Unpack
if ! tar -zxf "${FileName}"; then
    echo "error: Unpacking $FileName failed" >&2
    exit 1
fi

# Move
if [ ! -d "${DirectorY}" ]; then
    echo "error: Can't find $DirectorY to rename" >&2
    exit 1
else
    cd ..
    mv "prebuilt/${DirectorY}" "external/${OutDir}"
fi

exit 0