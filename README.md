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

5. Build the static zlib binaries.

    `./build_tools/build_static_zlib.sh`.

6. Now, build for Windows.

    `make windows`.