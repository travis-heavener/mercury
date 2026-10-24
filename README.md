<div align="center">
    <img src="website/og-image.jpg" width="300" height="158" alt="Mercury Banner">
    <br>
    <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License"></a>
    <img src="https://img.shields.io/badge/Language-C%2B%2B20-blue" alt="C++20">
    <img src="https://img.shields.io/github/downloads/travis-heavener/mercury/total?style=flat&color=orange&label=Downloads" alt="Downloads">
    <img src="https://img.shields.io/github/stars/travis-heavener/mercury?style=flat&label=Stars&color=orange" alt="Stars">
    <br>
    <h1>Mercury</h1>
    <a href="https://github.com/travis-heavener/mercury/actions/workflows/build.yml"><img src="https://github.com/travis-heavener/mercury/actions/workflows/build.yml/badge.svg" alt="Build Status"></a>
    <a href="https://github.com/travis-heavener/mercury/actions/workflows/website-deploy.yml"><img src="https://github.com/travis-heavener/mercury/actions/workflows/website-deploy.yml/badge.svg" alt="Deploy Status"></a>
    <h3><em>A lightweight, high-performance HTTP server for Linux and Windows</em></h3>
    <p>Project by <a href="https://github.com/travis-heavener">Travis Heavener</a> &bull; <a href="https://mercury.wowtravis.com">mercury.wowtravis.com</a></p>
</div>

---

Mercury is a lightweight, configurable HTTP web server implemented in modern C++20. Designed as an efficient alternative to heavyweight servers like Apache, it combines low memory utilization with modern protocol features, streaming compression, dynamic CGI scripting, and robust security defaults.

## Key Features

- **Blazing Fast & Low Memory**: Uses up to 58% less idle memory than Apache with sub-millisecond static response times.
- **Protocol Support**: Full HTTP/1.1 (keep-alive, pipelining, chunked transfer encoding) with backward-compatible HTTP/1.0 and HTTP/0.9 handlers.
- **Modern TLS**: TLS 1.3 encryption powered by OpenSSL with self-signed certificate automation.
- **Streaming Compression**: Dynamic, on-the-fly compression supporting **Brotli**, **Zstandard**, **Gzip**, and **Deflate**.
- **PHP FastCGI & CGI**: Persistent FastCGI gateway communicating directly with PHP-FPM (Linux) or persistent `php-cgi` (Windows), with zero disk I/O and legacy RFC 3875 CGI fallback.
- **Flexible XML Configuration**: Declarative virtual host rules, regex URL rewrites, HTTP redirects (301/302/308), CIDR/IP access control lists, and custom status pages.
- **Privacy First**: Zero telemetry, no external calls, and built-in DNT/Sec-GPC client IP bit-masking and hashing.
- **Privilege Separation**: Safely drops root privileges when started via `sudo` or `doas` after binding privileged ports, preserving invoking user ownership on logs and temp files.
- **Cross-Platform**: Compiles natively on Linux and cross-compiles for Windows with zero external runtime DLL dependencies.

---

## Table of Contents

