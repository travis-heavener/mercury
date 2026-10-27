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

Mercury includes an automated, cross-platform performance benchmark suite comparing Mercury against Apache (`apache2`/`httpd`) and Nginx under standardized, unprivileged conditions.

### Architecture & Lifecycle
The suite operates with zero external Python dependencies beyond standard test packages (`psutil`):
1. **Isolated Configurations**: Executes servers under unprivileged ports (`8080` for Mercury, `8082` for Apache, `8084` for Nginx) using templates in [`tests/conf_files/benchmark/`](../tests/conf_files/benchmark/).
2. **Standardized Payloads**: Serves fixed static fixtures from [`tests/root/benchmark/`](../tests/root/benchmark/):
   - `1k.html` (1 KiB micro-payload for request routing and parsing overhead)
   - `64k.bin` (64 KiB asset for socket write buffering)
   - `1m.bin` (1 MiB payload for streaming and chunking efficiency)
   - `hello.php` (FastCGI payload for dynamic backend execution)
3. **High-Precision Memory Accounting**: Runs background `psutil` sampling polling RSS every 50ms, recursively summing memory across the parent server process and all spawned child worker processes (`mpm_event` workers, Nginx workers).
4. **Apples-to-Apples Parity**:
   - 10 worker threads / concurrency baseline across all servers
   - Access logging disabled to isolate disk I/O from networking efficiency
   - Keep-Alive synchronized to 5s timeout and 10,000 maximum requests
   - Plain HTTP over IPv4 loopback to eliminate cipher and dual-stack kernel `TIME_WAIT` variance

### Running Benchmarks Locally
Run with defaults (benchmarks all discovered servers, 5 trials, 10s per trial):
```bash
python3 build_tools/benchmark.py
```

Benchmark only Mercury with quick trials:
```bash
python3 build_tools/benchmark.py --servers mercury --trials 3 --duration 2
```

Benchmark with high concurrency and a specific load tool:
```bash
python3 build_tools/benchmark.py --tool wrk --concurrency 100 --duration 10
```

Benchmark larger payload sizes:
```bash
python3 build_tools/benchmark.py --payload 64k --duration 5
```

Export structured JSON results and Markdown summary:
```bash
python3 build_tools/benchmark.py --output-format markdown,json --output benchmark_results.md
```

### Command-Line Options
| Flag | Default | Description |
|---|---|---|
| `--servers` | `mercury,apache,nginx` | Comma-separated list of servers to benchmark |
| `--trials` | `5` | Number of timed trial runs per server |
| `--duration` | `10` | Duration in seconds of each active load window |
| `--concurrency` | `50` | Simultaneous client connections |
| `--warmup` | `200` | Number of warmup requests fired before trial measurement |
| `--payload` | `1k` | Standardized fixture (`1k`, `64k`, `1m`, `php`) |
| `--tool` | `auto` | Load generator engine (`auto`, `wrk`, `hey`, `python`) |
| `--output-format` | `markdown` | Output format (`markdown`, `json`, or `markdown,json`) |
| `--output` | `None` | Path to file to save formatted benchmark output |
| `--mercury-path` | `None` | Custom path to Mercury binary |
| `--apache-path` | `None` | Custom path to Apache executable |
| `--nginx-path` | `None` | Custom path to Nginx executable |


