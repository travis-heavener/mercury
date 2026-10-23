# Workplan: FastCGI & PHP-FPM Implementation

## 1. Executive Summary & Objective

Transition Mercury's dynamic PHP processing from traditional RFC 3875 process-per-request CGI (`php-cgi` via `fork`/`execvpe`/pipes/temp files) to a high-performance, asynchronous/socket-based **FastCGI Client** communicating directly with **PHP-FPM** (Linux) or `php-cgi -b 127.0.0.1:9000` (Windows & Linux).

### Expected Benefits
* **Throughput**: Increase PHP throughput from ~100–250 req/s to 3,000–8,000+ req/s (10× to 40× gain).
* **Latency**: Reduce PHP request latency from 15–50 ms down to 0.5–2 ms (~90% drop).
* **Zero Disk I/O**: Stream response payloads directly into memory or chunked buffers, eliminating temporary file creation and unlinking in `tmp/`.
* **Resource Stability**: Eliminate child process churn, OS process creation overhead, and risk of zombie processes.
* **OPcache Persistence**: Enable persistent bytecode caching across requests in shared memory.

---

## 2. Technical Architecture & Protocol Framing

Mercury will act strictly as a **FastCGI Client (Gateway)**. PHP-FPM will operate externally as the service daemon.

```
┌────────────────────────────────────────────────────────┐
│                   Mercury Web Server                   │
│                                                        │
│   HTTP Request ──► File Detection ──► FastCGI Gateway  │
└────────────────────────────┬───────────────────────────┘
                             │
     UNIX Domain Socket: /run/php/php-fpm.sock
     OR TCP Loopback: 127.0.0.1:9000
                             │
┌────────────────────────────▼───────────────────────────┐
│              PHP-FPM Daemon (External)                 │
│                                                        │
│  ┌──────────────────────────────────────────────────┐  │
│  │ Worker Pool (Pre-Forked Processes)               │  │
│  │ Shared Memory OPcache (Pre-Compiled Bytecode)    │  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
```

### FastCGI Record Specification (v1.0)
Every packet contains an 8-byte binary header:

```cpp
struct FCGI_Header {
    uint8_t  version;          // FastCGI version (always 1)
    uint8_t  type;             // Record type (see below)
    uint16_t requestId;        // Host request identifier (network byte order)
    uint16_t contentLength;    // Payload length in bytes (network byte order, max 65535)
    uint8_t  paddingLength;    // Padding bytes appended to payload
    uint8_t  reserved;         // Reserved byte
};
```

### Required FastCGI Record Types
| Record Name | Type Value | Direction | Purpose |
|---|:---:|:---:|---|
| `FCGI_BEGIN_REQUEST` | `1` | Mercury ──► FPM | Initiates request with `role = FCGI_RESPONDER` (1) and keep-alive flags. |
| `FCGI_PARAMS` | `4` | Mercury ──► FPM | Transmits CGI environment key-value pairs using FastCGI length encoding; terminated by empty record (`contentLength = 0`). |
| `FCGI_STDIN` | `5` | Mercury ──► FPM | Streams HTTP POST/PUT request body; terminated by empty record (`contentLength = 0`). |
| `FCGI_STDOUT` | `6` | FPM ──► Mercury | Streams CGI headers (`Status`, `Content-Type`) and generated response body. |
| `FCGI_STDERR` | `7` | FPM ──► Mercury | Streams PHP execution warnings/errors (routed to Mercury `ERROR_LOG`). |
| `FCGI_END_REQUEST` | `3` | FPM ──► Mercury | Signals completion with application return code and protocol status (`FCGI_REQUEST_COMPLETE`). |

### FastCGI Parameter Length Encoding
FastCGI encodes key and value lengths compactly:
* If length `< 128` bytes: 1 byte (`length & 0x7F`).
* If length `≥ 128` bytes: 4 bytes with high bit set (`((length >> 24) | 0x80)`, `(length >> 16)`, `(length >> 8)`, `length`).

---

