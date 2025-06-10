# Mercury

### A project by Travis Heavener

[![Build](https://github.com/travis-heavener/mercury/actions/workflows/build.yml/badge.svg)](https://github.com/travis-heavener/mercury/actions/workflows/build.yml)

## Table of Contents

- [Table of Contents](#table-of-contents)
- [About](#about)
- [Build Info](#build-info)
    - [Linux & Windows Builds](#linux--windows-builds)
- [Changelog](#changelog)
- [Credits](#credits)

## About

Mercury is an HTTP server designed in C++ using C socket programming, and is available for both Linux and Windows.

Self-signed TLS 1.3 certs are now available with OpenSSL.

## Build Info

The `/build_tools/` directory contains all necessary shell scripts for building static binaries.

Binaries are placed in the `/bin/` directory, `mercury` for Linux and `mercury.exe` for Windows.

**Note: Mercury binaries must be run from within the `/bin/` directory as resources & config files are loaded relative to the working directory.**

### Linux & Windows Builds

1. Install all necessary dependencies & extract static libraries.

    `make libs`.

2. **If building for Linux**, run `make linux`.

    **If building for Windows**, run `make windows`.

## Changelog
See [CHANGELOG.md](CHANGELOG.md)

## Credits
See [CREDITS.md](CREDITS.md)
