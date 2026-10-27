# Mercury Testing Guide

This document describes the structure, execution model, schema, and authoring guidelines for the Mercury test suite.

---

## 1. Test Harness Overview

Mercury uses a test runner located in [`tests/`](../tests/):
- **Runner Entry Point**: [`tests/run.py`](../tests/run.py) manages the server lifecycle across multiple configuration files, spins up worker pools, and aggregates results.
- **JSON Test Specification**: [`tests/tests.json`](../tests/tests.json) defines declarative HTTP request/response test cases across HTTP versions and configurations.
- **Wire Regression Engine**: [`tests/regressions.py`](../tests/regressions.py) implements raw TCP and TLS socket tests for protocol edge cases, fuzzing, pipelining, and binary safety.
- **Test Case Abstraction**: [`tests/test_case.py`](../tests/test_case.py) handles HTTP serialization, socket communication, response parsing, and assertion verification.
- **Update Utility Test Suite**: [`tests/test_update.py`](../tests/test_update.py) validates version parsing, XML configuration reconciliation, process detection, and atomic rollback for `conf/update.py`.
- **Test Configurations**: [`tests/conf_files/`](../tests/conf_files/) contains specialized `mercury.conf` variants for feature-specific runs.
- **Document Fixtures**: [`tests/root/`](../tests/root/) contains static files, error pages, and PHP scripts used by tests.

---

## 2. 4-Way Transport Verification Matrix

For every HTTP/1.x case defined in `tests.json`, the runner executes assertions across **4 transport modes**:
1. **IPv4 Plain**: `http://127.0.0.1:8080`
2. **IPv4 TLS**: `https://127.0.0.1:8081`
3. **IPv6 Plain**: `http://[::1]:8080`
4. **IPv6 TLS**: `https://[::1]:8081`

> [!NOTE]
> If a case specifies `"httpsOnly": true`, the plain HTTP transport slots are counted as automatic passes without sending traffic.

---

## 3. Running the Test Suite

### Prerequisites
1. **Stop Running Servers**: Ensure no background instance of Mercury is currently running.
2. **Ports Available**: Ports `8080` and `8081` must be unbound.
3. **TLS Certificates**: Generate test certificates if missing:
   ```bash
   make cert
   ```
4. **Python Dependencies**:
   ```bash
   pip install brotli zstandard psutil
   ```
5. **No Python Optimization**: Do **not** use `python3 -O` or set `PYTHONOPTIMIZE`, as wire regressions rely on Python `assert` statements.

### Execution Command
Run from the repository root:
```bash
python3 tests/run.py
```
On Windows:
```cmd
python tests/run.py
```

### FastCGI integration

Install `php-fpm` in addition to `php-cgi` on Linux. `tests/run.py` automatically
starts isolated foreground PHP-FPM fixtures over TCP and Unix sockets, replays
shared HTTP assertions and wire regressions with CGI disabled, and cleans up
each fixture. Windows runs exercise Mercury-owned `php-cgi -b` startup and
reuse of an external listener. Additional runs assert 502 and 504 handling.
See [`tests/README.md`](../tests/README.md) for platform requirements and the
`fastcgiTransport`, `reuseCasesFrom`, and `platforms` run attributes.

### Update Utility Test Suite
To validate `conf/update.py` version parsing, XML reconciliation, and rollback automation:
```bash
python3 tests/test_update.py
```
This suite runs independently using Python's standard `unittest` framework and requires no running server or network access.

### Multi-Distro Docker Testing
To validate Mercury across Ubuntu, Debian, Alpine, Fedora, and Arch:
```bash
make docker_tests
```

---

## 4. JSON Test Schema (`tests.json`)

`tests/tests.json` contains an array of **Runs**. Each run tests a specific configuration file:

### Structure Hierarchy
```
Array of Runs [
  {
    "desc": "Run Description",
    "confFile": "normal.conf",
    "wireRegressions": false,
    "cases": [
      {
        "desc": "Group Description",
        "versions": ["1.0", "1.1"],
        "cases": [
          { ... Case definition ... }
        ]
      }
    ]
  }
]
```

### Case Attributes

