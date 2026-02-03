<div align="center">
    <img src="docs/og-image.jpg" width="300" height="158">
    <br>
    <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-orange.svg"></a>
    <img src="https://img.shields.io/badge/Language-C%2B%2B-orange">
    <img src="https://img.shields.io/github/stars/travis-heavener/mercury?style=flat&label=Stars&color=orange">
    <br>
    <h1>Mercury</h1>
</div>

<div align="center">
    <a href="https://github.com/travis-heavener/mercury/actions/workflows/build.yml"><img style="margin-top: 0.5rem; margin-bottom: 0" src="https://github.com/travis-heavener/mercury/actions/workflows/build.yml/badge.svg"></a>
    <h3><em>A lightweight, configurable HTTP server</em></h3>
    <h3>Project by Travis Heavener</h3>
</div>

---

## Table of Contents

- [About](#about)
    - [Performance](#performance)
- [Getting Started](#getting-started)
    - [Config Files](#config-files)
    - [Log Files](#log-files)
    - [Setting Up PHP](#setting-up-php)
    - [TLS Certs](#tls-certs)
    - [CLI](#cli)
    - [Troubleshooting](#troubleshooting)
- [For Developers](#for-developers)
    - [Windows Builds](#windows-builds)
    - [Linux Builds](#linux-builds)
    - [Compatibility](#compatibility)
    - [Making Releases](#making-releases)
    - [Testing Suite](#testing-suite)
- [Privacy Commitment](#privacy-commitment)
- [Contributing](#contributing)
- [Changelog](#changelog)
- [Credits](#credits)
- [Fun Facts](#fun-facts)
- [Support Mercury](#support-mercury)

## About

Mercury is a lightweight, configurable HTTP server made in C++ for Windows and Linux\*.

\* Linux copies of Mercury require glibc locally installed and support distros with APT, Pacman, or DNF.

### Performance

Mercury is a lighter-weight alternative to popular HTTP servers like Apache:

#### Linux

|                  | Mercury      | Apache   | Comparison                  |
|------------------|--------------|----------|:---------------------------:|
| Avg. Idle Memory | **9216 KB**  | 21906 KB | **Mercury is 58% lighter!** |
| Avg. Load Memory | **27914 KB** | 31198 KB | **Mercury is 11% lighter!** |

#### Windows

|                  | Mercury      | Apache   | Comparison                  |
|------------------|--------------|----------|:---------------------------:|
| Avg. Idle Memory | **16320 KB** | 30026 KB | **Mercury is 46% lighter!** |
| Avg. Load Memory | **20331 KB** | 32658 KB | **Mercury is 38% lighter!** |

*Note: tests were conducted over IPv4 & IPv6 connections w/ and w/o SSL (using Mercury v0.22.2).
Each value is the average of five separate trials using the Python test cases in the Mercury repository under `tests`.*

*Actual performance will vary between systems; this data is a baseline.*

## Getting Started

Once you've downloaded your own Mercury release, navigate to the `bin/` directory.
From the terminal, start `mercury` on Linux or `mercury.exe` on Windows.

Mercury takes only a single, optional command-line argument, which is the path to a config file if you'd like to use one instead of `conf/mercury.conf`.
This file must be relative to the Mercury project root or an absolute file.

A full list of Mercury versions is available on my website, [wowtravis.com/mercury](https://wowtravis.com/mercury).
New versions include crucial bug fixes or security improvements, so it is encouraged that you update whenever possible.

Any version of Mercury before v1.0.0 is marked as a pre-release and is not an official release.
Therefore, bugs and security vulnerabilities are still possible.
If you encounter any unexpected behavior, please start an Issue on the [Mercury GitHub repository](https://github.com/travis-heavener/mercury/issues).

### Config Files

In the `conf` directory are two config files: "mercury.conf" for server config and "mimes.conf" for a list of supported MIME types.
The `conf/default/` directory contains default copies these files.

For guidance, [CONFIG.md](CONFIG.md) contains a thorough documentation of each configuration setting, including accepted values and usage examples.

### Log Files

Once you start Mercury, a `logs` directory will be created, with an access and error log.
- access.log: a detailed log of all server traffic that is processed, along with status information.
- error.log: any detailed error messages that the server encounters.

### Setting Up PHP

PHP is supported via CGI.

To install, start Mercury and run the `phpinit` command.

Developer environments will automatically install php-cgi when running `make lib_deps` or `make libs`.

Windows users have the option to use their own PHP installation instead by modifying the path to php-cgi in "mercury.conf" under the WinPHPCGIPath node.

**Note: By default, in "mercury.conf" PHP support is disabled. Enable PHP by changing the value of the EnablePHPCGI node to on.**

### TLS Certs

Self-signed TLS 1.3 certs are now available with OpenSSL.

#### For Linux:

OpenSSL comes installed on most Linux distributions, *however* `conf/ssl/makecert.sh` will verify that OpenSSL is actually installed and will attempt to install it if missing.

1. Run `conf/ssl/makecert.sh` and enter the requested information to fill out the certificate.

#### For Windows:

1. Download [OpenSSL for Windows](https://slproweb.com/products/Win32OpenSSL.html) if not already installed.
Remember the install location for OpenSSL.

2. Update the `$OPENSSL_PATH` variable in `conf/ssl/makecert.ps1` with your correct path if needed.
If you did not change the install location for OpenSSL then you will likely not need to make any changes.

3. Run `conf/ssl/makecert.ps1` and enter the requested information to fill out the certificate.

#### For Development:

In your Linux environment, use `make cert` in the root directory of this project and enter your information to create a new certificate pair.
Your certificate will be located at `conf/ssl/cert.pem` and your private key at `conf/ssl/key.pem`.

**Note: By default, in "mercury.conf" TLS is disabled. Enable TLS by changing the value of the TLSPort node to a port like 443.**

### CLI

Mercury exposes a CLI to the user with the following commands:

| Command | Description                 |
|---------|-----------------------------|
| clear   | Clears the terminal window  |
| donate  | Shows optional donation URL |
| exit    | Exit Mercury                |
| help    | List available commands     |
| info    | View current utilization    |
| phpinit | Downloads & configures PHP  |
| ping    | ???                         |
| status  | See "info"                  |

### Troubleshooting

#### Linux Executables

Linux executables are compiled from the "ubuntu-22.04" GitHub Actions runner and use an older version of glibc (2.35).
Since Mercury does not statically link against glibc, the program depends on the glibc version available at runtime.

If you receive crashes (e.g. SIGFPE __libc_early_init /usr/bin/libc.so.6), you will need to build from the Mercury source.
See [For Developers](#for-developers) for instructions on how to do build Mercury.

#### Windows Executables

Windows executable of Mercury are blocked by default from running on client devices.

If you encounter a message along the lines of:
> Microsoft Defender SmartScreen prevented an unrecognized app from starting. Running this app might put your PC at risk.

Select "More info" and then "Run anyway".

#### Powershell Scripts

On Windows, there are two Powershell scripts bundled with each release:

1. PHP installer for Windows (`conf/setup_php.ps1`)

2. TLS certificate maker (`conf/ssl/makecert.ps1`)

The script for installing PHP for Windows should be run through the Mercury CLI via the `phpinit` command, otherwise it will be blocked from execution.

For either of the two scripts, if you attempt to run them manually and receive an error that it's blocked from execution, paste the following into your terminal window to temporarily bypass the Powershell execution policy:

```
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
```

#### Further Troubleshooting

If you encounter any other issues or unexpected behavior, please consider opening an Issue on the [Mercury GitHub repository](https://github.com/travis-heavener/mercury/issues/new/choose).

## For Developers

Currently, the Mercury development environment is only available on Linux environments that support APT, Pacman, or DNF.
**Windows is not recognized as a development environment and as such the following steps will not work as intended (if at all).**

After cloning the repository, paste the following into your terminal to configure the development environment:

```bash
make clean      # Initialize the environment
make lib_deps   # Install dependency packages
make libs -j    # Build static library dependencies
```

Run `make -j` to build for Windows and Linux or refer to the following sections to build for your specific platform.

Binaries are placed in the `bin/` directory, with `bin/mercury` for Linux and `bin/mercury.exe` for Windows.

### Windows Builds

Run `make windows`.

### Linux Builds

Run `make linux`.

If you're interested in *only* building for Linux, you can set `LINUX_ONLY=1` when running `make libs -j` to prevent building Windows library dependencies.

#### Linux: Alternative Compilers

If you'd like to use a different C++ compiler for Linux builds, set the environment variable `CXX` before running `make linux` or alike.

Example:
```bash
CXX=clang++ make linux
```

Note: currently, only `g++` and `clang++` are supported.

### Compatibility

All releases are currently built using the "ubuntu-22.04" GitHub Actions runner.
All libraries are statically linked except for glibc, which uses the version of it installed on your Linux machine.

The following table contains known compatible versions of software used to build Mercury.

| Name      | Version    |
|-----------|------------|
| g++       | 11.4.0     |
| mingw-w64 | 8.0.0      |

### Making Releases

To build a release, manually dispatch the "Make Release" GitHub Action to build the binaries, test them, and push the release to the downloads website.

While a release can be manually made locally (via `make release`), this process is now automated and should only be done by dispatching this workflow.

**NOTE**: the "Make Release" workflow will take the most recent changes on main and bundle them with the version committed to main.
Do not build a release from a work-in-progress branch.

### Testing Suite

This project has its own Python test script that manages its own config and test files.
The test runner is available in the `tests` directory.

1. Using Python 3, make sure that the following Python packages are installed using your package manager of choice:
    - `brotli`
    - `zstandard`
    - `psutil`

2. With Python and its dependencies installed, run `make cert` to create a TLS cert key pair.

3. Now, start Mercury once for yourself and run the `phpinit` CLI command to configure the PHP-CGI.

4. Once your TLS certs and PHP are configured, start `tests/run.py` to run a number of tests against the server.

**NOTE:** Make sure that Mercury is ***NOT*** running when you start the test script--the script will launch several versions of Mercury to test against, but will not overwrite your configuration settings.

### Docker & Dockerfile

Because Docker installation methods vary wildly between Linux distributions, it does not get installed during the `make lib_deps` step.

Refer to the [DockerDocs](https://docs.docker.com/engine/install/) for your specific distro's install guide.

If you have Docker installed, you can use the `make docker_tests` recipe to run the Python test script against a number of Linux distributions.

## Privacy Commitment

As netizens (*noun*. a user of the internet), it is our due diligence to be aware of our presence online and to keep our personal information secure.

**Mercury does not and will never collect any of your personal information.**

The ***only*** outgoing connections ***ever made*** from Mercury are to my personal website ([wowtravis.com](https://wowtravis.com/)) to check for the latest version (a process which can be disabled in mercury.conf) and to [php.net](https://php.net) on Windows when running the `phpinit` CLI command.

In addition, the Mercury access and error logs (logs/access.log and logs/error.log) record all incoming HTTP traffic including client IPs **UNLESS** the RedactLogIPs config variable is set to true (see mercury.conf).
Because of this, deployments of Mercury may keep track of client IPs however and if they choose, but the Mercury project itself does not collect this information.

All of Mercury's source code is freely available for curious users to inspect on GitHub via [https://github.com/travis-heavener/mercury](https://github.com/travis-heavener/mercury).

That being said, Mercury is a living software, and security patches are rolled out alongside feature updates.
As such, any update prefixed with v0.X.X (ex. v0.18.2) are pre-release.
These pre-release versions are not guaranteed to be bug-proof (nor should any software ever claim to be bug-proof).

If you notice any security issues or have a suggestion, please refer to [SECURITY.md](SECURITY.md).

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md)

## Changelog
See [CHANGELOG.md](CHANGELOG.md)

## Credits
See [CREDITS.md](CREDITS.md)

## Fun Facts

- Mercury was initially named "Mars", however the name was changed to "Mercury" before the first version was released.
- The project itself was nearly abandoned after v0.2.7, however more bugs became apparent and I became more invested in Mercury.
- Mercury almost had a CGI-powered Node JS extension for serving non-static content, however it was scrapped and PHP support was later added (see [Branch: archive/node-driver](https://github.com/travis-heavener/mercury/tree/archive/node-driver)).

## Support Mercury

If Mercury has helped you in any way, be sure to star it on GitHub!

[![GitHub Repo stars](https://img.shields.io/github/stars/travis-heavener/mercury?style=flat&label=Stars&color=orange)](https://github.com/travis-heavener/mercury)

And, if you are a Mercury superfan, consider supporting me directly!

Contributions play a critical role in covering hosting costs and keeping my projects like Mercury alive.

[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/travis.heavener)

