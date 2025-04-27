# Mercury

### A project by Travis Heavener

## Table of Contents

- [Table of Contents](#table-of-contents)
- [About](#about)
- [Build Info](#build-info)
    - [Linux & Windows Builds](#linux--windows-builds)
    - [Windows Only](#windows-only)
- [Changelog](CHANGELOG.md)
- [Credits](CREDITS.md)

## About

Mercury is an HTTP server designed in C++ using the C-socket paradigm, and is available for both Linux and Windows.

Self-signed TLS 1.3 certs are now available with OpenSSL.

## Build Info

The `/build_tools/` directory contains all necessary shell scripts for building static binaries.

Binaries are placed in the `/bin/` directory, `main.o` for Linux and `main.exe` for Windows.

**Note: Mercury binaries must be run from within the `/bin/` directory as resources & config files are loaded relative to the working directory.**

### Linux & Windows Builds

1. Install all necessary dependencies.

    `./build_tools/install_deps.sh`.

2. Build the static OpenSSL binaries.

    `./build_tools/build_static_openssl.sh`.

3. Build the static Brotli binaries.

    `./build_tools/build_static_brotli.sh`.

4. **If building for Linux**, run `make linux`.

    **If building for Windows**, go to step 5.

### Windows Only

5. Clone the official zlib repository (https://github.com/luvit/zlib)
6. `cd` into the repository
7. Edit win32/Makefile.gcc. Search for "PREFIX" and set it equal to `x86_64-w64-mingw32-`. On the next line, edit "CC" to be equal to `$(PREFIX)gcc-win32`.
8. Locate the install location of x86_64-w64-mingw32 on your system (try /usr/x86_64-w64-mingw32). With this location, replace `<dir>` with the install location of x86_64-w64-mingw32 and run:

    `sudo BINARY_PATH=<dir>/bin INCLUDE_PATH=<dir>/include LIBRARY_PATH=<dir>/lib make -B -f win32/Makefile.gcc`.

9. Run the same command with the additional "install" at the end:

    `sudo BINARY_PATH=<dir>/bin INCLUDE_PATH=<dir>/include LIBRARY_PATH=<dir>/lib make -B -f win32/Makefile.gcc install`.

10. *Now*, `cd` back into the directory of this repository and run `make windows`.