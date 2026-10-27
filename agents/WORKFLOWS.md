# Developer & Agent Workflows

This document outlines the standard operational workflows, build systems, debugging procedures, release processes, and agent-specific guidelines for Mercury.

---

## 1. Prerequisites & Environment Setup

Mercury is developed primarily on Linux (Ubuntu, Debian, Fedora, Arch). Windows builds are produced via MinGW-w64 cross-compilation from Linux.

### Required Packages
- **Build Tools**: `g++` (v11+ with C++20 support) or `clang++`, `make`, `nasm`, `upx-ucl` (or `upx`).
- **Windows Cross-Compiler**: `mingw-w64` (`x86_64-w64-mingw32-g++` and `x86_64-w64-mingw32-windres`).
- **Runtime & CGI**: `php-cgi` (must be on `$PATH`).
- **Python Test Suite**: Python 3.10+ with `brotli`, `zstandard`, and `psutil`:
  ```bash
  pip install brotli zstandard psutil
  ```
- **Security & Certs**: `openssl` (CLI and library headers).

### Initializing Environment
To bootstrap a clean development environment with all static library dependencies:
```bash
make clean      # Clean previous build artifacts
make lib_deps   # Install distro build dependencies via build_tools/install_deps.sh
make libs -j    # Build static third-party libraries (OpenSSL, Brotli, Zstd, Zlib, PugiXML)
```
> [!NOTE]
> If developing strictly for Linux, you can pass `LINUX_ONLY=1 make libs -j` to bypass building Windows library dependencies.

---

## 2. Build Recipes & Compilation

The project uses a root [`Makefile`](../Makefile) with strict compiler warnings enabled (`-Wall -Wextra -Werror -std=c++20`).

| Target / Command | Description | Artifacts |
|---|---|---|
| `make linux` | Builds optimized Linux executable | `bin/mercury` |
| `make windows` | Cross-compiles Windows executable with icon resource | `bin/mercury.exe` |
| `make all` | Builds both Linux and Windows targets | `bin/mercury`, `bin/mercury.exe` |
| `DEBUG=1 make -j -B` | Builds in debug mode (`-g -O0`, UPX compression skipped) | `bin/mercury` |
| `CXX=clang++ make linux` | Builds Linux target using Clang | `bin/mercury` |
| `make cert` | Generates 365-day self-signed TLS 1.3 keypair | `conf/ssl/cert.pem`, `conf/ssl/key.pem` |
| `make clean` | Removes build artifacts, object files, and logs | `bin/`, `tmp/`, `logs/` |
| `make docker_tests` | Executes test suite across multiple Docker container distros | Multi-distro verification |

### UPX Executable Compression
Release builds automatically run `upx -qqq` on compiled binaries to minimize binary footprint.
> [!IMPORTANT]
> When debugging with `gdb`, profiling, or analyzing crashes, **always build with `DEBUG=1`** (`DEBUG=1 make -j -B`). UPX packing obfuscates symbols and prevents debugger attachment.

---

## 3. Running Mercury Locally

### Launching the Server
By default, Mercury looks for configuration in `conf/mercury.conf`:
```bash
./bin/mercury
```

To run with an alternate or test configuration:
```bash
./bin/mercury -c tests/conf_files/normal.conf
# or: ./bin/mercury --config /path/to/custom.conf
```

To display command-line options and help:
```bash
./bin/mercury --help
```

### Interactive CLI Commands
When running interactively in a terminal, Mercury exposes a command-line interface on standard input:

| Command | Action |
|---|---|
| `help` | Displays available CLI commands |
| `info` / `status` | Reports active thread pool usage and queued connection metrics |
| `pwd` | Prints current document root path |
| `ping` | Verifies server responsiveness (`Pong!`) |
| `clear` | Clears terminal screen |
| `donate` | Displays donation link |
| `phpinit` | Installs / configures system `php-cgi` |
| `exit` | Initiates graceful server shutdown |

---

## 4. Debugging & Diagnostics

### GDB & Core Dumps
Compile with debug symbols and run under `gdb`:
```bash
DEBUG=1 make -B linux
gdb --args ./bin/mercury conf/mercury.conf
```

Useful GDB breakpoints:
- `http::Server::handleReqs`: Catch connection processing entry.
- `http::version::handler_1_1::genResponse`: Inspect request routing, access checks, and headers.
- `http::Response::streamBody`: Debug response chunking and socket writes.
- `http::cgi::handlePHPRequest`: Inspect CGI process spawning and pipe communication.

### Memory Leak & Thread Sanitizer Checks
To compile with LLVM sanitizers:
```bash
CXX="clang++ -fsanitize=address,undefined -fno-omit-frame-pointer" DEBUG=1 make -B linux
```

### Server Logs
Mercury maintains two dedicated log files in `logs/`:
- `logs/access.log`: Records all processed HTTP transactions, client IP (masked/hashed according to `ClientSecurityMode`), method, URI, status code, and HTTP version.
- `logs/error.log`: Records socket failures, TLS errors, CGI execution failures, and internal error states.

---

## 5. Testing Workflow

The test harness runs both JSON-defined functional tests and raw-socket protocol regression tests:

```bash
# 1. Ensure test certificates are generated
make cert

# 2. Run the full test suite
python3 tests/run.py
```

See [`agents/TESTING.md`](TESTING.md) for complete details on authoring test cases, assertion structures, and raw wire regressions.

---

## 6. Performance Benchmarking Workflow

