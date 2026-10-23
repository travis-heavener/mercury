# Extending the Mercury test suite

`python3 tests/run.py` runs both the JSON cases in `tests.json` and the raw-socket
cases in `regressions.py`. On Windows, use `python tests/run.py`. Run without
`-O` or `PYTHONOPTIMIZE`: the wire tests use assertions.

## Setup and execution

Build Mercury using the repository build instructions. The runner expects
`bin/mercury` on Linux or `bin/mercury.exe` on Windows. Install the Python
packages `brotli`, `zstandard`, and `psutil` in the interpreter running the suite.
PHP CGI and PHP-FPM (Linux) are required for the full suite: `php-cgi` must be on PATH on Linux;
on Windows, use `conf/setup_php.ps1` as shown in the Windows test workflow.

Copy `conf/default/mimes.conf` to `conf/mimes.conf` if needed, create `logs/`
and `conf/ssl/`, and generate a local certificate/key with `make cert` or the
OpenSSL command in the platform test workflow. The required files are
`conf/ssl/cert.pem` and `conf/ssl/key.pem`. These are test certificates; the
runner deliberately disables certificate verification.

Stop any running Mercury process first. The suite starts and stops the server
for each run configuration. It needs IPv4 and IPv6 loopback and free ports
8080 (HTTP) and 8081 (HTTPS). It changes its own working directory, so the
command above works from the repository root. A successful run ends with
`Success` and matching passing/total counts; inspect failures above that summary.

The Linux workflow installs `php-fpm` alongside `php-cgi`. The runner starts a
private foreground FPM pool for each FastCGI run, on an ephemeral TCP port or a
temporary Unix socket, and stops only those fixture processes. It does not use
or alter system FPM services. Linux runs require permission to create Unix
sockets; a missing FPM installation or failed fixture startup fails the suite.
When run as root (for example, Docker), fixture workers run as `nobody`; ensure
the checkout is readable and traversable by that user.

Windows runs test both Mercury's automatic PHP launch and reuse of an existing
fixture listener, and verify ownership on shutdown. Unix-socket runs are Linux
only. Traditional CGI runs remain enabled.

FastCGI runs reuse the normal JSON cases and wire regressions with CGI disabled,
covering routing, PATH_INFO, query strings, forms, PHP headers/status, binary
payloads exceeding two FastCGI records, pipelining, and concurrent requests.
Separate runs verify unavailable backends (502, with both CGI and FastCGI enabled
to ensure failure does not fall back to CGI) and slow PHP (504).

## JSON structure and attributes

The file is an array of **runs**. Each run selects a server configuration and
contains **groups**; each group expands its **cases** across its HTTP versions.
Use ordinary JSON: quoted keys, no trailing commas, and no `//` comments.

### Run attributes

| Attribute | Required / default | Purpose |
| --- | --- | --- |
| `desc` | Required string | Run label printed by the runner. |
| `confFile` | Required string | Filename under `tests/conf_files/`; selects server features and limits for all cases in the run. |
| `cases` | Required array | Groups to expand and execute. |
| `wireRegressions` | Optional boolean, `false` | Also runs `regressions.py` after JSON cases. Keep enabled on the existing `regressions.conf` run: wire cases depend on its access rules, CGI, redirects, limits, and fixtures. |
| `fastcgiTransport` | Optional string | Create isolated backend/config: `tcp` (FPM on Linux, Mercury-owned PHP on Windows), `unix` (Linux FPM), `external` (Windows fixture listener), or `unavailable` (reserved, non-listening loopback endpoint). Config derives from `regressions.conf`, with FastCGI on and CGI off, except `unavailable`, which enables both to check precedence. |
| `platforms` | Optional string array | Include run only on listed `sys.platform` values; excluded runs are omitted from totals. |
| `reuseCasesFrom` | Optional config filename | Prepend cases from an existing non-FastCGI run, then append this run's own cases. Use `normal.conf` to share assertions without copying them. |

### Group attributes

| Attribute | Required / default | Purpose |
| --- | --- | --- |
| `desc` | Descriptive string, not consumed by loader | Identifies the feature for readers. Currently not printed per group. |
| `versions` | Required array of strings | Versions without the `HTTP/` prefix, e.g. `["1.0", "1.1"]`. Every case runs for every listed version. Unsupported versions can be used to test rejection. |
| `cases` | Required array | Request/response cases. |

### Case attributes