## 3. Phased Implementation Roadmap

### Phase 1: Protocol Encoders & Data Structures
**Target Directory**: `src/http/fastcgi/`
- [x] Create `src/http/fastcgi/protocol.hpp`:
  - Define FastCGI constants (`FCGI_VERSION_1`, `FCGI_RESPONDER`, record types, protocol statuses).
  - Define `FCGI_Header`, `FCGI_BeginRequestBody`, `FCGI_EndRequestBody`.
  - Implement helper functions for big-endian serialization (`toNetwork16`/`fromNetwork16`, `toNetwork32`/`fromNetwork32`).
- [x] Create `src/http/fastcgi/params.hpp` and `params.cpp`:
  - Implement zero-allocation FastCGI parameter encoder taking key-value pairs (using `std::string_view`).
  - Encode variable-length prefix headers without intermediate string allocations.
- [x] Add unit tests verifying byte-exact framing for headers, parameters, and padding (`tests/test_fastcgi_protocol.cpp`).

---

### Phase 2: FastCGI Transport Client
**Target Files**: `src/http/fastcgi/client.hpp` and `src/http/fastcgi/client.cpp`
- [x] Implement socket transport abstraction:
  - **Linux**: Support UNIX domain sockets (`AF_UNIX`) via `sockaddr_un` (e.g. `unix:/run/php/php8.4-fpm.sock` or `/var/run/php-fpm.sock`).
  - **Linux & Windows**: Support TCP sockets (`AF_INET` / `AF_INET6`) via Winsock / POSIX sockets (e.g. `127.0.0.1:9000` or `[::1]:9000`).
  - **Auto-Discovery**: Implement `detectDefaultFastCGIAddress()` prioritizing the highest installed PHP version on the system by default for security and performance, falling back to `127.0.0.1:9000`.
- [x] Implement `Client::sendRequest`:
  - Send `FCGI_BEGIN_REQUEST`.
  - Send `FCGI_PARAMS` stream assembled from Mercury's CGI environment extractor.
  - Send empty `FCGI_PARAMS` terminator.
  - Stream `FCGI_STDIN` from HTTP request body (in chunks up to 64 KiB), followed by empty `FCGI_STDIN` terminator.
- [x] Implement `Client::receiveResponse`:
  - Read incoming `FCGI_Header` records in a loop.
  - Demux `FCGI_STDOUT` payload: parse CGI headers (`Status`, `Content-Type`, response headers) into `Response` headers; stream body directly into `Response` via `MemoryStream`.
  - Route `FCGI_STDERR` packets to `ERROR_LOG`.
  - Terminate upon receiving `FCGI_END_REQUEST`.
- [x] Configure non-blocking read/write timeouts (default: 10 seconds) with `poll()` / `WSAPoll` to prevent server hangs on misbehaving scripts.
- [x] Add comprehensive unit test suite (`tests/test_fastcgi_client.cpp`) verifying endpoint parsing, content-type inference, parameter extraction, and mock FastCGI roundtrips.

---

### Phase 3: Server & Configuration Integration
**Target Files**: `src/conf/conf.hpp`, `src/conf/conf.cpp`, `conf/mercury.conf`
- [ ] Add XML configuration directives:
  - `<EnableFastCGI> on|off </EnableFastCGI>` (default: `off`).
  - `<FastCGIAddress> auto </FastCGIAddress>` (optional; defaults to auto-detecting highest PHP version socket or `127.0.0.1:9000`; accepts `unix:/path` or `host:port`).
  - `<FastCGITimeout> 10 </FastCGITimeout>` (timeout in seconds, default: 10).
- [ ] Update config loader in `src/conf/conf.cpp`:
  - Add node names to `mercuryNodeNames`.
  - Validate address format (`unix:` prefix or path for UDS vs `host:port` / `ip:port` for TCP, or `auto`).
