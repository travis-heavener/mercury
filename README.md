# Mercury

### A project by Travis Heavener

## About

Mercury is an HTTP server designed in C++ using the C-socket paradigm.

## Dependencies

Currently, Mercury is available on Linux and Windows.

### Linux

Linux builds are tested on Ubuntu 22 LTS via WSL, thus I assume most other Debian flavors should compile as well.

To build for Linux, zlib must be installed.

1. Install zlib via `sudo apt-get install zlib1g-dev`.
2. `cd` into the directory of this repository clone and run `make linux`.

### Windows

To build for Windows, zlib must be installed.

1. Clone the official zlib repository (https://github.com/luvit/zlib)
2. `cd` into the repository
3. Edit win32/Makefile.gcc
4. Locate the install location of x86_64-w64-mingw32 on your system (try /usr/x86_64-w64-mingw32). With this location, replace `<dir>` with the install location of x86_64-w64-mingw32 and run:

    `sudo BINARY_PATH=<dir>/bin INCLUDE_PATH=<dir>/include LIBRARY_PATH=<dir>/lib make -B -f win32/Makefile.gcc`.

5. Run the same command with the additional "install" at the end:

    `sudo BINARY_PATH=<dir>/bin INCLUDE_PATH=<dir>/include LIBRARY_PATH=<dir>/lib make -B -f win32/Makefile.gcc install`.

6. *Now*, `cd` back into the directory of this repository and run `make windows`.

## Changelog

### v0.1.1
- Added initial welcome note
- Fixed [Dependencies](#dependencies) with Windows info
- Windows port now allows deflate compresesion via zlib port

### v0.1.0
- Added changelog (lol)
- Added Windows port