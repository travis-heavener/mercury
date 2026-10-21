# Agent Guidelines: Mercury

## Project Overview

Mercury is a lightweight, high-performance, configurable HTTP server implemented in C++20 for Linux and Windows. It provides full HTTP/1.1 support (with backward-compatible HTTP/1.0 and HTTP/0.9 handlers), TLS 1.3 encryption via OpenSSL, fast CGI execution for PHP, streaming compression (Brotli, Zstandard, Gzip, Deflate), access control, URL rewriting, and HTTP redirects.

---

## Quick Reference Commands

| Task | Command | Notes |
|---|---|---|
| **Build Linux** | `make linux` | Produces `bin/mercury` |
| **Build Windows** | `make windows` | Cross-compiles `bin/mercury.exe` using MinGW-w64 |
| **Build All** | `make all` | Builds both Linux and Windows binaries |
| **Debug Build** | `DEBUG=1 make -j -B` | Enables `-g -O0` and skips UPX compression |
| **Run Server** | `./bin/mercury [-c conf-file]` | Defaults to `conf/mercury.conf` (`--help` for options) |
| **Create TLS Certs** | `make cert` | Generates self-signed test keypair in `conf/ssl/` |
| **Run Test Suite** | `python3 tests/run.py` | Runs JSON cases and socket wire regressions |
| **Multi-Distro Tests** | `make docker_tests` | Tests across Ubuntu, Debian, Alpine, Fedora, Arch |
| **Clean Build** | `make clean` | Removes binaries, object files, and logs |

---

## Repository Map

```
Mercury/
├── AGENTS.md                 # Agent entry point and operational summary (this file)
├── agents/                   # Detailed agent guidelines and references
│   ├── CONVENTIONS.md        # Codebase conventions and engineering standards
│   ├── ARCHITECTURE.md       # Deep architectural breakdown and concurrency model
│   ├── WORKFLOWS.md          # Build, run, debug, and release runbooks
│   └── TESTING.md            # Complete test suite guide and wire regression specs
├── src/                      # C++ source code
│   ├── main.cpp              # Entry point, signals, listener dispatch, CLI loop
│   ├── winheader.hpp         # Windows Winsock and platform wrappers
│   ├── conf/                 # XML config loader (pugixml), match rules, access ACLs
│   ├── http/                 # Connection engine, parser, response, TLS, codecs
│   │   ├── version/          # Protocol handlers for HTTP/1.1, 1.0, and 0.9
│   │   └── cgi/              # RFC 3875 CGI execution engine (php-cgi)
│   ├── io/                   # File abstraction, path normalization, directory index
│   ├── logs/                 # Thread-safe access/error logging and IP anonymization
│   └── util/                 # ThreadPool, CLI parser, string manipulation, toolbox
├── conf/                     # Default and runtime configuration files
├── tests/                    # Python test runner, JSON test cases, wire regressions
├── build_tools/              # Library builders, dependency checkers, release tools
├── libs/                     # Static third-party libraries (OpenSSL, Brotli, Zstd, etc.)
└── Makefile                  # Primary build automation file
```

---

## Core Engineering & Architectural Tenets

All agents modifying this codebase must adhere to the following principles:

### 1. Code & Formatting Conventions
Adhere strictly to [`agents/CONVENTIONS.md`](agents/CONVENTIONS.md):
- **Comments**: Start with uppercase, omit terminal periods on single-line comments, use telegraphic style (drop articles *a*, *an*, *the*), and lead with imperative action verbs (`// Bind socket`, `# Parse headers`).
- **Formatting**: 4-space indentation across C++, Python, HTML, and CSS (tabs strictly for Makefile recipes). 1TBS / K&R brace placement.
- **Naming**: `snake_case` for C/Python, `PascalCase` for classes/structs, `camelCase` for C++ methods/functions. Prefix pointers with `p` (`pSSL`, `pResponse`, `pServer`). Pre-increment (`++i`) in loops.
- **Compiler Compliance**: C++20 standard (`-std=c++20`), compiling cleanly with `-Wall -Wextra -Werror`.

### 2. Socket Management & Concurrency
- **Connection Registration**: Track every client descriptor under `clientsMutex` (`trackClient`) before enqueuing to the `ThreadPool`. Always untrack (`untrackClient`) prior to socket closure.
- **Graceful Shutdown**: On shutdown, wake blocked readers using `shutdown()` before joining worker threads.
- **Loopback Cooldowns**: Rapidly binding and rebinding server sockets over loopback (especially IPv6) may encounter `EADDRINUSE` (`errno: 98`) due to kernel TCP `TIME_WAIT`.

### 3. Memory Safety & RAII
- Never leak dynamic resources. Favor modern C++ RAII wrappers (`std::unique_ptr`, `std::shared_ptr`, `std::vector`, `std::string`).
- Ensure every OpenSSL context and session structure (`SSL*`, `SSL_CTX*`) is properly paired with its corresponding cleanup call (`SSL_free`, `SSL_CTX_free`).

### 4. Binary Data & Protocol Safety
- Never assume incoming HTTP bodies or CGI payloads are null-terminated ASCII. Preserve raw byte arrays and binary payloads containing `\0`.
- Retain surplus pipelined bytes in the connection buffer across keep-alive requests.
- Header lines are limited to 64 KiB (returning 431 on overflow), and malformed or duplicate framing headers (`Content-Length`, `Transfer-Encoding`) must return 400.

### 5. Version Synchronization
- Any version change must update both `version.txt` (`Mercury vX.Y.Z`) and `CHANGELOG.md` (`## [X.Y.Z] - YYYY-MM-DD`). The CI workflow will fail if these diverge.

---

## Detailed Agent Documentation

For in-depth explanations and specialized procedures, refer to:
- [`agents/CONVENTIONS.md`](agents/CONVENTIONS.md): Complete style, commenting, indentation, and architectural idiom rules.
- [`agents/ARCHITECTURE.md`](agents/ARCHITECTURE.md): Deep dive into server internals, protocol framing, compression pipeline, CGI, and security boundaries.
- [`agents/WORKFLOWS.md`](agents/WORKFLOWS.md): Comprehensive instructions for building, running, debugging with GDB, profiling, and cutting releases.
- [`agents/TESTING.md`](agents/TESTING.md): Detailed documentation of the 4-way transport test harness, JSON test schema, and wire regression tests.

