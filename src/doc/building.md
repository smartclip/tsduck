# Building TSDuck   {#building}
[TOC]

TSDuck can be built on Windows, Linux and macOS.

Support for Dektec devices, DVB tuners and HiDes modulators is implemented only
on Windows and Linux. macOS can only support files and networking for TS input and output.

Some protocols such as SRT and RIST require external libraries which may
not be available on all platforms or all versions of a specific distro.

# Unix systems (Linux and macOS) {#unixbuild}

## Pre-requisites {#requnix}

Operations in this section must be run once, before building TSDuck for the
first time one a given system.

Execute the shell-script `build/install-prerequisites.sh`.
It downloads and installs the requested packages which are necessary
to build TSDuck. The list of packages and how to install them depend
on the operating system distribution and version.

Currently, the script supports the following operating systems:
- macOS
- Ubuntu
- Debian
- Raspbian (Debian for Raspberry Pi)
- Fedora
- Red Hat Enterprise Linux
- CentOS
- Arch Linux
- Alpine Linux
- Gentoo
- Linux Mint

Since all packages are pulled from the standard repositories of each distro,
there is generally no need to re-run this script later. The packages will be
updated as part of the system system updates. Note, however, that a new version
of TSDuck may require additional dependencies. In case of build error, it can
be wise to run `build/install-prerequisites.sh` again and retry.

Dektec DTAPI: The command `make` at the top level will automatically
download the LinuxSDK from the Dektec site. See `dektec/Makefile` for details.
There is no manual setup for DTAPI on Linux. Note that the Dektec DTAPI is
available only for Linux distros on Intel CPU's with the GNU libc. Non-Intel systems
(for instance ARM-based devices such as Raspberry Pi) cannot use Dektec devices.
Similarly, Intel-based distros using a non-standard libc (for instance Alpine Linux
which uses musl libc) cannot use Dektec devices either.

## Building the TSDuck binaries alone {#buildunix}

Execute the command `make` at top level. The TSDuck binaries, executables and shared
objects (`.so` or `.dylib`), are built in directories `bin/release-<arch>-<hostname>`.

Note that TSDuck contains thousands of source files and building it can take time.
However, since most machines have multiple CPU's, all makefiles are designed for
parallel builds. On a quad-core machine, for instance, the command `make -j10`
is recommended (10 parallel compilations), reducing the total build time to less
than five minutes.

To build a 32-bit version of TSDuck on a 64-bit system, execute the command `make m32`.
Of course, this works only if your 64-bit system has all required 32-bit development
tools and libraries.

To cleanup the repository tree and return to a pristine source state,
execute `make distclean` at the top level.

## Building without specialized dependencies {#buildopt}

In specific configurations, you may want to disable some external libraries
such as `libcurl` or `pcsc-lite`. Of course, the corresponding features in
TSDuck will be disabled but the impact is limited. For instance, disabling
`libcurl` will disable the input plugins `http` and `hls`.

The following `make` variables can be defined:

- `NOTEST`   : Do not build unitary tests.
- `NODEKTEC` : No Dektec support, remove dependency to `DTAPI`.
- `NOCURL`   : No HTTP support, remove dependency to `libcurl`.
- `NOPCSC`   : No smartcard support, remove dependency to `pcsc-lite`.
- `NOSRT`    : No SRT support (Secure Reliable Transport), remove dependency to `libsrt`.
- `NORIST`   : No RIST support (Reliable Internet Stream Transport), remove dependency to `librist`.
- `NOTELETEXT` : No Teletext support, remove teletext handling code.
- `ASSERTIONS` : Keep assertions in production mode (slower code).

The following command, for instance, builds TSDuck without dependency
to `pcsc-lite`, `libcurl` and Dektec `DTAPI`:
~~~
make NOPCSC=1 NOCURL=1 NODTAPI=1
~~~

## Building with RIST support on Linux {#buildrist}

By default, TSDuck is built with RIST support on macOS and Windows only.

As of this writing, RIST (Reliable Internet Stream Transport) is not available
in the standard repositories of any Linux distro. By default, TSDuck cannot be
built on Linux with RIST support since this would introduce dependencies on
packages which do not exist during installation.

However, it possible to manually rebuild TSDuck with RIST support using the
command `make RIST=1`.

Before building TSDuck with RIST support, it is necessary to install `librist`
with development files in standard locations. You may manually rebuild and install
`librist`.

