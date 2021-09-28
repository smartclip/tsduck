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
#  This script installs all pre-requisites packages to build TSDuck,
#  depending on the system.
#
#  Supported options:
#
#  --m32 : install libraries for 32-bit cross-compilation (when supported)
#  --static : install static libraries for static build (when supported)
#  Additional options are passed to the package manager (dnf, apt, brew, etc.)
#
#  Supported operating systems:
#
#  - macOS
#  - Ubuntu
#  - Debian
#  - Raspbian
#  - Fedora
#  - Red Hat
#  - CentOS
#  - Arch Linux
#  - Alpine Linux
#  - Gentoo
#  - Linux Mint
#
#-----------------------------------------------------------------------------

SCRIPT=$(basename $0 .sh)
SCRIPTDIR=$(cd $(dirname $0); pwd)
error() { echo >&2 "$SCRIPT: $*"; exit 1; }

# Default options.
STATIC=false
M32=false
PKGOPTS=

# Decode command line options.
while [[ $# -gt 0 ]]; do
    case "$1" in
        --m32)
            M32=true
            ;;
        --static)
            STATIC=true
            ;;
        *)
            PKGOPTS="$PKGOPTS $1"
            ;;
    esac
    shift
done

[[ -n "$PKGOPTS" ]] && echo "====> using packager options: $PKGOPTS"

#-----------------------------------------------------------------------------

SYSTEM=$(uname -s)
DISTRO=$(lsb_release -i 2>/dev/null | sed -e 's/.*:[\t ]*//')
MAJOR=$(lsb_release -r 2>/dev/null | sed -e 's/.*:[\t ]*//' -e 's/\..*//')
MINOR=$(lsb_release -r 2>/dev/null | sed -e '/\./!d' -e 's/.*:[\t ]*//' -e 's/.*\.//')
VERSION=$(( ${MAJOR:-0} * 100 + ${MINOR:-0} ))

if [[ "$SYSTEM" == "Darwin" ]]; then

    # macOS
    pkglist="gnu-sed grep dos2unix coreutils pcsc-lite srt librist python3 openjdk"
    if [[ -z $(which clang 2>/dev/null) ]]; then
        # Build tools not installed
        xcode-select --install
    fi
    if [[ -z $(which brew 2>/dev/null) ]]; then
        # Homebrew not installed
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    brew update
    for pkg in $pkglist; do
        # Install or upgrade package (cannot be done in one unique command).
        # Sometimes, brew exits with an error status even though the installation completes.
        # Mute this and enforce a good status to avoid GitHub Actions CI failure.
        brew ls --versions $pkg >/dev/null && cmd=upgrade || cmd=install
        HOMEBREW_NO_AUTO_UPDATE=1 brew $PKGOPTS $cmd $pkg || true
    done
    # Register the openjdk jvm.
    [[ -e /Library/Java/JavaVirtualMachines/openjdk.jdk ]] || \
        sudo ln -sfn /usr/local/opt/openjdk/libexec/openjdk.jdk /Library/Java/JavaVirtualMachines/openjdk.jdk
    # Update command: brew update; brew upgrade

elif [[ "$DISTRO" == "Ubuntu" ]]; then

    # Ubuntu
    pkglist="g++ dos2unix curl tar zip doxygen graphviz pcscd libpcsclite-dev dpkg-dev python3 default-jdk"
    if [[ "$MAJOR" -le 17 ]]; then
        pkglist="$pkglist libcurl3 libcurl3-dev"
    else
        pkglist="$pkglist libcurl4 libcurl4-openssl-dev"
    fi
    if $M32; then
        pkglist="$pkglist gcc-multilib"
    fi
    if [[ "$VERSION" -ge 2010 ]]; then
        pkglist="$pkglist libsrt-openssl-dev"
    elif [[ "$VERSION" -ge 1904 ]]; then
        pkglist="$pkglist libsrt-dev"
    fi
    sudo apt update
    sudo apt install -y $PKGOPTS $pkglist
    # Update command: sudo apt update; sudo apt upgrade

elif [[ "$DISTRO" == "Linuxmint" ]]; then

    # Linux Mint
    pkglist="g++ dos2unix curl tar zip doxygen graphviz pcscd libpcsclite-dev dpkg-dev python3 default-jdk libcurl4 libcurl4-openssl-dev libsrt-dev"
    sudo apt update
    sudo apt install -y $PKGOPTS $pkglist
    # Update command: sudo apt update; sudo apt upgrade