- [About](#about)
- [Performance](#performance)
- [Getting Started](#getting-started)
    - [Configuration](#config-files)
    - [Logging](#log-files)
    - [Setting Up PHP](#setting-up-php)
    - [TLS Certificates](#tls-certs)
    - [Interactive CLI](#cli)
    - [Updating Mercury](#updating-mercury)
    - [Troubleshooting](#troubleshooting)
- [For Developers](#for-developers)
- [Privacy Commitment](#privacy-commitment)
- [Contributing](#contributing)
- [License](#license)
- [Support Mercury](#support-mercury)

---

## About

### Performance

Mercury is significantly lighter on system resources compared to traditional web servers:

#### Linux (Ubuntu 22.04)

| Metric | Mercury | Apache | Comparison |
|---|---|---|:---:|
| **Avg. Idle Memory** | **9.2 MB** | 21.9 MB | **Mercury is 58% lighter** |
| **Avg. Load Memory** | **27.9 MB** | 31.2 MB | **Mercury is 11% lighter** |

#### Windows (x64)

| Metric | Mercury | Apache | Comparison |
|---|---|---|:---:|
| **Avg. Idle Memory** | **16.3 MB** | 30.0 MB | **Mercury is 46% lighter** |
| **Avg. Load Memory** | **20.3 MB** | 32.7 MB | **Mercury is 38% lighter** |

*Baseline averages across 5 trials over IPv4 and IPv6 connections using the test suite.*

---

## Getting Started

### Running Mercury

Download the latest release archive from [mercury.wowtravis.com](https://mercury.wowtravis.com) or the [Releases](https://github.com/travis-heavener/mercury/releases) page.

- **Linux**: `./bin/mercury [options]`
- **Windows**: `bin\mercury.exe [options]`

Common options:
- `-c, --config <file>`: Specify path to configuration file (defaults to `conf/mercury.conf`)
- `-h, --help`: Display usage and command-line options
- `-p, --port <port>`: Override HTTP port
- `-r, --root <path>`: Override document root directory
- `-t, --test`: Test configuration file syntax and exit
- `-v, --version`: Display version information

> [!NOTE]
> All versions before `v1.0.0` are pre-releases. Pre-release updates may introduce breaking changes or configuration adjustments between versions, so be sure to consult [`CHANGELOG.md`](CHANGELOG.md) when upgrading. If you encounter any unexpected behavior, please [open an issue](https://github.com/travis-heavener/mercury/issues).

### Config Files

- `conf/mercury.conf`: Primary server configuration (ports, document root, security modes, match rules, rewrite/redirect directives).
- `conf/mimes.conf`: MIME type mapping table.
- Default templates are located in `conf/default/`.

> [!TIP]
> Always review [`CONFIG.md`](CONFIG.md) when setting up configuration files—it provides thorough documentation of each configuration setting, including accepted values and usage examples. If you use hashed IP logging (`GPCHashed` or `Hashed`), remember to change the `ClientSecurityIPSalt` node in `conf/mercury.conf` to a unique secret string to properly salt anonymous GPC hash entries.

### Log Files

Mercury automatically creates a `logs/` directory upon startup:
- `logs/access.log`: Processed requests with client IP, timestamp, method, path, status, and HTTP version.
- `logs/error.log`: Diagnostic, socket, and CGI error logs.

### Setting Up PHP

Mercury supports PHP via persistent **FastCGI** (recommended) and traditional **CGI**:

- **FastCGI / PHP-FPM (Recommended)**:
  1. In `conf/mercury.conf`, set `<EnableFastCGI> on </EnableFastCGI>`.
  2. Mercury automatically auto-detects the newest installed PHP-FPM UNIX domain socket on Linux (`/run/php/*.sock`) or connects to `127.0.0.1:9000` on Windows.
  3. Custom sockets or addresses can be configured via `<FastCGIAddress>` (e.g. `unix:/run/php/php8.3-fpm.sock` or `127.0.0.1:9000`).
  4. On Windows, run `php\start_fastcgi.bat` or `php\php-cgi.exe -b 127.0.0.1:9000`.

- **Legacy PHP-CGI**:
  1. Start Mercury and type `php:init` in the interactive console to automatically download and configure `php-cgi`.
  2. In `conf/mercury.conf`, set `<EnablePHPCGI> on </EnablePHPCGI>`.

### TLS Certs

Self-signed TLS 1.3 certificates can be generated with a single command:
- **Linux**: Run `conf/ssl/makecert.sh` (or `make cert` in the source repository).
- **Windows**: Run `conf/ssl/makecert.ps1` (ensure OpenSSL is installed).
- Certificates are saved to `conf/ssl/cert.pem` and `conf/ssl/key.pem`.
- Enable TLS by setting `<TLSPort> 443 </TLSPort>` (or another port) in `conf/mercury.conf`.

### CLI

Mercury provides an interactive terminal command interface:

| Command | Action |
|---|---|
| `help` | List available commands |
| `info` / `status` | View active thread pool usage and connection queue |
| `pwd` | Print document root directory |
| `ping` | Healthcheck response (`Pong!`) |
| `phpinit` | Download and configure PHP-CGI |
| `clear` | Clear terminal output |
| `donate` | Display optional donation link |
| `exit` | Gracefully shut down the server |

### Updating Mercury

While Python is not required to run Mercury, users with Python 3 installed can use the included update script for automated, rollback-safe upgrades:

```bash
# Linux
python3 conf/update.py

# Windows
python conf\update.py
```

The updater safely stages the latest release in an isolated temporary directory, creates a timestamped backup of your configuration (`conf/mercury.conf.bak.<timestamp>`), reconciles any newly introduced XML settings without overwriting your custom ports, rules, or security salts, validates the updated binary syntax w/ `-t`, and automatically rolls back if syntax verification fails.

### Troubleshooting

- **Linux (glibc mismatch)**: Prebuilt binaries are compiled against glibc 2.35 (Ubuntu 22.04). If you encounter `SIGFPE` or dynamic loader errors on older distributions, build from source via `make linux`.
- **Windows SmartScreen**: Select **More info** $\rightarrow$ **Run anyway** if prompted by Microsoft Defender SmartScreen on unsigned binaries.
- **PowerShell Execution Policy**: If `.ps1` scripts are blocked on Windows, run:
  ```powershell
  Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
  ```
- **Port Conflicts**: Ensure ports 80/8080 and 443/8081 are not already bound by another web server.
- **Configuration & XML Errors**: If Mercury fails to start due to a malformed configuration file, it pinpoints the exact XML syntax error along with line and column numbers. Ensure all opening tags match their closing tags and that required configuration directives are present.

---

## For Developers

Developers and automated agents should reference [`AGENTS.md`](AGENTS.md) and the [`agents/`](agents/) directory for architecture breakdowns, testing specs, and engineering conventions.

### Quick Build Commands

```bash
make clean      # Clean previous build artifacts
make lib_deps   # Install build dependencies (APT, Pacman, DNF)
make libs -j    # Build static dependencies (OpenSSL, Brotli, Zstd, Zlib, PugiXML)
make linux      # Compile bin/mercury
make windows    # Cross-compile bin/mercury.exe (MinGW-w64)
```

- **Debug Builds**: Run `DEBUG=1 make -j -B` to compile with `-g -O0` and bypass UPX binary compression.
- **Running Tests**: Run `python3 tests/run.py` to execute the JSON test cases and socket wire regressions (requires `brotli`, `zstandard`, `psutil`, and generated TLS certs via `make cert`).
- **Multi-Distro Testing**: Run `make docker_tests` to execute tests across Ubuntu, Debian, Alpine, Fedora, and Arch containers.

See [`agents/WORKFLOWS.md`](agents/WORKFLOWS.md) and [`agents/TESTING.md`](agents/TESTING.md) for complete runbooks.

---

## Privacy Commitment

Mercury values user privacy and adheres to strict standards:
- **Zero Telemetry**: Mercury never collects personal data, telemetry, or analytics.
- **Minimal Network Calls**: The only outbound connection is an optional startup version check to `mercury.wowtravis.com` (which can be disabled via `<StartupCheckLatestRelease> off </StartupCheckLatestRelease>`), plus `php.net` during Windows `phpinit`.
- **DNT / Sec-GPC Compliance**: Client IPs in `access.log` can be automatically bit-masked or salted/hashed when `Do-Not-Track` or `Sec-GPC` headers are detected (configured via `<ClientSecurityMode>`). Review [`CONFIG.md`](CONFIG.md) for configuration guidance, and be sure to change `<ClientSecurityIPSalt>` to your own unique secret value when using hashed IP logging.

---

## Contributing

Contributions, bug reports, and pull requests are welcome! See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`agents/CONVENTIONS.md`](agents/CONVENTIONS.md) for contribution guidelines and coding standards.

---

## Changelog

See [`CHANGELOG.md`](CHANGELOG.md) for version release notes and history.

---

## Credits

See [`CREDITS.md`](CREDITS.md) for project contributors and third-party library licenses.

---

## Support Mercury

If you find Mercury helpful, consider giving it a star on [GitHub](https://github.com/travis-heavener/mercury)!

Contributions help cover hosting costs and support ongoing open-source development:

[![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://wowtravis.com/support)
