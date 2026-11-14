# Agent Guidelines: Mercury

## Project Overview

Mercury is a high-performance HTTP server in C++20 for Linux and Windows. It supports HTTP/1.1 (with HTTP/1.0 and HTTP/0.9 fallback), TLS 1.3 via OpenSSL, CGI and FastCGI (PHP-FPM), streaming compression (Brotli, Zstandard, Gzip, Deflate), access control, rate limiting, URL rewriting, and redirects.

---

## Agent Operational Rules & Constraints

1. **Concise Communication**:
   - Provide direct, helpful responses; eliminate wordy explanations, restatements, and filler.

2. **Strict Git Boundaries**:
   - **Never stage (`git add`), commit (`git commit`), or push changes** unless explicitly instructed by the user.
   - Only execute read-only git queries (`git status`, `git diff`, `git log`). Keep all changes unstaged.

3. **Protected Files & Secrets**:
   - **Do not modify local `.conf` files** (`conf/mercury.conf`), test certificates (`conf/ssl/`), or repository credentials/secrets. Use temporary configs or harnesses for tests.

4. **Up-to-Date Documentation**:
   - Consult latest official documentation via web search for external APIs, standards, and OS behaviors (OpenSSL 3/4, POSIX, Win32, RFCs).

---

## Quick Reference Commands

| Task | Command | Notes |
|---|---|---|
| **Build Linux** | `make linux` | Produces `bin/mercury` |
| **Build Windows** | `make windows` | Cross-compiles `bin/mercury.exe` (MinGW-w64) |
| **Build All** | `make all` | Builds Linux and Windows binaries |
| **Debug Build** | `DEBUG=1 make -j -B` | Enables `-g -O0` symbols |
| **Run Server** | `./bin/mercury [-c conf-file]` | Defaults to `conf/mercury.conf` |
| **Create TLS Certs** | `make cert` | Generates self-signed test keypair in `conf/ssl/` |
| **Run Test Suite** | `python3 tests/run.py` | Full test suite and wire regressions |
| **Run Benchmarks** | `python3 build_tools/benchmark.py` | Benchmarks vs. Apache & Nginx |
| **Multi-Distro Tests** | `make docker_tests` | Runs container tests across distros |
| **Clean Build** | `make clean` | Removes build outputs, objects, and logs |

---

## Repository Map

```
Mercury/
├── AGENTS.md                 # Agent guidelines and operational tenets (this file)
├── agents/                   # Detailed agent guidelines and specifications
│   ├── CONVENTIONS.md        # Codebase style, naming, and engineering rules
│   ├── ARCHITECTURE.md       # Architectural breakdown, threading, and protocols
│   ├── WORKFLOWS.md          # Build, run, debug, and release runbooks
│   └── TESTING.md            # Test runner guide and wire regression specs
├── src/                      # C++ source code
│   ├── main.cpp              # Entry point, signals, listener dispatch, CLI loop
│   ├── winheader.hpp         # Windows Winsock and platform compatibility layer
│   ├── conf/                 # XML config loader (pugixml), routing rules, ACLs
│   ├── http/                 # Protocol engine, parser, response, TLS, streams
│   │   ├── cgi/              # RFC 3875 legacy CGI execution engine
│   │   ├── fastcgi/          # FastCGI client and PHP-FPM process manager
│   │   ├── telemetry/        # Version check, release telemetry, metrics
│   │   └── version/          # HTTP/1.1, 1.0, and 0.9 protocol handlers
│   ├── io/                   # File abstraction, path normalization, file cache
│   ├── logs/                 # Thread-safe access/error logger, IP anonymization
│   └── util/                 # ThreadPool, rate limiter, string tools, CLI parser
├── conf/                     # Default and runtime configuration files
├── tests/                    # Test runner, JSON cases, fixtures, regressions
├── build_tools/              # Dependency installers, benchmarks, release tools
├── libs/                     # Static third-party libraries (OpenSSL, Brotli, Zstd, etc.)
└── Makefile                  # Primary build automation file
```

---

## Core Engineering & Architectural Tenets

### 1. Code & Formatting Conventions
Adhere strictly to [`agents/CONVENTIONS.md`](agents/CONVENTIONS.md):
- **C++ Standard**: C++20 (`-std=c++20`), compiling cleanly with `-Wall -Wextra -Werror`.
- **Formatting**: 4-space indentation; 1TBS / K&R brace placement; tabs strictly for Makefile recipes.
- **Naming**: `snake_case` (C/Python), `PascalCase` (classes/structs), `camelCase` (C++ methods/functions). Prefix pointers with `p` (`pSSL`, `pServer`). Pre-increment (`++i`) in loops.
- **Comments**: Telegraphic style (drop articles *a*, *an*, *the*), leading with imperative verbs (`// Bind socket`). Start with uppercase; omit terminal periods on single-line comments. Never reference audit issue numbers in code or changelog.

### 2. Socket Management & Concurrency
- **Client Tracking**: Register every client descriptor under `clientsMutex` (`trackClient`) before enqueuing to `ThreadPool`; always untrack (`untrackClient`) before closing.
- **Shutdown**: On server stop, wake blocked readers with `shutdown()` before joining worker threads.
- **Loopback Cooldown**: Rapid loopback rebinds may hit kernel `TIME_WAIT` (`EADDRINUSE`).

### 3. Memory Safety & RAII
- Zero resource leaks. Use RAII wrappers (`std::unique_ptr`, `std::shared_ptr`, `std::vector`, `std::string`).
- Ensure every OpenSSL allocation (`SSL*`, `SSL_CTX*`) is paired with its cleanup call (`SSL_free`, `SSL_CTX_free`).

### 4. Binary Data & Protocol Safety
- Never assume incoming HTTP bodies or CGI payloads are null-terminated ASCII. Preserve raw byte arrays containing `\0`.
- Retain surplus pipelined bytes in connection buffers across keep-alive requests.
- Limit header lines to 64 KiB (return 431 on overflow); return 400 on malformed or duplicate framing headers (`Content-Length`, `Transfer-Encoding`).

### 5. Version Synchronization
- Keep `version.txt` (`Mercury vX.Y.Z`) and `CHANGELOG.md` (`## [X.Y.Z] - YYYY-MM-DD`) strictly in sync.

---

## Detailed References

- [`agents/CONVENTIONS.md`](agents/CONVENTIONS.md): Formatting, naming, commenting, and code idioms.
- [`agents/ARCHITECTURE.md`](agents/ARCHITECTURE.md): Concurrency model, protocol framing, CGI/FastCGI, and security boundaries.
- [`agents/WORKFLOWS.md`](agents/WORKFLOWS.md): Build recipes, debugging with GDB, profiling, and releases.
- [`agents/TESTING.md`](agents/TESTING.md): Test harness, JSON test schema, and wire regressions.