elif [[ "$DISTRO" = "Debian" || "$DISTRO" = "Raspbian" ]]; then

    # Debian or Raspbian (Raspberry Pi)
    pkglist="g++ dos2unix curl tar zip doxygen graphviz pcscd libpcsclite-dev dpkg-dev python3 default-jdk"
    if $M32; then
        pkglist="$pkglist gcc-multilib"
    fi
    if [[ "$MAJOR" -le 9 ]]; then
        pkglist="$pkglist libcurl3 libcurl3-dev"
    else
        pkglist="$pkglist libcurl4 libcurl4-openssl-dev"
    fi
    if [[ "$MAJOR" -ge 11 ]]; then
        pkglist="$pkglist libsrt-openssl-dev"
    fi
    sudo apt update
    sudo apt install -y $PKGOPTS $pkglist
    # Update command: sudo apt update; sudo apt upgrade

elif [[ -f /etc/fedora-release ]]; then

    # Fedora
    FC=$(grep " release " /etc/fedora-release 2>/dev/null | sed -e 's/^.* release \([0-9\.]*\) .*$/\1/')
    pkglist="gcc-c++ dos2unix curl tar zip doxygen graphviz pcsc-tools pcsc-lite-devel libcurl libcurl-devel rpmdevtools python3 java-latest-openjdk-devel"
    if $STATIC; then
        pkglist="$pkglist glibc-static libstdc++-static"
    fi
    if $M32; then
        pkglist="$pkglist glibc-devel.i686 libstdc++-devel.i686 pcsc-lite-devel.i686 libcurl-devel.i686"
    fi
    if [[ $FC -ge 31 ]]; then
        pkglist="$pkglist srt-devel"
        if $M32; then
            pkglist="$pkglist srt-devel.i686"
        fi
    fi
    sudo dnf -y install $PKGOPTS $pkglist
    # Update command: sudo dnf update

elif [[ -f /etc/redhat-release ]]; then

    # Red Hat or CentOS
    EL=$(grep " release " /etc/redhat-release 2>/dev/null | sed -e 's/$/.99/' -e 's/^.* release \([0-9]*\.[0-9]*\).*$/\1/')
    EL=$(( ${EL/.*/} * 100 + ${EL/*./} ))
    pkglist="gcc-c++ dos2unix curl tar zip doxygen graphviz pcsc-lite pcsc-lite-devel libcurl libcurl-devel rpmdevtools python3 java-latest-openjdk-devel"
    if $STATIC; then
        pkglist="$pkglist glibc-static libstdc++-static"
    fi
    if $M32; then
        pkglist="$pkglist glibc-devel.i686 libstdc++-devel.i686 pcsc-lite-devel.i686 libcurl-devel.i686"
    fi
    if [[ $EL -ge 802 ]]; then
        pkglist="$pkglist srt-devel"
        if $M32; then
            pkglist="$pkglist srt-devel.i686"
        fi
    fi
    if [[ $EL -lt 800 ]]; then
        sudo yum -y install $PKGOPTS $pkglist
    elif [[ $EL -lt 803 ]]; then
        sudo dnf -y config-manager --set-enabled PowerTools
        sudo dnf -y install $PKGOPTS $pkglist
    else
        sudo dnf -y config-manager --set-enabled powertools
        sudo dnf -y install $PKGOPTS $pkglist
    fi
    # Update command: sudo dnf update

elif [[ -f /etc/arch-release ]]; then

    # Arch Linux
    pkglist="make gcc dos2unix core/which inetutils net-tools curl tar zip doxygen graphviz pcsclite srt python jdk-openjdk"
    sudo pacman -Syu --noconfirm $PKGOPTS $pkglist
    # Update command: sudo pacman -Syu

elif [[ -f /etc/alpine-release ]]; then

    # Alpine Linux
    pkglist="bash coreutils diffutils procps util-linux linux-headers git make g++ dos2unix curl tar zip doxygen graphviz pcsc-lite-dev curl-dev python3 openjdk11"
    sudo apk add $PKGOPTS $pkglist
    # Update command: sudo apk update; sudo apk upgrade

elif [[ -f /etc/gentoo-release ]]; then

    # Gentoo Linux
    pkglist="sys-devel/gcc app-text/dos2unix net-misc/curl app-arch/tar app-arch/zip app-arch/unzip app-doc/doxygen media-gfx/graphviz sys-apps/pcsc-lite net-libs/srt dev-lang/python dev-java/openjdk"
    sudo emerge -n $PKGOPTS $pkglist
    # Update command: sudo emerge --sync
    # or (??): sudo emerge -auvND --with-bdeps=y world
    # or (??): sudo emerge --update --deep --changed-use @world

fi