| Attribute | Type | Required | Description |
|---|---|:---:|---|
| `method` | string | Yes | HTTP method (`GET`, `POST`, `HEAD`, `OPTIONS`, `PUT`, `DELETE`, etc.) |
| `path` | string | Yes | Target URI including query parameters (e.g. `/index.html?key=val`) |
| `expectedStatus` | integer | Yes | Expected numeric HTTP status code (e.g. `200`, `404`, `301`, `403`) |
| `headers` | object | No | Request headers to inject (e.g. `{"Range": "bytes=0-10", "Accept-Encoding": "br"}`) |
| `expectedHeaders` | object | No | Response header assertions (see below) |
| `body` | string | No | Request payload sent with request (auto-calculates `Content-Length`) |
| `expectedBody` | string | No | Expected response body (exact match by default) |
| `expectedBodyContainsMode`| bool | No | If `true`, checks substring containment rather than exact match |
| `httpsOnly` | bool | No | If `true`, executes only on TLS transports |
| `comments` | string | No | Human/agent documentation describing the purpose of the test |

### Header Assertion Rules
In `expectedHeaders`:
- `"Header-Name": "exact_value"`: Header must match string exactly (case-sensitive value).
- `"Header-Name": true`: Header must be present with any non-empty value.
- `"Header-Name": false`: Header must be absent from the response.

### Compression Assertions
When asserting `Content-Encoding` (e.g. `"expectedHeaders": {"Content-Encoding": "br"}`), the test runner automatically decompresses the received payload and compares it byte-for-byte with the corresponding fixture file in `tests/root/`.

---

## 5. Raw Wire Regressions (`regressions.py`)

JSON tests cannot easily test raw socket malformations or byte-level protocol framing. `tests/regressions.py` tests raw TCP socket interactions directly.

### Covered Wire Scenarios
1. **Malformed Headers**: Injected newlines (`\n` without `\r`), overflowing or negative `Content-Length`, duplicate headers, and smuggling attempts.
2. **Transfer-Encoding Smuggling**: Client attempts to send `Transfer-Encoding: chunked` (must return `400`).
3. **Pipelining**: Sending multiple requests in a single TCP write; verifying that bodies and responses stay properly framed and separated.
4. **Fragmented Bodies**: Sending chunked or fragmented payload chunks across 1-byte or odd-byte socket increments.
5. **Binary Preservation**: Preserving binary payloads (`\0`, arbitrary bytes) through PHP-CGI execution.
6. **Traversal & Sanitization**: Probing access control bypasses with encoded dots (`/%2e/`), backslashes (`/%5c`), and double slashes (`//`).
7. **Redirect Header Injection**: Verifying CRLF query parameters cannot inject new response headers.
8. **Directory Range Requests**: Validating byte range slicing on dynamically generated in-memory directory listings.
9. **Keep-Alive Limits**: Verifying that `Connection: close` is advertised and enforced on the 100th request.
10. **Header Limits**: Sending 64 KiB+ headers to assert proper **431 Request Header Fields Too Large** rejection without memory ballooning.
11. **Concurrent Clients**: 48 simultaneous worker threads validating thread safety and IP tracking isolation.
12. **HTTP/0.9 Simplicity**: Verifying that one-line HTTP/0.9 requests do not stall waiting for extra CRLFs.

---

## 6. How to Add a Test

When fixing a bug or adding a feature:

1. **Choose JSON vs Wire**:
   - Use `tests/tests.json` for standard HTTP request/response validation, status checks, header transformations, routing, or redirects.
   - Use `tests/regressions.py` if testing raw socket byte order, malformed framing, pipelining, or concurrency.
2. **Select Configuration**:
   - Reuse an existing run in `tests.json` (such as `normal.conf` or `regressions.conf`) whenever possible.
   - Only create a new configuration under `tests/conf_files/` if unique server settings or limits are needed.
3. **Add Deterministic Fixtures**:
   - Place any static files or PHP scripts under `tests/root/`. Keep fixtures small, deterministic, and free of dynamic timestamps.
4. **Run and Verify**:
   - Execute `python3 tests/run.py`.
   - Ensure all runs pass across all 4 transport variants.

---

## 7. Performance Benchmark Suite (`build_tools/benchmark.py`)

Run the suite as an unprivileged user. It requires Python 3.9+ and `psutil` (`python -m pip install psutil`); `wrk` and `hey` are optional. Defaults are five fresh-process trials per discovered server, ten seconds of measured load, fifty clients, and two hundred warmup requests per trial.

### Runtime setup and diagnostics

The runner generates absolute-path configs in `logs/benchmark/` from templates in `tests/conf_files/benchmark/`, and binds IPv4 loopback ports 8080 (Mercury), 8082 (Apache), and 8084 (Nginx). Paths containing spaces are supported. Apache uses its installed modules, selecting event MPM on Linux and winnt MPM on Windows. On Windows, discovery searches real Chocolatey installations before PATH shims; executable overrides are available.