- [ ] Update HTTP request handlers (`src/http/version/handler_1_1.cpp`, `handler_1_0.cpp`):
  - When target file is a `.php` file:
    1. If `conf::IS_FASTCGI_ENABLED`: route request to `http::fastcgi::handleRequest(...)`.
    2. Else if `conf::IS_PHP_ENABLED`: fallback to legacy `http::cgi::handlePHPRequest(...)`.
    3. Else: serve raw `.php` static file or trigger 403 Forbidden based on configuration.

---

### Phase 4: Zero-Disk I/O & Memory Optimization
**Target Files**: `src/http/fastcgi/client.cpp`, `src/http/response.cpp`
- [x] Eliminate temporary disk files:
  - Parse `Status: <code [reason]>` directly from the `FCGI_STDOUT` stream buffer.
  - Extract and trim headers in-place using `std::string_view`.
  - Stream body directly into `MemoryStream` attached to `Response`, eliminating disk writes and temp files in `tmp/`.
- [x] Reuse buffers for request serialization to minimize dynamic allocations per PHP transaction.

---

### Phase 5: Connection Pooling (Optional / Performance Optimization)
- [ ] Evaluate persistent connection reuse:
  - Check `FCGI_KEEP_CONN` flag support in PHP-FPM.
  - Implement a thread-safe connection pool for active worker threads, recycling open domain/TCP sockets across keep-alive requests to avoid `connect()`/`close()` overhead.

---

### Phase 6: Tooling, PHP Setup Scripts & Environment Automation
**Target Files**: `conf/setup_php.ps1`, `build_tools/install_deps.sh`, `docker/entrypoint.sh`, `tests/run.py`
- [ ] **Windows PHP Setup Script (`conf/setup_php.ps1`)**:
  - Update `setup_php.ps1` to configure OPcache in generated `php.ini` (`zend_extension=opcache`, `opcache.enable=1`, `opcache.enable_cli=1`).
  - Provide a companion script or parameter (e.g. `conf/run_php_fastcgi.ps1` or `setup_php.ps1 -StartFastCGI`) to launch the Windows FastCGI listener:
    ```powershell
    $env:PHP_FCGI_CHILDREN = 8
    $env:PHP_FCGI_MAX_REQUESTS = 1000
    Start-Process -FilePath "php\php-cgi.exe" -ArgumentList "-b 127.0.0.1:9000" -WindowStyle Hidden
    ```
- [ ] **Linux Dependency Installer (`build_tools/install_deps.sh`)**:
  - Add `php-fpm` to package lists across all supported package managers:
    - `apt`: `php-fpm` (e.g. `php-fpm` / `php-cgi`).
    - `pacman`: `php-fpm`.
    - `dnf`: `php-fpm`.
- [ ] **Multi-Distro Docker Container Harness (`docker/entrypoint.sh`)**:
  - Install `php-fpm` across Ubuntu, Debian, Arch, and Fedora containers.
  - Automatically start the `php-fpm` daemon before running `tests/run.py`.
- [ ] **Test Runner Lifecycle Automation (`tests/run.py`)**:
  - Add automatic detection and lifecycle management for FastCGI backend:
    - **Linux**: Check for active PHP-FPM UNIX domain socket or launch local test daemon.
    - **Windows**: Automatically spawn `php/php-cgi.exe -b 127.0.0.1:9000` as a background test subprocess before FastCGI test runs, and terminate it cleanly during test cleanup.

---

### Phase 7: Verification, Testing & Cross-Platform Support
- [ ] **Linux Verification**:
  - Test against local PHP-FPM daemon via UNIX domain socket (`/run/php/php-fpm.sock`).
  - Test against PHP-FPM over TCP (`127.0.0.1:9000`).
- [ ] **Windows Verification**:
  - Test cross-compilation with MinGW-w64 (`make windows`).
  - Test against Windows PHP running in FastCGI bind mode (`php-cgi.exe -b 127.0.0.1:9000`).
