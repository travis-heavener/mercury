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

### v0.1.6
- Fixed #3 (socket bind failure in immediate re-execution)

### v0.1.5
- Added mercury.conf configuration XML file
    - Config file has logfile directives that don't currently do anything yet
    - Parsed by [PugiXML](https://github.com/zeux/pugixml)!
- Refactored & reduced unused methods
    - Known MIME types are now loaded by the loadConfig method in conf.hpp

### v0.1.4
- Accept header now processes `*/*` as intended
- Now explicitly prevents users from accessing files above the document root
- Improved handling of 405 Method Not Allowed status codes
    - Response now appropriately specifies Allow header
    - Now only returns HTML display if explicitly Accepted by the request

### v0.1.3
- Fixed MAJOR file compression bug on Windows systems
- Improved overall handling of file compression

### v0.1.2
- File paths with query strings are now handled as intended
- Improved handling of index files

### v0.1.1
- Added initial welcome note
- Fixed [Dependencies](#dependencies) with Windows info
- Windows port now allows deflate compresesion via zlib port

### v0.1.0
- Added changelog (lol)
- Added Windows port