It is also possible to get [pre-built packages for various Linux
distros here](https://github.com/tsduck/rist-installer/releases).
If you use other versions of rpm-based or deb-based distros,
the [rist-installer project](https://github.com/tsduck/rist-installer)
contains scripts to rebuild these packages.

## Building the TSDuck installation packages {#buildinst}

Execute the command `make installer` at top level to build all packages.

Depending on the platform, the packages can be `.deb` or `.rpm` files.

There is no need to build the TSDuck binaries before building the installers.
Building the binaries, when necessary, is part of the installer build.

All installation packages are dropped into the subdirectory `installers`.
The packages are not deleted by the cleanup procedures. They are not pushed
into the git repository either.

Special note on macOS: There is no binary package for TSDuck on macOS.
On this platform, TSDuck is installed using [Homebrew](https://brew.sh),
a package manager for open-source projects on macOS.
See [here for more details](https://github.com/tsduck/homebrew-tsduck).

## For packagers of Linux distros {#distropack}

Packagers of Linux distros may want to create TSDuck packages. The build methods
are not different. This section contains a few hints to help the packaging.

By default, TSDuck is built with capabilities to check the availability of new
versions on GitHub. The `tsversion` command can also download and upgrade TSDuck
from the binaries on GitHub. Packagers of Linux distros may want to disable this
since they prefer to avoid mixing their TSDuck packages with the generic TSDuck
packages on GitHub. To disable this feature, build TSDuck with `make NOGITHUB=1`.

The way to build a package depends on the package management system. Usually,
the build procedure includes an installation on a temporary fake system root.
To build TSDuck and install it on `/temporary/fake/root`, use the following
command:
~~~~
make NOGITHUB=1 install SYSROOT=/temporary/fake/root
~~~~

It is recommended to create two distinct packages: one for the TSDuck tools and
plugins and one for the development environment. The development package shall
require the pre-installation of the tools package.

If you need to separately build TSDuck for each package, use targets `install-tools`
and `install-devel` instead of `install` which installs everything:

~~~~
make NOGITHUB=1 install-tools SYSROOT=/temporary/fake/root
make NOGITHUB=1 install-devel SYSROOT=/temporary/fake/root
~~~~

## Installing in non-standard locations {#nonstdinstunix}

On systems where you have no administration privilege and consequently no right
to use the standard installers, you may want to manually install TSDuck is some
arbitrary directory.

You have to rebuild TSDuck from the source repository and install it using
a command like this one:

~~~~
make install SYSPREFIX=$HOME/usr/local
~~~~

The TSDuck commands are located in the `bin` subdirectory and can be executed
from here without any additional setup. It is probably a good idea to add this
`bin` directory in your `PATH` environment variable.

## Running from the build location {#runbuildunix}

It is sometimes useful to run a TSDuck binary, `tsp` or any other, directly
from the build directory, right after compilation. This can be required for
testing or debugging.

Because the binary directory name contains the host name, it is possible to build
TSDuck using the same shared source tree from various systems or virtual machines.
All builds will coexist using distinct names under the `bin` subdirectory.

For _bash_ users who wish to include the binary directory in the `PATH`, simply
"source" the script `build/setenv.sh`. Example:
~~~~
$ . build/setenv.sh 
$ which tsp
/Users/devel/tsduck/bin/release-x86_64-mymac/tsp
~~~~

This script can also be used with option `--display` to display the actual
path of the binary directory. The output can be used in other scripts
(including from any other shell than _bash_). Example:
~~~~
$ build/setenv.sh --display
/Users/devel/tsduck/bin/release-x86_64-mymac
~~~~

Use `build/setenv.sh --help` for other options.

# Windows systems {#winbuild}

## Pre-requisites {#reqwindows}

Operations in this section must be run once, before building TSDuck for the
first time one a given system. It should also be run to get up-to-date versions
of the build tools and libraries which are used by TSDuck.

First, install Visual Studio Community Edition.
This is the free version of Visual Studio.
It can be downloaded [here](https://www.visualstudio.com/downloads/).
If you already have Visual Studio Enterprise Edition (the commercial version),
it is fine, no need to install the Community Edition.

Then, execute the PowerShell script `build\install-prerequisites.ps1`.
It downloads and installs the requested packages which are necessary
to build TSDuck on Windows.

If you prefer to collect the various installers yourself, follow the links to
[NSIS downloads](http://nsis.sourceforge.net/Download),
[SRT downloads](https://github.com/tsduck/srt-win-installer/releases/latest),
[RIST downloads](https://github.com/tsduck/rist-installer/releases/latest),
[DTAPI downloads](https://www.dektec.com/downloads/SDK),
[Doxygen downloads](http://www.doxygen.org/download.html) and
[Graphviz downloads](https://graphviz.gitlab.io/_pages/Download/Download_windows.html).

## Building the binaries without installer {#buildwindows}

Execute the PowerShell script `build\build.ps1`. The TSDuck binaries, executables
and DLL's, are built in directories named `bin\<target>-<platform>`, for instance
`bin\Release-x64` or `bin\Debug-Win32`.

To cleanup the repository tree and return to a pristine source state,
execute the PowerShell script `build\cleanup.ps1`.

## Building the installers {#instwindows}

Execute the PowerShell script `build\build-installer.ps1`.
Two installers are built, for 32-bit and 64-bit systems respectively.

There is no need to build the TSDuck binaries before building the installers.
Building the binaries, is part of the installer build.

All installation packages are dropped into the subdirectory `installers`.
The packages are not deleted by the cleanup procedures. They are not pushed
into the git repository either.

## Installing in non-standard locations {#nonstdinstwin}

On systems where you have no administration privilege and consequently no right
to use the standard installers, you may want to manually install TSDuck is some
arbitrary directory.

On Windows systems, a so-called _portable_ package is built with the installers.
This is a zip archive file which can be expanded anywhere.

## Running from the build location {#runbuildwin}

It is sometimes useful to run a TSDuck binary, `tsp` or any other, directly
from the build directory, right after compilation. This can be required for
testing or debugging.

The commands can be run using their complete path without additional setup.
For instance, to run the released 64-bit version of `tsp`, use:
~~~~
D:\tsduck> bin\Release-x64\tsp.exe --version
tsp: TSDuck - The MPEG Transport Stream Toolkit - version 3.12-730
~~~~

For other combinations (release vs. debug and 32 vs. 64 bits), the paths
from the repository root are:
~~~~
bin\Release-x64\tsp.exe
bin\Release-Win32\tsp.exe
bin\Debug-x64\tsp.exe
bin\Debug-Win32\tsp.exe
~~~~

# Installer files summary {#instfiles}

The following table summarizes the packages which are built and dropped
into the `installers` directory, through a few examples, assuming that the
current version of TSDuck is 3.12-745.

| File name                             | Description
| ------------------------------------- | -----------------------------------------------------
| TSDuck-3.12-745-src.zip               | Source archive on Windows
| tsduck-3.12-745.tgz                   | Source archive on Linux and macOS
| tsduck_3.12-745_amd64.deb             | Binary package for 64-bit Ubuntu
| tsduck_3.12-745_armhf.deb             | Binary package for 32-bit Raspbian (Raspberry Pi)
| tsduck-3.12-745.el7.i386.rpm          | Binary package for 32-bit Red Hat or CentOS 7.x
| tsduck-3.12-745.el7.x86_64.rpm        | Binary package for 64-bit Red Hat or CentOS 7.x
| tsduck-3.12-745.el7.src.rpm           | Source package for Red Hat or CentOS 7.x
| tsduck-3.12-745.fc25.i386.rpm         | Binary package for 32-bit Fedora 25
| tsduck-3.12-745.fc25.x86_64.rpm       | Binary package for 64-bit Fedora 25
| tsduck-3.12-745.fc25.src.rpm          | Source package for Fedora 25
| tsduck-dev_3.12-745_amd64.deb         | Development package for 64-bit Ubuntu
| tsduck-dev_3.12-745_armhf.deb         | Development package for 32-bit Raspbian (Raspberry Pi)
| tsduck-devel-3.12-745.el7.i386.rpm    | Development package for 32-bit Red Hat or CentOS 7.x
| tsduck-devel-3.12-745.el7.x86_64.rpm  | Development package for 64-bit Red Hat or CentOS 7.x
| tsduck-devel-3.12-745.fc25.i386.rpm   | Development package for 32-bit Fedora 25
| tsduck-devel-3.12-745.fc25.x86_64.rpm | Development package for 64-bit Fedora 25
| TSDuck-Win32-3.12-745.exe             | Binary installer for 32-bit Windows
| TSDuck-Win64-3.12-745.exe             | Binary installer for 64-bit Windows
| TSDuck-Win32-3.12-745-Portable.zip    | Portable package for 32-bit Windows
| TSDuck-Win64-3.12-745-Portable.zip    | Portable package for 64-bit Windows

On Linux systems, there are two different packages. The package `tsduck` contains
the tools and plugins. This is the only required package if you just need to use
TSDuck. The package named `tsduck-devel` (or `tsduck-dev` on Ubuntu and Debian)
contains the development environment. It is useful only for third-party applications
which use the TSDuck library.

On Windows systems, there is only one binary installer which contains the tools,
plugins, documentation and development environment. The user can select which
components shall be installed. The development environment is unselected by default.