- [ ] **Regression Suite**:
  - Add FastCGI test group to `tests/regressions.py` verifying:
    - Basic `GET` execution with query parameters.
    - `POST` request body handling (form data and raw payloads).
    - PHP path info resolution (`/test.php/extra/path`).
    - Custom header transmission (`HTTP_X_*`).
    - Error handling (PHP crashes, fatal errors, script timeouts).
- [ ] **Documentation**:
  - Update `CONFIG.md` with FastCGI directives and PHP-FPM configuration examples.
  - Update `agents/ARCHITECTURE.md` with FastCGI concurrency flow.
  - Update `CHANGELOG.md`.

---

## 4. File Modification Matrix

| File | Change Scope | Status |
|---|---|---|
| `src/http/fastcgi/protocol.hpp` | FastCGI binary structs, constants, byte-swapping helpers. | Completed |
| `src/http/fastcgi/params.hpp` | FastCGI parameter encoder with length-prefix encoding. | Completed |
| `src/http/fastcgi/params.cpp` | Implementation of environment block serializer. | Completed |
| `src/http/fastcgi/client.hpp` | FastCGI client class definition (UDS and TCP socket client). | Completed |
| `src/http/fastcgi/client.cpp` | Request transmission, STDOUT/STDERR demuxing, response header parsing, auto-discovery. | Completed |
| `tests/test_fastcgi_protocol.cpp` | Unit test suite for wire framing, parameters, and padding. | Completed |
| `tests/test_fastcgi_client.cpp` | Unit test suite for transport client, endpoint parsing, and mock transactions. | Completed |
| `src/conf/conf.hpp` | Add `IS_FASTCGI_ENABLED`, `FASTCGI_ADDRESS`, `FASTCGI_TIMEOUT` variables. | Pending |
| `src/conf/conf.cpp` | Add parsing logic for `<EnableFastCGI>`, `<FastCGIAddress>`, `<FastCGITimeout>`. | Pending |
| `src/http/version/handler_1_1.cpp` | Route PHP requests to FastCGI handler before falling back to legacy CGI. | Pending |
| `src/http/version/handler_1_0.cpp` | Route PHP requests to FastCGI handler before falling back to legacy CGI. | Pending |
| `conf/mercury.conf` | Document FastCGI directives with default settings. | Pending |
| `conf/setup_php.ps1` | Configure OPcache in `php.ini` and add FastCGI launch option for Windows. | Pending |
| `build_tools/install_deps.sh` | Add `php-fpm` package installation across apt, pacman, and dnf. | Pending |
| `docker/entrypoint.sh` | Install and initialize `php-fpm` daemon inside multi-distro test containers. | Pending |
| `tests/run.py` | Add automated FastCGI backend lifecycle management (start/stop) during test runs. | Pending |
| `tests/regressions.py` | Add FastCGI regression cases (GET, POST, headers, errors). | Pending |
| `Makefile` | Ensure `src/http/fastcgi/*.cpp` files are included in build rules. | Completed (automatic discovery) |

---

## 5. Risk Assessment & Mitigation

| Risk | Impact | Mitigation Strategy |
|---|---|---|
| **PHP-FPM Socket Stalls / Timeouts** | Worker thread blocked indefinitely. | Enforce strict socket timeouts (`SO_RCVTIMEO`, `SO_SNDTIMEO`) and `poll()` loops with 10s default deadline. Return 504 Gateway Timeout if FPM stalls. |
| **Large Request / Response Payloads** | FastCGI payload limit is 65,535 bytes per record. | Chunk outgoing request bodies into `FCGI_STDIN` records $\le 65,535$ bytes. Reassemble incoming `FCGI_STDOUT` records iteratively. |
| **Platform Incompatibilities** | Windows does not natively support UNIX domain sockets in older environments. | Support TCP loopback (`127.0.0.1:9000`) universally across both Windows and Linux; enable UNIX domain sockets conditionally on POSIX systems. |
| **Backward Compatibility** | Existing setups rely on `php-cgi` without external FPM daemon. | Retain existing `EnablePHPCGI` implementation as fallback; FastCGI is an opt-in toggle (`EnableFastCGI`). |

