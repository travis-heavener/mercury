# Mercury

### A project by Travis Heavener

## Table of Contents

- [Table of Contents](#table-of-contents)
- [About](#about)
- [Build Info](#build-info)
    - [Linux](#linux)
    - [Windows](#windows)
- [Changelog](CHANGELOG.md)
- [Credits](CREDITS.md)

## About

Mercury is an HTTP server designed in C++ using the C-socket paradigm.

Currently, Mercury is available on Linux and Windows.

## Build Info

### Linux

Linux builds are tested on Ubuntu 22 LTS via WSL, thus I assume most other Debian flavors should compile as well.

Self-signed TLS 1.3 certs available for Linux builds via libssl.

To build for Linux, zlib, libssl, and libbrotli must be installed.

1. Install deps via `sudo apt-get install zlib1g-dev libssl-dev libbrotli-dev -y`.

Now, build the dynamic Linux binary.

2. `cd` into the directory of this repository clone and run `make linux`.

### Windows

Windows builds are compiled in the same Linux environment as mentioned above (see [Linux](#linux)).

To build for Windows, zlib must be installed.

1. Clone the official zlib repository (https://github.com/luvit/zlib)
2. `cd` into the repository
3. Edit win32/Makefile.gcc. Search for "PREFIX" and set it equal to `x86_64-w64-mingw32-`. On the next line, edit "CC" to be equal to `$(PREFIX)gcc-win32`.
4. Locate the install location of x86_64-w64-mingw32 on your system (try /usr/x86_64-w64-mingw32). With this location, replace `<dir>` with the install location of x86_64-w64-mingw32 and run:

    `sudo BINARY_PATH=<dir>/bin INCLUDE_PATH=<dir>/include LIBRARY_PATH=<dir>/lib make -B -f win32/Makefile.gcc`.

5. Run the same command with the additional "install" at the end:

    `sudo BINARY_PATH=<dir>/bin INCLUDE_PATH=<dir>/include LIBRARY_PATH=<dir>/lib make -B -f win32/Makefile.gcc install`.

6. *Now*, `cd` back into the directory of this repository and run `make windows`.
