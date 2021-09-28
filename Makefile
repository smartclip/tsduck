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
#  Root makefile for the TSDuck project.
#
#  Additional options which can be defined:
#
#  - NOTEST     : Do not build unitary tests.
#  - NODEKTEC   : No Dektec support, remove dependency to DTAPI.
#  - NOCURL     : No HTTP support, remove dependency to libcurl.
#  - NOPCSC     : No smartcard support, remove dependency to pcsc-lite.
#  - NOSRT      : No SRT support, remove dependency to libsrt.
#  - NORIST     : No RIST support, remove dependency to librist.
#  - NOTELETEXT : No Teletext support, remove teletext handling code.
#  - NOGITHUB   : No version check, no download, no upgrade from GitHub.
#
#  Options to define the representation of bitrates:
#
#  - BITRATE_INTEGER    : Bitrates are 64-bit integer.
#  - BITRATE_FRACTION   : Bitrates are fractions of two 64-bit integers.
#  - BITRATE_FLOAT      : Bitrates are 64-bit floating-point.
#  - BITRATE_FIXED      : Bitrates are 64-bit fixed-point.
#  - BITRATE_DECIMALS=n : Number of decimal with fixed-point (default: 1).
#
#-----------------------------------------------------------------------------


include Makefile.tsduck

EXTRA_DISTCLEAN   += bin dektec
NORECURSE_SUBDIRS += bin dektec

# Analyze our code only, not downloaded 3rd-party code in dektec.
CPPCHECK_SOURCES   = src
FLAWFINDER_SOURCES = src
SCANBUILD_SOURCES  = src
COVERITY_SOURCES   = src
CLOC_SOURCES       = src
CLOC_FLAGS        += --exclude-ext=.tgz,.tar.gz,.tar,.pdf,.pptx,.docx

# By default, recurse make target in all subdirectories
default:
	+@$(RECURSE)

# Build and run all tests.
.PHONY: test-all
test-all: test test-suite

# Build and run unitary tests.
.PHONY: test
test: default
	@$(MAKE) -C src/utest $@

# Execute the TSDuck test suite from a sibling directory, if present.
.PHONY: test-suite
test-suite: default
	@if [[ -d ../tsduck-test/.git ]]; then \
	   cd ../tsduck-test; git pull; ./run-all-tests.sh --bin "$(BINDIR)"; \
	 elif [[ -x ../tsduck-test/run-all-tests.sh ]]; then \
	   ../tsduck-test/run-all-tests.sh --bin "$(BINDIR)"; \
	 else \
	   echo >&2 "No test repository in ../tsduck-test"; \
	 fi

# Build the sample applications.
.PHONY: sample
sample:
	@$(MAKE) -C sample $@

# Display the built version
.PHONY: show-version
show-version: default
	$(BINDIR)/tsversion --version=all

# Install files, using SYSROOT as target system root if necessary.
.PHONY: install install-tools install-devel
install install-tools install-devel:
	$(MAKE) NOTEST=true -C src $@
	$(MAKE) NOTEST=true -C build $@

# Various build targets are redirected to build subdirectory.
.PHONY: tarball rpm rpm32 deb
tarball rpm rpm32 deb installer:
	$(MAKE) -C build $@
