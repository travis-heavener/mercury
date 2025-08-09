# Mercury

### A project by Travis Heavener

[![Build](https://github.com/travis-heavener/mercury/actions/workflows/build.yml/badge.svg)](https://github.com/travis-heavener/mercury/actions/workflows/build.yml)
[![Test Runner (Linux)](https://github.com/travis-heavener/mercury/actions/workflows/test-linux.yml/badge.svg)](https://github.com/travis-heavener/mercury/actions/workflows/test-linux.yml)
[![Test Runner (Windows)](https://github.com/travis-heavener/mercury/actions/workflows/test-windows.yml/badge.svg)](https://github.com/travis-heavener/mercury/actions/workflows/test-windows.yml)

## Table of Contents

- [Table of Contents](#table-of-contents)
- [About](#about)
- [Build Info](#build-info)
    - [Linux & Windows Builds](#linux--windows-builds)
    - [TLS Certs](#tls-certs)
    - [Compatibility](#compatibility)
- [Testing Suite](#testing-suite)
- [Contributing](#contributing)
- [Changelog](#changelog)
- [Credits](#credits)

## About

Mercury is an HTTP server designed in C++ using C socket programming, and is available for both Linux and Windows.

## Build Info

The `/build_tools/` directory contains all necessary shell scripts for building static binaries.

Binaries are placed in the `/bin/` directory, `mercury` for Linux and `mercury.exe` for Windows.

**Note: Mercury binaries must be run from within the `/bin/` directory as resources & config files are loaded relative to the working directory.**

### Linux & Windows Builds

See compatibility

1. Install all necessary dependencies & extract static libraries.

    `make libs`.

2. **If building for Linux**, run `make linux`.

    **If building for Windows**, run `make windows`.

### TLS Certs

Self-signed TLS 1.3 certs are now available with OpenSSL.

- Replace `/conf/ssl/cert.pem` with your own certificate; and,

- Replace `/conf/ssl/key.pem` with your own private key.

### Compatibility

The following table contains known compatible versions of important software used to build Mercury.
Older mingw-w64 versions have introduced issues when binding IPv6 sockets with WinAPI.
The g++ version restriction is much looser, as initially version 11.x.x was in use and was only upgraded as a side effect of an OS upgrade (from 22.04 LTS).

| Name      | Version        |
|-----------|----------------|
| Ubuntu    | 24.04 LTS      |
| g++       | 13.3.0         |
| mingw-w64 | 11.0.1-3build1 |

## Testing Suite

This project has its own Python test runner complete with passes for IPv4 & IPv6 traffic with and without TLS enabled.
The test runner is available in the `tests` directory.

With Python installed, fire up the Mercury server, cd into the tests directory, and use the `run.py` file to run a number of tests against the server.

## Contributing
Contributions to the Mercury project are always welcome and appreciated.
Embracing help is crucial to maintaining a project and a healthy development community.

### Creating Issues & Pull Requests
The following sentence describes the thought process that should go into opening a new issue or pull request and is intentionally written subjectively:

> Each issue and pull request should contain enough information to understand its purpose and goal.

That's really all there is to it. I (Travis Heavener) will not actively police the formatting of issues and/or PRs. However, including certain Markdown headers such as `## About` and `## Screenshots` are a practice that I use myself and would encourage on this project.

Please make use of available tags for issues and pull requests, they are there for a reason.

### Creating Issues for Bugs
Creating issues for bugs are a bit more specific. It's best to include details about the environment to help recreate the conditions in which a bug has occured. But fear not, if you forget something in your issue, nobody is perfect. Just edit the issue. :)

### Branch Cleanup
When closing a pull request, it is encouraged that you delete your branch from the remote (GitHub). While this isn't a hard *requirement*, it is strongly encouraged as it improves the clarity of what branches are open or not.

Committing directly to main is disabled, so you must create a pull request for every change you'd like to make to main.

### Reviewing Pull Requests
In general 

## Changelog
See [CHANGELOG.md](CHANGELOG.md)

## Credits
See [CREDITS.md](CREDITS.md)