Run `.github/workflows/benchmark.yml` manually through Actions → Performance Benchmark. `release_tag` accepts a published `vX.Y.Z` tag, `latest` (newest published release including prereleases), or `build`. The workflow resolves the release once so Linux and Windows use the same target. Both jobs use Python 3.12 and `psutil`, acquire native server binaries, initialize the ignored MIME runtime config, and run benchmark regression checks before measurements.

For `build`, a separate Ubuntu 22.04 job builds both Linux and Windows executables using the repository's existing MinGW cross-compilation toolchain and shares them with both benchmark jobs. Native Windows does not run the Linux-oriented Makefile. Release runs skip compilation.

`trials`, `duration`, and `servers` are validated before provisioning benchmark hosts. Inputs reach shell commands through environment variables. Linux prefers `wrk`; Windows falls back to Python unless an external tool is available. Throughput from different generators should not be compared across platforms.

The runner derives threaded worker limits from client concurrency (60 workers for the default 50 clients), and records the limit in JSON and Markdown. Native regression checks exercise 50 persistent clients before benchmark trials. Benchmark HTML uses LF line endings on both platforms.

Each platform publishes its Markdown table to the step summary and uploads JSON, startup/error logs, and generated configs. Diagnostic uploads run after failures too. If a startup check fails, inspect `<server>_startup.log` in the platform result artifact. Missing `conf/mimes.conf` is repaired from tracked defaults without replacing an existing file.

With `create_pr=true`, the PR job explicitly checks that release resolution and both benchmark jobs succeeded, and that the run was not cancelled. An expected skip of the optional source-build job does not block successful release results. A failed, skipped, or cancelled benchmark prevents all PR-job steps from running. Dispatch from a branch for PR creation; that selected branch is the PR base. The aggregation job downloads both reports, replaces only README's Performance section, checks tracked-file scope, and pushes a unique run-specific branch without force-pushing. It uses the configured Mercury App token when available, otherwise the workflow token. Repository settings must permit Actions to create pull requests when using the workflow token. `version.txt` and `CHANGELOG.md` are not updated.

For local runs and measurement caveats, see [TESTING.md](TESTING.md#7-performance-benchmark-suite-build_toolsbenchmarkpy).

```bash
python3 build_tools/benchmark.py --servers mercury --trials 3 --duration 5
python3 build_tools/benchmark.py --output-format markdown,json --output results.md
python3 -m unittest discover -s tests -p test_benchmark.py -v
```

## 7. Release & Version Management

Mercury maintains strict version alignment across files.

### Version Synchronization Rule
When cutting a new release or preparing version bumps:
1. `version.txt` must contain the exact string `Mercury vX.Y.Z` without a trailing newline.
2. `CHANGELOG.md` must have a corresponding top header `## [X.Y.Z] - YYYY-MM-DD`.
3. The CI workflow `.github/workflows/compare-changelog-version.yml` validates this equivalence:
   ```bash
   CHANGELOG_VERSION=$( cat CHANGELOG.md | grep -Po "(?<=^## \[)\d+\.\d+\.\d+(?=\] - \d{4}-\d{2}-\d{2})" | head -1 )
   STORED_VERSION=$( cat version.txt | head -n 1 | tr -d '\n' | grep -Po "(?<=^Mercury v)\d+\.\d+\.\d+$" | head -1 )
   [ "$CHANGELOG_VERSION" = "$STORED_VERSION" ]
   ```

### Release Pipeline
- Releases are automated via GitHub Actions (`.github/workflows/release-maker.yml`).
- Dispatching "Make Release" on `main` packages cached Linux and Windows binaries, generates release notes using `build_tools/release_readme_maker.py`, and publishes artifacts to GitHub and the Mercury release website.

---

## 8. Crucial Guidelines & Gotchas for AI Agents

1. **Strict Conventions Adherence**:
   - Follow all conventions detailed in [`agents/CONVENTIONS.md`](CONVENTIONS.md).
   - Use 4-space indentation across C++, Python, HTML, and CSS (tabs strictly for Makefile recipes).
   - Telegraphic comments starting with uppercase, omitting terminal periods on single lines, leading with imperative action verbs (`// Bind socket`, `# Parse headers`).
   - Pointer naming prefix: use `p` prefix for raw or smart pointers (`pSSL`, `pResponse`, `pServer`).
2. **Socket Lifetime & Thread Safety**:
   - Never close client sockets without untracking them (`untrackClient`) under `clientsMutex`.
   - Blocked readers must be awakened with `shutdown()` before worker threads are joined.
   - When binding server sockets in loops, remember kernel `TIME_WAIT` on loopback can temporarily hold ports (especially IPv6).
3. **No Dynamic Memory Leaks**:
   - Prefer RAII types (`std::unique_ptr`, `std::shared_ptr`, `std::vector`, `std::string`).
   - OpenSSL allocations (`SSL*`, `SSL_CTX*`, `BIO*`) must have matching cleanup calls (`SSL_free`, `SSL_CTX_free`).
4. **Binary & Pipeline Safety**:
   - HTTP/1.1 request pipelining requires that extra unread bytes in the socket buffer are preserved for the subsequent request.
   - Never assume request bodies or CGI output are null-terminated ASCII/UTF-8; preserve binary data and handle zero bytes (`\0`).
5. **Security Boundaries**:
   - Never bypass canonical path verification (`isInDocumentRoot`).
   - Never allow unescaped CRLF (`\r\n`) in header names or values to prevent HTTP response splitting.

