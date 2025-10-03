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
    - [Privacy Commitment](#privacy-commitment)
- [Getting Started](#getting-started)
    - [Config Files](#config-files)
    - [Log Files](#log-files)
    - [Setting Up PHP](#setting-up-php)
    - [TLS Certs](#tls-certs)
    - [CLI](#cli)
    - [Troubleshooting](#troubleshooting)
- [Build Info](#build-info)
    - [Linux & Windows](#linux--windows)
    - [Compatibility](#compatibility)
    - [Making Releases](#making-releases)
- [Testing Suite](#testing-suite)
- [Contributing](#contributing)
- [Changelog](#changelog)
- [Credits](#credits)
- [Support Mercury](#support-mercury)

## About

Mercury is a lightweight, configurable HTTP server made in C++ for Windows and Linux\*.

\* Most (if not all) of the supported Linux distributions are for Debian (or any other distribution using APT packages).
Additionally, most of these distributions come with glibc, which is required to be locally installed for Mercury to run.

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

*Note: tests were conducted over IPv4 & IPv6 connections w/ and w/o SSL.
Each value is the average of five separate trials using the Python test cases in the Mercury repository under `tests`.*

*Actual performance will vary between systems; this data is a baseline.*

### Privacy Commitment

As netizens (*noun*. a user of the internet), it is our due diligence to be aware of our presence online and to keep our personal information secure.

**Mercury does not and will never collect any of your personal information.**

That is the bottom line.

The ***only*** outgoing connections ***ever made*** from Mercury are to my personal website ([wowtravis.com](https://wowtravis.com/)) to check for the latest version (a process which can be disabled in mercury.conf).

In addition, the Mercury log access and error logs (logs/access.log and logs/error.log) record all incoming HTTP traffic including client IPs **UNLESS** the RedactLogIPs config variable is set to true (see mercury.conf). Because of this, deployments of Mercury may keep track of client IPs however and if they choose, but the Mercury project itself does not collect this information.

All of Mercury's source code is freely available for curious users to view and poke at on GitHub via [https://github.com/travis-heavener/mercury](https://github.com/travis-heavener/mercury).

That being said, Mercury is a living software, and security patches are rolled out alongside feature updates. As such, any update prefixed with v0.X.X (ex. v0.18.2) are pre-release. These pre-release versions are not guaranteed to be bug-proof (nor should any software ever claim to be bug-proof).

If you notice any security issues or have a suggestion, please refer to [SECURITY.md](SECURITY.md).

## Getting Started

Once you've downloaded your own Mercury release, navigate to the `/bin/` directory.
If running on Linux, start `mercury` from the terminal; if running on Windows, start `mercury.exe`.

A full list of Mercury versions is available on our website, [wowtravis.com/mercury](https://wowtravis.com/mercruy).
If you start up Mercury and are met with an "Update available!" notification, navigate to our website to download a new copy.
New versions are continuously being produced and may include crucial bug fixes or security improvements.

Any version of Mercury marked as a "pre-release" (versions starting with "v0.x.x") is not a finalized product.
Therefore, bugs and security vulnerabilities are still possible.
If you encounter any unexpected behavior, please start an Issue on the [Mercury GitHub repository](https://github.com/travis-heavener/mercury/issues).

### Config Files

In the `conf` directory are two config files: "mercury.conf" for server config and "mimes.conf" for a list of supported MIME types.

### Log Files

Once you start Mercury, a `logs` directory will be created, with an access and error log.
The access log contains a detailed log of all server traffic that is processed, along with status information.
The error log contains any detailed error messages that the server encounters.

### Setting Up PHP

PHP is now supported via php-cgi for Windows and Linux!

To install:
- For Linux, run `sudo apt install php-cgi`.
- For Windows, run `/conf/setup_php.ps1`.

Developer environments also come with php-cgi installed after running `make lib_deps` or `make libs`.

Windows users have the option to use their own PHP installation instead by modifying the path to php-cgi in "mercury.conf" under the WinPHPCGIPath node.

**Note: By default, in "mercury.conf" PHP support is disabled. Enable PHP by changing the value of the EnablePHPCGI node to on.**

### TLS Certs

Self-signed TLS 1.3 certs are now available with OpenSSL.

#### For Linux:

1. Run `./conf/ssl/makecert.sh` and enter the following information to fill out the certificate.

#### For Windows:

1. Download [Git for Windows](https://git-scm.com/downloads/win) if not already installed.
It's crucial that Git is installed since it comes bundled with OpenSSL.

2. Double check the install location of Git. OpenSSL should be installed in the `<Git location>/usr/bin` directory.
Update the `$OPENSSL_PATH` variable in `/conf/ssl/makecert.ps1` with your correct path if needed.

3. Run `/conf/ssl/makecert.ps1` and enter the following information to fill out the certificate.

#### For Developers:

In your Linux/Debian environment, use `make cert` in the root directory of this project and enter your information to automatically create a new certificate pair.
Your certificate will be located at `/conf/ssl/cert.pem` and your private key at `/conf/ssl/key.pem`.

**Note: By default, in "mercury.conf" TLS is disabled. Enable TLS by changing the value of the TLSPort node to a port like 443.**

### CLI

As of Mercury v0.22.0, a basic CLI is available to the user. Here is a list of available commands:

| Command | Description |
|-----|-----|
| exit | Exit Mercury |
| help | List available commands |
| info | View current utilization |
| ping | ??? |

### Troubleshooting

#### Windows Executables

Windows executable of Mercury are blocked by default from running on client devices.

If you encounter a message along the lines of:
> Microsoft Defender SmartScreen prevented an unrecognized app from starting. Running this app might put your PC at risk.

Select "More info" and then "Run anyway".

#### Powershell Scripts

Powershell scripts are blocked by default from running on client devices.
If you encounter:
> Do you want to open this file?

Select "Open".

If you still encounter issues running a Powershell script, open a new Powershell terminal and enter:
```
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

Now run the Powershell script from that same Powershell terminal.
The above `Set-ExecutionPolicy` will temporarily allow the Powershell scripts to run while the terminal is open, and will reset to the system default policy when closed.

#### Further Troubleshooting

If you encounter any other issues or unexpected behavior, please consider opening an Issue on the [Mercury GitHub repository](https://github.com/travis-heavener/mercury/issues/new/choose).

## Build Info

The `/build_tools/` directory contains all necessary shell scripts for building static binaries.

Binaries are placed in the `/bin/` directory, `mercury` for Linux and `mercury.exe` for Windows.

### Linux & Windows

To build for Linux and/or Windows, use the following steps:

1. Install all necessary dependencies & extract static libraries.

    `make libs`.

2. Using GNU Make, build for your desired platform(s) via `make linux`, `make windows`, or `make all`.

Note: see [Compatibility](#compatibility) section for software compatibility.

### Compatibility

The following table contains known compatible versions of important software used to build Mercury.
Older mingw-w64 versions have introduced issues when binding IPv6 sockets with WinAPI.
The g++ version restriction is likely less crucial, as initially version 11.x.x was in use and was only upgraded as a side effect of an OS upgrade (Ubuntu 24.04 LTS from 22.04 LTS).
Thus, almost any Linux environment with the following g++ and mingw-w64 versions should be sufficient for building Mercury.
However, it's important to note that APT and glibc must both be present on development and client Linux devices (hence why most Debian systems are supported).

| Name      | Version    |
|-----------|------------|
| g++       | 13.3.0     |
| mingw-w64 | 11.0.1     |

### Making Releases

To build a release, manually dispatch the "Make Release" GitHub Action to automatically build the binaries, test them, then bundle the release and push it to the downloads website.

While a release can be manually made locally (via `make release`), this process is now automated and should only be done by dispatching this workflow.

**NOTE**: the "Make Release" workflow will take the most recent changes on main and bundle them with the version committed to main. If you are building a release from a work-in-progress branch, *don't*.
Please refrain from building a release from commits not yet pushed to main--any such releases will be removed and your contributorship will be reconsidered.

## Testing Suite

This project has its own Python test runner complete with passes for IPv4 & IPv6 traffic with and without TLS enabled.
The test runner is available in the `tests` directory.

With Python installed, fire up the Mercury server, cd into the tests directory, and use the `run.py` file to run a number of tests against the server.

Note that in mercury.conf, the DocumentRoot should point to `./tests/files/` as the testing suite has its own set of documents to run tests against.

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md)

## Changelog
See [CHANGELOG.md](CHANGELOG.md)

## Credits
See [CREDITS.md](CREDITS.md)

## Support Mercury

If Mercury has helped you in any way, be sure to star it on GitHub!

[![GitHub Repo stars](https://img.shields.io/github/stars/travis-heavener/mercury?style=flat&label=Stars&color=orange)](https://github.com/travis-heavener/mercury)

And, if you are a Mercury superfan, consider supporting me directly!

Contributions play a critical role in covering hosting costs and keeping my projects like Mercury alive.

[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/travis.heavener)