**Mercury needs `conf/mimes.conf` even when `-c` selects a benchmark config.** This runtime file is ignored by Git. The runner copies `conf/default/mimes.conf` only when the runtime copy is missing, and preserves existing customizations. An external Mercury executable must retain its normal `bin/` and `conf/default/` installation layout. The workflow also initializes the MIME file before running checks.

Before each trial, the runner checks config syntax, starts a fresh server with stdin held open, and retains stdout/stderr in `logs/benchmark/<server>_startup.log`. A startup error reports the command, exit code, and log tails. Port conflicts fail before launching another process. The server and active memory sampler are stopped on failure; successful reports are written only after the requested trials complete. Missing optional Apache/Nginx binaries are listed as skipped; missing requested Mercury or an entirely empty run fails.

### Workloads and measurement

Static fixtures under `tests/root/benchmark/` support `1k` (1 KiB), `64k` (64 KiB), and `1m` (1 MiB). The runner checks HTTP status and exact fixture bytes before timing, then performs warmup outside the measured memory/load window. All load tools request uncompressed responses; `hey` also disables automatic compression and redirects. HTTP errors, socket errors, tool failures, and zero successful requests reject the trial.

The PHP fixture remains reserved for future backend benchmarks. `--payload php` is intentionally unavailable: the current configs do not configure PHP-FPM/FastCGI across all three servers, and benchmarking a served PHP source file would produce misleading results. Compression trials are also deferred.

Mercury keeps 10 idle threads and may grow to 20; Apache uses 20 worker threads, and Nginx uses one event-driven worker process. These architectures differ; equal worker counts do not imply equal concurrency capacity. Keep-alive uses a 5-second timeout and a 10,000-request limit. Mercury writes access logs to the platform null device; Apache/Nginx disable access logging. Mercury still incurs log formatting/queue overhead.

RSS is sampled every 50 ms during load and sums parent plus recursive children. Shared resident pages can be counted more than once across processes, so this is not proportional/private memory. The report includes average idle/load RSS, the average of each trial's sampled peak RSS, throughput, and mean/p50/p99 latency. Percentiles are averaged across trials rather than pooled. All valid trials contribute to arithmetic means; no automatic outlier filtering is applied. Host load, the Python client/GIL, OS differences, and Windows Nginx's implementation affect results. Compare servers within the same host/tool run; CI measurements are not a universal performance claim.

### Running locally

```bash
# Run detected servers using available load generator
python3 build_tools/benchmark.py

# Run quick Mercury trials
python3 build_tools/benchmark.py --servers mercury --trials 3 --duration 2

# Use external load generator and larger fixture
python3 build_tools/benchmark.py --tool wrk --payload 64k --concurrency 50 --duration 10

# Export both report formats
python3 build_tools/benchmark.py --output-format markdown,json --output benchmark_results.md

# Run lifecycle, parsing, clean-checkout startup, and report regression checks
python3 -m unittest discover -s tests -p test_benchmark.py -v
```

The native clean-checkout regression runs when a Mercury executable exists in `bin/`; otherwise that case is skipped. Synthetic executable checks are POSIX-only; the workflow runs the native regression on both platforms.

### Command-line options

| Flag | Default | Description |
|---|---|---|
| `--servers` | `mercury,apache,nginx` | Unique comma-separated selection of known servers |
| `--trials` | `5` | Positive number of fresh-process trials per server |
| `--duration` | `10` | Positive duration in seconds per active load window |
| `--concurrency` | `50` | Positive concurrent client count |
| `--warmup` | `200` | Nonnegative warmup request count per trial |
| `--payload` | `1k` | Static fixture: `1k`, `64k`, or `1m` |
| `--tool` | `auto` | Prefer `wrk`, then `hey`, then `python`; explicit unavailable tools fail |
| `--output-format` | `markdown` | `markdown`, `json`, or `markdown,json` |
| `--output` | None | Report path; both formats produce matching `.md`/`.json` siblings |
| `--mercury-path` | None | Explicit Mercury executable; invalid overrides fail |
| `--apache-path` | None | Explicit Apache executable; invalid overrides fail |
| `--nginx-path` | None | Explicit Nginx executable; invalid overrides fail |
| `--mercury-version` | Executable `-v` | Verified release label; required when checkout and release versions differ |

Mercury's `-v` may read the installation's `version.txt`; querying a copied binary alone does not verify its compiled release. The workflow uses the resolved release tag as the report label, or the checkout version for a source build. The Markdown comparison column uses Apache when present, otherwise Nginx.