| Attribute | Required / default | Purpose |
| --- | --- | --- |
| `method` | Required string | Request method, e.g. `GET`, `HEAD`, or `POST`; also used to skip reading a HEAD body. Use uppercase for normal requests. |
| `path` | Required string | Request target including optional query, e.g. `/index.html?x=1`. Sent as supplied, without URL encoding or path normalization by the runner. |
| `expectedStatus` | Required integer | Exact HTTP/1.x status. Ignored for HTTP/0.9, which has no status line; existing cases use `-1`. |
| `headers` | Optional object, `{}` | Request header names and string values. Used for negotiation, ranges, conditionals, filters, framing, and malformed-header cases. See transformations below. |
| `expectedHeaders` | Optional object, `{}` | Response header assertions: a string means exact value, `true` means present with any value, `false` means absent. Names are case-insensitive; string values are case-sensitive. Do not use numbers or `null`. |
| `body` | Optional string, `""` | UTF-8 request payload, useful for CGI and body-limit tests. Nonempty bodies automatically get their UTF-8 byte count as Content-Length. |
| `expectedBody` | Optional string; no body assertion when omitted in HTTP/1.x | Exact decoded UTF-8 response text, or substring when contains mode is enabled. `""` asserts an empty body. Supply this for every HTTP/0.9 case. |
| `expectedBodyContainsMode` | Optional boolean, `false` | If true, `expectedBody` only needs to occur within the response. Useful for generated HTML; exact matching is stronger for stable fixtures. |
| `httpsOnly` | Optional boolean, `false` | Sends the case only over TLS, e.g. Brotli negotiation. Plain HTTP slots are counted as automatic passes without sending a request. |
| `comments` | Optional descriptive text | Records why a case exists. Ignored by the runner; does not change assertions. |

Unknown attributes are currently ignored, so spelling mistakes can silently
omit optional assertions. Use the names above exactly. `comments` can also be
used on runs/groups as reader-only metadata.

## Choosing assertions for a feature

| Feature | Inputs and assertions |
| --- | --- |
| Routing, traversal, malformed URI | `path`, `expectedStatus`; use `expectedBody` to distinguish the actual resource when success is expected. |
| Access rules and header filters | Choose `confFile` with the rule; set `path`/`headers`; assert denial status or the configured response header. |
| Redirects | Assert status and exact `expectedHeaders.Location`; assert a would-be injected header is `false` where relevant. |
| Rewrites and CGI query handling | Set `path` with query and assert fixture output in `expectedBody`. |
| CGI request bodies | Set `method`, `body`, optional Content-Type, and exact `expectedBody`. |
| Size limits | Choose a configuration with the relevant limit; assert the rejection status for an oversized target or body. |
| Byte ranges | Set `headers.Range`; assert status, Content-Range, Content-Length, and expected slice. |
| Conditional requests | Set headers such as If-Modified-Since; assert expected status and relevant metadata/body. |
| Compression | Set Accept-Encoding and a specific string Content-Encoding expectation; this also decompresses the body and compares it byte-for-byte with the fixture file. |
| Connection policy | Assert Connection headers in JSON. Use wire tests to prove actual closure, persistence, or request sequencing. |
| Custom error pages, directory indexes | Select the configuration and assert status plus exact or contained body text. |

### Request construction and limits

Header names are uppercased. Host defaults to `localhost`; `"Host": ""` removes
it to test missing-Host handling. User-Agent is always `Mercury Test Agent`.
For nonempty `body`, omit an explicit Content-Length: the runner adds the byte
length itself. Case-variant duplicate Accept/Accept-Encoding names are merged
with commas; Range values are combined. Other duplicate normalized names are
discarded. Use wire tests for exact duplicate-header order and framing.

JSON escapes such as `\r`, `\n`, `\t`, and `\u0000` become actual characters
before sending. Percent escapes in `path` remain percent escapes on the wire.
The whole request is UTF-8 encoded, so JSON is suitable for textual payloads,
including Unicode, but not arbitrary binary bytes.

For HTTP/1.x, `expectedBody` compares UTF-8 text before decompression. Do not
combine it with compressed-body assertions. A specific string Content-Encoding
expectation (`gzip`, `deflate`, `br`, or `zstd`) additionally validates the decoded
bytes against `tests/root/` plus the requested path. Use this with a direct,
full static-file GET, without query, rewriting, or ranges. A boolean `true`
checks presence only and does not select a decoder. For HEAD, use header
presence/absence and `expectedBody: ""`, not a specific encoding string.

HTTP/0.9 JSON cases send the historical two-CRLF terminator, ignore headers/body
inputs, and compare the first received body buffer after stripping carriage
returns. Keep their expected text short; use wire tests for complete-body or
exact single-CRLF framing checks.

## Adding a meaningful case

1. Read the relevant configuration and existing JSON/wire coverage. Identify
   a distinct behavior or boundary that would fail if the feature broke.
2. Reuse the closest run and group. Add a group when a new description or
   version set helps. Add a new run/configuration only when settings must differ.
3. Reuse fixtures under `tests/root/`; add a small deterministic fixture only
   when necessary. Avoid dates, external services, timing-dependent output, and
   assumptions about platform-specific paths.
4. Add the smallest useful assertions. Prefer checking both rejection of
   dangerous input and a valid neighboring case when testing a parser boundary.
5. Run the complete suite, inspect the final summary, and let Build & Test
   verify Windows as well as Linux. A passing status alone does not prove an
   injection was harmless if content or headers are the actual security boundary.

For example, this case can be inserted into an HTTP/1.1 group using
`regressions.conf` to check that CGI preserves a Unicode request body:

```json
{
    "comments": "Content-Length counts UTF-8 bytes, not characters.",
    "method": "POST",
    "path": "/body_tests/raw.php",
    "body": "caf\u00e9 \u2603",
    "expectedStatus": 200,
    "expectedBody": "caf\u00e9 \u2603"
}
```

This example is already included; do not add a second copy. The header-boundary
cases also cover NUL, DEL, bare CR, invalid names, and obsolete folding, with
valid tab whitespace and token punctuation as positive controls. The body
cases check UTF-8 lengths and request-looking text that must remain CGI payload.
These target Mercury's HTTP handling, not SQL or shell injection in applications
hosted behind Mercury.

## Regression tests, wire tests, and counts

A **regression test** preserves a behavior that previously failed or might
break during later changes. It describes the purpose of a test, not a separate
language or framework. Many JSON cases are regression tests too.

**Wire regressions** exercise actual protocol bytes and connection behavior.
`regressions.py` is part of the same Python suite. It covers malformed framing,
fragmented/binary bodies, pipelining, client-address ownership, access checks,
redirect injection, range output, header limits, connection options, and
HTTP/0.9 framing. Those checks need socket control beyond a single JSON
request/response. There is no separate native C++ regression executable.

Every JSON case/version combination occupies four result slots: IPv4 HTTP,
IPv4 HTTPS, IPv6 HTTP, and IPv6 HTTPS. `httpsOnly` still occupies four slots,
but the two HTTP slots automatically pass. The wire count is test **groups**,
not individual requests or assertions: currently 13 groups across four
transports plus two mixed-source-IP checks, totaling 54. Thus the final total
mixes JSON transport slots and wire groups; it is not a count of unique cases.
The wire-enabled run prints `Passing (wire regressions: 54/54)` on success.

To extend wire coverage when JSON cannot express the behavior, add a function
accepting `connect` and register it in `WIRE_TESTS`. Its four-transport count
is calculated automatically. Preserve bounded timeouts and deterministic
assertions. Prefer JSON for ordinary request/response cases to keep the suite
simple.

## Linux automatic PHP-FPM ownership

`python3 tests/run.py` automatically runs these checks on Linux after all shared
CGI/FastCGI HTTP fixtures have stopped. Windows keeps its existing FastCGI startup
and ownership checks within the HTTP runs. Use this single command locally and
in CI; no separate ownership-test command is required.

Run as your normal development user to exercise real Linux automatic pool
ownership. For focused debugging only, the standalone command remains available:
`python3 tests/test_fastcgi_backend.py`.

Linux ownership-test failures make the overall runner exit unsuccessfully, even
when HTTP cases pass. The final passing/total count covers HTTP slots and wire
groups; ownership checks print their own run label and compact passing/failing
result, including skipped counts. Failures retain individual test tracebacks.
The focused standalone command keeps standard unittest output.

These checks cover omitted and `auto` addresses, PHP access to an owner-only
document root, inherited worker identity, private socket permissions, shutdown
cleanup, explicit external backend reuse, disabled FastCGI, and failed startup
without falling back to a system pool. Direct-root runs skip unprivileged
ownership cases and verify that automatic startup does not create root PHP
workers. Existing explicit TCP/Unix fixtures continue to test external backends.
