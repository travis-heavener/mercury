# Changelog

## [0.37.0] - 2026-10-23
- 

## [0.36.0] - 2026-10-22
- Perf: enable `-O3 -fno-strict-aliasing` optimizations for release builds in `Makefile`
- Perf: cache formatted HTTP `Date` header string at 1-second granularity per thread, eliminating redundant `gmtime`/`strftime` formatting on every response
- Perf: pre-calculate capacity and assemble response headers directly into single reserved buffer, eliminating intermediate string copies and reallocations in `Response::streamBody`
- Perf: cache static `Server` header string to avoid repeated version substring allocations
- Perf: consolidate file existence, type, size, and timestamp queries into single-pass `stat`/`_stat64` in `File` and `file_tools`
- Perf: skip PHP path-info scanning in `File` constructor when PHP is disabled or URI does not contain `.php`
- Perf: cache file modification timestamps directly in `File`, eliminating redundant `stat` calls during `If-Modified-Since` header evaluation
- Perf: parse HTTP request headers in-place using `std::string_view`, eliminating intermediate line, substring, and whitespace trimming allocations in `loadEarlyHeaders`
- Perf: replace temporary string constructions in `isValidHeaderName` with zero-allocation `constexpr std::string_view` lookup
- Perf: decouple temporary worker thread joining from ThreadPool `queueMutex`, eliminating `enqueue` lock contention and worker thread latency spikes
- Fix: handle and log `warn_unused_result` functions (`chown`, `read`, `write`) across privilege demotion, CLI escape parsing, and CGI execution
- Fix: add backoff delay to IPv6 socket binding retries to mitigate loopback TCP `TIME_WAIT` conflicts

## [0.35.0] - 2026-10-21
- Added: `-t, --test` CLI argument, tests configuration file syntax and validity before exiting
- Added: `-r, --root <path>` CLI argument, overrides configured `DocumentRoot` directory
- Added: `-p, --port <port>` CLI argument, overrides configured HTTP `Port`
- Added: `stats` console command (aliases: `status`, `info`, `metrics`, `uptime`), displaying server uptime, active connections, thread pool utilization, total requests, HTTP status distribution, and ingress/egress bandwidth
- Added: `config` (alias: `conf`) console command, displays loaded server configuration parameters (ports, network bindings, buffer sizes, body limits, timeouts, and security settings)
- Added: `mimes` (alias: `mime`) console command, displays or filters configured MIME type mappings (`mimes [query]`)
- Added: `version` (alias: `ver`) console command, displays Mercury version, supported HTTP protocols, and encryption capabilities
- Added: `routes` (alias: `rules`) console command, displays active routing rules including Match blocks, Access ACLs, custom headers, Directory Index settings, Redirects, and Rewrites
- Added: `tls` (alias: `cert`) console command, inspects TLS status and X.509 certificate metadata (subject, issuer, validity period, and expiration status)
- Added: `logs` (alias: `tail`) console command, views tail of access or error log files with configurable line counts (`logs [access|error] [n]`)
- Added: CLI console command aliases `quit`/`q` (`exit`), `?` (`help`), and `cls` (`clear`)
- Changed: format interactive console help menu into clean, column-aligned command table
- Docs: Add download tracker badge to README
- Docs: Update SECURITY.md contact email
- Fix: free OpenSSL context (`SSL_CTX_free`) if certificate or private key loading fails in `initTLSContext`
- Fix: verify private key matches public certificate during TLS initialization (`SSL_CTX_check_private_key`)
- Fix: ensure `Server` destructor cleans up `pSSL_CTX` if initialized
- Fix: drain `ThreadPool` tasks on `stop()` to eliminate potential cyclic references
- Fix: prevent shutdown hang in `cleanupConfig` by acquiring mutex and ensuring failed file removals are untracked
- Fix: guard filesystem calls in directory listings, status docs, and config cleanup with error codes to prevent uncaught exceptions
- Fix: prevent intra-process temporary file collisions by checking candidate paths against active temp files under lock
- Fix: validate compressor initialization in `precompressBody` to prevent emitting uncompressed bodies under `Content-Encoding` headers
- Fix: use generic paths for directory index hyperlinks to prevent backslashes in Windows URLs
- Fix: add null-pointer checks for `SSL_CTX` and `SSL` creation in remote version checker
- Fix: replace non-thread-safe `std::gmtime` and `std::localtime` with thread-safe `gmtime_r`/`gmtime_s` and `localtime_r`/`localtime_s`
- Fix: avoid 100% CPU spinning in CLI loop when stdin is not a TTY (background/daemon mode)
- Fix: redraw current line on Ctrl+L in CLI instead of erroneously executing typed command
- Perf: stream error documents (404/500/etc.) and directory listings directly from memory instead of writing temporary files to disk
- Perf: use in-memory compression for small response bodies, eliminating disk I/O in precompression
- Perf: eliminate redundant string copies and false moves in `Request`, `MemoryStream`, routing handlers, and header casing
- Perf: optimize `trimString` to operate in-place without heap reallocation
- Perf: optimize GMT and local date formatting with thread-safe `strftime`, eliminating repeated `stringstream` allocations
- Perf: cap asynchronous logger queue capacity to prevent unbounded memory growth
- Perf: move string payloads directly into asynchronous logger queues to eliminate redundant allocations
- Perf: achieve up to 45% higher throughput, ~30% lower latency, and 10% lower peak memory on dynamic and compressed workloads

## [0.34.0] - 2026-10-21
- Added: `-h, --help` CLI argument, shows brief help information for using Mercury
- Added: `-v, --version` CLI argument, shows current Mercury version
- Added: `-c, --config <file>` CLI argument, retaining existing functionality for specifying alternate config files at runtime
- Added: automatic privilege dropping and user ownership preservation when executed via sudo/doas to protect system files and prevent mismatched permissions
- Changed: distinguish configuration file open/read failures from XML syntax errors with line and column reporting (including invalid configuration values, finally!)
- Changed: generalize socket bind errno 13 message to formally reference elevated permissions (sudo, doas, etc.)
- Docs: improve README conciseness
- Docs: add disclaimer in README for breaking changes in pre-release builds
- Docs: add disclaimer for quick-start users to ensure they configure a fresh GPC hash salt
- Docs: improve CHANGELOG formatting, adhering loosely to [Keep A Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)

## [0.33.1] - 2026-10-11
- Migrate to new website URL ([https://mercury.wowtravis.com/](https://mercury.wowtravis.com/))

## [0.33.0] - 2026-10-06
- Improve request parsing and resilience to malformed requests
    - Reject invalid header names/values, duplicate Host/Content-Length/Transfer-Encoding headers, and invalid or overflowing Content-Length values
    - Limit request headers to 64 KiB and return 431 when exceeded
    - Reject unsupported request Transfer-Encoding with 400; chunked responses remain supported
    - Preserve pipelined requests and keep request bodies separate from subsequent requests
- Improve URI handling and access checks
    - Decode paths once and normalize separators and single-dot segments before checking access rules
    - Reject malformed percent escapes, encoded control characters in paths, and parent-directory traversal
    - Preserve encoded query strings through redirects, rewrites, and PHP-CGI
    - Prevent response-header injection and correctly handle literal percent signs in filenames
- Fix thread pool and logger synchronization and shutdown behavior
    - Preserve client IP addresses while connections wait for workers
    - Use thread-safe log timestamp formatting
- Fix incomplete socket writes, empty-response framing, and connection closure handling
- Fix keep-alive option parsing and honor Connection: close across repeated headers
- Fix HTTP/0.9 requests waiting for an extra header terminator
- Fix compression negotiation and handling of MIME types with parameters
- Preserve binary PHP-CGI request and response bodies
- Fix byte ranges for generated responses and clear stale range/compression headers on errors
- Improve file-stream error handling and temporary-file cleanup
- Remove unused methods and variables and improve code comments
- Expand JSON tests and add raw-socket regression coverage to the existing Python suite
    - Cover malformed headers, injection attempts, body framing, pipelining, and concurrent clients
    - Fix UTF-8 request lengths, fragmented-response reads, and boolean Content-Encoding expectations in the test runner
    - Add tests/README.md with setup instructions, JSON attributes, and guidance for extending coverage
- Fix documentation publishing with unchanged files and validation of fork pull requests
- Update configuration and contributor documentation

## [0.32.2] - 2026-10-05
- Bump OpenSSL to v4.0.3 (#478)

## [0.32.1] - 2026-10-05
- Bump OpenSSL to v4.0.2 (#475)
- README improvements

## [0.32.0] - 2026-08-04
- Add support for custom HTTP status & error pages (#448)
    - Each status document must be named as the 3-digit HTTP status code you'd like to override
        - E.g. /500.html, /403.html
    - File extensions for supported status documents must be .html files
    - Files must be immediate children under the document root
- Add EnableCustomStatusDocs config node to toggle support for aforementioned custom status pages (#448)

## [0.31.8] - 2026-06-24
- Bump PugiXML to v1.16 (#469)

## [0.31.7] - 2026-06-14
- Bump OpenSSL to v4.0.1 (#467)

## [0.31.6] - 2026-06-04
- Fix segmentation fault when fetching latest version w/o active internet connection (#464)

## [0.31.5] - 2026-04-20
- Bump OpenSSL to v4.0.0 (#460)

## [0.31.4] - 2026-04-12
- Bump OpenSSL to v3.6.2 (#459)

## [0.31.3] - 2026-04-08
- Improved resilience to path attacks
    - Now rejects requests with URI-encoded null bytes (%00 and %2500)

## [0.31.2] - 2026-04-08
- Now properly closes connections on HTTP/0.9 connections in the event of an error
    - Previously would send an error page HTML response
    - Performance boost for HTTP/0.9 errors from not loading error page HTML
- Address security hole from using PHP w/ legacy HTTP versions
    - Now returns 403 Forbidden (HTTP/1.0) or closes the connection (HTTP/0.9)

## [0.31.1] - 2026-04-07
- Updated ClientSecurityMode naming scheme (#449)
    - Renamed Anonymous/GPCAnonymous to Hashed/GPCHashed

## [0.31.0] - 2026-03-24
- Replaced RedactLogIPs with more advanced ClientSecurityMode in config file (#442)
    - Use ClientSecurityIPSalt to specify the hash salt (HMAC)

## [0.30.2] - 2026-02-23
- Fix minor memory leak with version checker (#437)
- Thread safety improvements to CGI handler fork() calls on Linux (#437)

## [0.30.1] - 2026-02-22
- Bump Zlib to v1.3.2

## [0.30.0] - 2026-02-06
- Improved CLI flush responsiveness (#431)
- Add "pwd" CLI command to print document root (#432)
- Added stderr log for bind failure if missing sudo

## [0.29.9] - 2026-02-01
- Bump OpenSSL to v3.6.1

## [0.29.8] - 2026-01-24
- Add optional donation banner (#414)
    - Added "donate" CLI command to display URL

## [0.29.7] - 2026-01-22
- Global documentation updates (per #257)

## [0.29.6] - 2026-01-14
- Improve clarity of TLS cert setup instructions

## [0.29.5] - 2026-01-14
- Remove `<format>` header in response handler
    - Allows for compiling on GCC 11/MinGW 8

## [0.29.4] - 2026-01-14
- Bump copyright year

## [0.29.3] - 2025-12-23
- Added Fedora Linux support
- Reworked static binaries
    - Appears to work between distros that have glibc version differences (e.g. Arch's glibc 2.42 and Ubuntu's glibc 2.39)

## [0.29.2] - 2025-12-13
- Slight improvements to Windows PHP CGI pipe handling

## [0.29.1] - 2025-12-12
- Remove PHP license since PHP has not been bundled since v0.14.0

## [0.29.0] - 2025-12-11
- Added checks for invalid config nodes (#363)

## [0.28.5] - 2025-12-11
- Correct minor inconsistency with license naming
    - Zstandard License -> BSD License in CREDITS.md

## [0.28.4] - 2025-11-16
- Improve documentation for cross-platform builds
- Fix generated README after refactor from v0.28.3
- Fix program hanging when attempting to close on Linux
- Add support for Kali Linux (#372)

## [0.28.3] - 2025-11-15
- Add unofficial support for Arch Linux (#372)
- Fix potential crash from PHP CGI on Arch Linux
- README improvements for cross-platform Linux builds

## [0.28.2] - 2025-11-12
- Windows PHP init script now fetches the latest PHP version (#374)

## [0.28.1] - 2025-11-12
- Updated README table for CLI commands
- Add note about outgoing connection to php.net
- Updated README to include note about CONFIG.md

## [0.28.0] - 2025-11-12
- Added rich CLI w/ command history (#347)
- Fixed sending Ctrl+C to Windows executable running via WSL
- Explicitly disabled Content-Encoding for byte range responses
    - If the Range header is present in a request, Content-Encoding is not honored

## [0.27.0] - 2025-11-11
- Added "status" command as an alias for "info" (#357)
- Added "phpinit" CLI command (#357)
    - Works for Windows and Linux
        - Invokes conf/setup_php.ps1 from the program on Windows
        - Runs `sudo apt install php-cgi` on Linux

## [0.26.0] - 2025-11-11
- Add CONFIG.md for config file documentation (#305)
- Add header matching support for Match nodes (#350)
- Extend MIME types list (#351)
- Improved internal path handling (#353)
- Fix using regex_search instead of regex_match for Redirect & Rewrite rules
- Rewrite and Redirect classes now have separate header files

## [0.25.0] - 2025-11-09
- Slightly reduced executable size
    - Removed unused -lbrotlidec library
- Update Brotli (v1.1.0 -> v1.2.0)
- Added safeguard against long URIs (#343)

## [0.24.0] - 2025-10-27
- Minor codebase improvements
    - Request::getHeader now returns std::optional instead of const std::string*
    - Migrated bulk setsockopt calls to macrodef
- Add rewrite engine (#245)
- Now decodes URIs in redirect (& rewrite) checks
- Now appends query strings to redirects & rewrites
- Added support for multiple list headers per RFC spec (#317)
    - Currently only for Accept, Accept-Encoding, Range
- Now uses Brotli instead of Zstandard for web files (#348)
    - Ex. HTML, CSS, and JS files
- Added application/wasm to default mimes.conf

## [0.23.10] - 2025-10-24
- Fixed crash when URI was a "?" (#344)
- Improved error logging for 500 Internal Server Error
- Now takes path to config file as an optional CMD argument
    - Should ideally only used by the testing script
    - File path is relative to the Mercury project root

## [0.23.9] - 2025-10-23
- Fixed byte range size calculations for FileStream objects (#338)
- Fixed byte ranges for PHP files (#339)
- Remove Range header support from HTTP/1.0
    - Didn't exist in spec

## [0.23.8] - 2025-10-22
- Switch to fully, locally static Zlib for Linux (#326)

## [0.23.7] - 2025-10-21
- Now properly intercepts SIGTERM on Linux & exits gracefully
- Now handles more Windows termination signals instead of POSIX ones
    - E.g. CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT

## [0.23.6] - 2025-10-20
- Debloated README (#307)
- Fixed min. compression size config control for HTTP/1.0 (#308)

## [0.23.5] - 2025-10-18
- Now resolves `//` as `/` in request paths (#298)
- Fixed Windows PHP setup script using relative `tmp` path (#299)
    - Previously broke file uploads
- Fixed HTTP body uploads for PHP files on Linux caused by pipe deadlock (#301)

## [0.23.4] - 2025-10-09
- Improve consistency of file paths in docs (#284)

## [0.23.3] - 2025-10-09
- Fix broken website hyperlink (#273)

## [0.23.2] - 2025-10-09
- Add Fun Facts section to README (#132)
- Fix typo in README Privacy Commitment (#267)
- Fix test suite config file path in README (#268)
- Fixed phrasing in CREDITS.md for PHP usage (#269)
- Normalized "Issue" and "Pull Request" capitalization in docs (#270)

## [0.23.1] - 2025-10-08
- Bump OpenSSL to v3.6.0 (#258)

## [0.23.0] - 2025-10-08
- Justified CLI table in README (#240)
- Updated Performance section in README to include what version was tested
- Add config controls for keep-alive connections & minimum response body size for compression (#237)
- Add Redirect node in config file (#170)
    - See example attached in /conf/default/mercury.conf
- Fixed Match node pattern lookups (previously was comparing empty strings)
- Fixed Match nodes requiring Access nodes within them
    - Was dereferencing a nullptr :(
- Access logs now include the raw incoming path instead of the parsed path
- Fixed URI-encoded question marks breaking query string parsing in Match (and the new Redirect) nodes (#246)

## [0.22.2] - 2025-10-03
- Removed update.ps1 and update.sh update scripts
    - Introduced a number of problems and disk I/O risks
    - Could lock up and crash if the directory was open by another program

## [0.22.1] - 2025-10-02 [YANKED]
- Minor performance boost for repeated mallocs with server buffers
- Added performance comparison w/ Apache to README (#220)
- Added "CLI" section to README Table of Contents (#236)
- Added Ztandard license

## [0.22.0] - 2025-09-29 [YANKED]
- Add thread-safe temp file tracker (#211)
- Fix printing extra "< " after a shutdown is requested
- Added temporary thread creation when the connection backlog grows (#225)
    - Greatly reduces latency under bursts or heavy load
- Added mention of testing suite DocumentRoot `./tests/files/` (#229)
- Update CONTRIBUTING.md with new Issue templates (#227)
- Added CLI commands (#226)
    - Documentation in README under "CLI"
- Added update script (#195)
    - update.ps1 on Windows
    - update.sh on Linux

## [0.21.0] - 2025-09-28
- Add support for running Mercury outside the `bin` directory (#215)
    - Working across UNC paths for Windows as well
- Added support for typing "exit" to exit (#221)
    - Greatly improve thread closure handling
- Now checks the individual version pattern (vX.X.X) when comparing against remote latest version (#195)
- Stylistically improved welcome banner :)

## [0.20.1] - 2025-09-28
- Fix Windows port checking if WinPHPCGIPath exists even if EnablePHPCGI is set to off (#217)

## [0.20.0] - 2025-09-28
- Migrate PugiXML to build_tools install with all other libs (#212)
- Add Zstandard (Zstd) compression (#159)
    - New compression preference order goes Zstd -> Brotli (if HTTPS) -> gzip -> deflate -> plaintext
- Update CREDITS.md to reflect PHP setup script from [v0.15.0](#v0150)
- Fix PugiXML license hyperlink in CREDITS.md

## [0.19.0] - 2025-09-28
- Added streaming for PHP CGI response bodies (#147)
- Added support for PHP path info URIs (#127)

## [0.18.4] - 2025-09-27
- Fixed directory index lookups on UNC paths (#201)
- Fixed directory index listings returning last modified GMT timestamps (#201)
- Fixed document root `.` not running in Mercury root (#200)
- Fixed temp files being created in the bin directory (#202)
- Migrate Contributing section to CONTRIBUTING.md (#197)
- Reviewed and updated SECURITY.md security policy (#199)

## [0.18.3] - 2025-09-26
- Removed canonicalization of tmp directory
- Added support for UNC paths in DocumentRoot (#186)
    - Allows Windows executable execution from within a WSL environment
- Fixed PHP scripts returning 204 No Content instead of 502 Bad Gateway on Linux if PHP-CGI is not installed (#183)
- Improved plaintext viewing for README.md (now README.txt) in releases (#176)
- Bump OpenSSL to v3.5.3 (#184)

## [0.18.2] - 2025-09-25
- Stylistic README updates
    - Added Support Mercury section
    - Cleaned Table of Contents
    - Added Privacy Commitment section
- Added RedactLogIPs config node (#169)
- Subtly improved error handling for mercury.conf with value hints

## [0.18.1] - 2025-09-24
- Fixed precompression making an extra temp file if no compression method was to be used (#166)
- More thoroughly removes temp files if precompression fails

## [0.18.0] - 2025-09-23
- Minor performance improvements for frequently used methods
- Improved default public HTML
    - Distinguished index.php from index.html
- Update default error doc & directory index HTML templates (#155)
- Improve directory index & error doc streaming (#148)
    - Now precompiled into a tmp file and buffered via FileStream
- Updated Windows PHP path to an archived 8.4.12 version
    - This recently broke as PHP updated to 8.4.13, but this archived version should remain parked

## [0.17.0] - 2025-09-17
- Fixed bad URI handling for legacy HTTP versions
- Dramatically reduced memory usage when parsing Requests
- Now skips Chunked Transfer Encoding for bodies smaller than the response buffer size
- Fixed sending Content-Encoding with precompressed bodies
    - Applies to HTTP/1.0 & sufficiently small HTTP/1.1 bodies
- Fixed sending Content-Length with Chunked Transfer Encoding responses
- Max response size now only applies to MemoryStreams, FileStreams are exempt (uses Chunked Transfer Encoding for large files)
- Now partially supports Range header (#142)
    - Returns 416 Range Not Satisfiable if attempting to use multipart byte ranges but WILL still merge overlapping ranges if possible
        - May look into multipart byte range support later
- Improved internal heap memory handling (#136)
- Windows PHP setup script now initializes php.ini
    - Now properly initializes PHP's upload_tmp_dir to tmp in the Mercury root

## [0.16.0] - 2025-09-15
- Refactored & streamlined config loader script (#128)
    - Now gracefully handles invalid formatting for lines in mimes.conf
- Extended index file support to allow multiple files (#137)
    - Renamed IndexFile -> IndexFiles
    - Contains a comma-separated list of all index file names to look for, in order left to right
    - Now allows specifying no index file
- Added Access node w/ Allow & Deny nodes for restricting access (#141)
- No longer requires ShowDirectoryIndexes node in Match nodes (defaults to true)
- Fixed Match patterns applying to full file paths, not relative paths
- Fixed IPv6 client IP resolution on Windows (previously worked only for ::1)
- Added BindAddressIPv4 and BindAddressIPv6 nodes to control bind address (#122)
    - Replaced EnableIPv4 and EnableIPv6 nodes

## [0.15.1] - 2025-09-08
- Remove unused string compression methods
- Remove unused brotli-cpp lib

## [0.15.0] - 2025-09-08
- Clarified supported Linux distributions in README & website (#133)
- Added TLS cert creation scripts to releases (#131)
    - Release builder now ships w/o default TLS certs
    - `conf/ssl/makecert.sh` for Linux
    - `conf/ssl/makecert.ps1` for Windows
- PHP is no longer bundled with Windows releases
    - To download PHP, run `conf/setup_php.ps1`
- Fix request body reading previously truncating with file uploads (#126)
    - Now respects and uses Content-Length header in requests to read whole body
- Now properly buffers incoming HTTP requests (#134)
    - MaxRequestBuffer node -> RequestBufferSize node in mercury.conf
    - Added MaxRequestBody node to specify how large a request's body can be
- Now properly buffers outgoing HTTP responses (#134)
    - Added ResponseBufferSize node in mercury.conf
    - Added MaxResponseBody node to specify how large a response's body can be
    - Body compression is now streamed via chunked transfer encoding (HTTP/1.1+ only)
- Fixed compression error handling (previously uncaught)
- Now gracefully exits if compression fails
- Added compression bypass for small bodies (#139)
- Fixed sending blank HTTP request bodies over TLS (#143)

## [0.14.0] - 2025-09-04
- Fully reworked PHP processing (#97)
    - Now uses traditional CGI
    - Removed PHP support for legacy HTTP versions (HTTP/0.9 and HTTP/1.0)
        - Mainly due to compatibility reasons (ie. lacking important headers)
    - Now supports Windows (PHP v8.4.12 comes bundled in `/php/` directory)
        - Added WinPHPCGIPath config node for Windows-only to point to php-cgi.exe, defaults to `./php/php-cgi.exe`
        - Alternative PHP CGIs can be downloaded from [www.php.net/downloads.php](https://www.php.net/downloads.php)
    - Now supports all CGI environment meta-variables per RFC 3875 (see https://datatracker.ietf.org/doc/html/rfc3875)
- Updated Server header syntax (Mercury vX.X.X -> Mercury/X.X.X)
- Fixed connections being closed when sending x-www-form-urlencoded HTTP body in requests
- Reviewed licenses after updating libs in [v0.9.2](#v092)
    - Updated Brotli license
    - Updated zlib license

## [0.13.1] - 2025-09-02
- Fix Response object headers not being overwritten if multiple of the same name are applied in succession
    - This causes the default MIME type for PHP scripts to not function as intended
- Add lazy MIME inferring for PHP CGI responses
- Removed extra debug log in CGI client
- Fixed HEAD requests w/ PHP-FPM returning no body internally
    - Resulted in Content-Length of 0 and broke early type inferring
- Now applies Match node headers for PHP files

## [0.13.0] - 2025-09-02
- PHP CGI improvements (#110)
    - Now initially sets the Content-Type if not received from PHP CGI (as fallback)
    - Force overrides the Content-Length header to the actual length of returned content
- Now ignores MIME checks over HTTP/0.9
- Now sends 204 No Content instead of 200 OK for OPTIONS method requests
- Now checks if the provided method is allowed BEFORE checking if the path exists (#110)
    - Returns 501 Not Implemented if method isn't supported for the HTTP version
    - Still returns 405 Method Not Allowed if method is allowed for the version but not on the resource
- Fix OPTIONS method support for server-wide asterisk (#115)
- Release bundler script now places files inside a directory within the release archive (#114)
- Add Windows executable icon (#116)

## [0.12.0] - 2025-08-26
- Add basic php-fpm support for Linux (#97)
    - Install via `sudo apt install php8.3-fpm`
- Bump C++ version from C++17 to C++20
- Add "Getting Started" section to README
- Fix explicitly declaring HTTP/0.9 as the version in the status line (#108)
- Fix connection: close handling for HTTP/1.1 (forcing as keep-alive) (#109)
- Fix access logging w/ HTTP/0.9 status codes (now prints all status codes as 0)
- Fix stoull throwing std::invalid_argument when passing non-numeric port in mercury.conf

## [0.11.0] - 2025-08-25
- Now checks for a new version at startup (#91)
    - Can be toggled off in mercury.conf
    - Can now toggle startup welcome banner
- Added support for toggling legacy HTTP version support (HTTP/0.9, HTTP/1.0) (#79)
- Now makes immediate log file directory if missing (#5)
- Updated default public HTML (#92)

## [0.10.0] - 2025-08-14
- Modularized request handlers (#39)
    - Added foundation for implementing future HTTP versions > HTTP/1.1
- Add support for HTTP/0.9 (finally closed #16!)

## [0.9.2] - 2025-08-14
- Bump Brotli to v1.1.0
- Bump OpenSSL to v3.5.2
- Bump zlib to v1.3.1

## [0.9.1] - 2025-08-10
- Fixed IPv6 for Windows (again)
    - Now zeroes out sockaddr_in6 which could sometimes fail by using garbage data as an invalid socket
- Now properly closes an IPv6 socket while trying to rebind in startup loop

## [0.9.0] - 2025-08-10
- Fixed IPv6 for Windows (#57)
- Now returns proper Allow header methods per the server's HTTP version (not hard-coded as a macrodef)
- Global codebase refactor (#51)
- Added precompiled headers to reduce compilation overhead (#37)
- Minor performance improvements (#51)

## [0.8.0] - 2025-08-07
- Fix cross-platform segmentation fault (#42)
    - Resulted from race condition when concurrently modifying STL structures (unordered_set, queue) across threads
- Implemented ThreadPool to prevent thread explosion
- Minor performance improvements

## [0.7.8] - 2025-08-05
- Remove OPTIONS method support from HTTP/1.0 (#40)
- Fix path checking logic before checking for invalid methods (#41)

## [0.7.7] - 2025-07-31
- Reduce excessive error logging
- Improved clarity of socket error & init logs (now includes TLS status & IPv4/IPv6 mode)
- Now writes access logs after processing the request, not before
    - Added request HTTP version
    - Added response status

## [0.7.6] - 2025-07-29
- Add support for toggling directory index listings in config file Match nodes
- Can now toggle IPv4 & IPv6 independently in config file
- Now explicitly rejects accessing symlinked content
    - Symlinking the document root itself is still allowed
    - Note than an Internal Server Error (500) may occur if attempting to access Linux symlinks on Windows

## [0.7.5] - 2025-07-24
- Add support for HTTP/1.0 ([#16](https://github.com/travis-heavener/mercury/issues/16))
- Normalized response header plaintext formatting (ex. cOntENt-LeNGTh --> Content-Length)
- Fix socket init failure handling (now properly exits if all sockets fail)
- Improved handling of individual main threads for each socket

## [0.7.4] - 2025-06-18
- Replaced existing logger w/ dedicated, thread-safe Logger class

## [0.7.3] - 2025-06-07
- Added Keep-Alive connection support
- Now detaches threads for each accepted client connection (vastly improves support for Keep-Alive connections)

## [0.7.2] - 2025-05-31
- Fixed bug where invalid/incomplete query strings would cause fatal crash

## [0.7.1] - 2025-05-31
- Improved support against CRLF injection
- Force CRLF for request & response

## [0.7.0] - 2025-05-29
- Fixed major path injection bug
- Added explicit support for forward slashes in document roots
- Fixed status code handling when text/html isn't accepted as a MIME
- Improved support with Windows-style directory separators '\\'
- Now enforces HTTP/1.1 and returns 505 status when there's a version mismatch

## [0.6.4] - 2025-05-28
- Refactored executable names (main -> mercury)

## [0.6.3] - 2025-04-26
- Add MIT license

## [0.6.2] - 2025-04-26
- Fully migrated to static Linux binaries
- Port OpenSSL for Windows (now allows TLS certs)
- Port Brotli for Windows (now allows Brotli compression)
- All build tools are now shell scripts!

## [0.6.1] - 2025-04-26
- Fix connections hanging on Windows port & handling of client sockets closing
    - Now polls client sockets for incoming data

## [0.6.0] - 2025-04-26
- Add IPv6 support (fixes additional overhead from localhost resolution in Chrome)
- Increase max request backlog size from 25 to 100
- Fix error log buffer not flushing
- Remove Host node from config file

## [0.5.1] - 2025-04-26
- Add support for If-Modified-Since headers and 304 Not Modified responses

## [0.5.0] - 2025-04-25
- Add HEAD verb support
- Add OPTIONS verb support
- Fix handling of invalid methods (405 status code)

## [0.4.3] - 2025-04-25
- Add Date header to outgoing requests

## [0.4.2] - 2025-04-25
- Fix RFC2616 compliance by enforcing Host header on requests

## [0.4.1] - 2025-04-25
- Add Last-Modified header built-in support for GET requests

## [0.4.0] - 2025-04-25
- Add support for Brotli compression (only over HTTPS)
- Enabled compression for non-text MIMEs
- Revert Linux static build (resolves OpenSSL errors)

## [0.3.3] - 2025-04-25
- Fix errno 13 additional error text

## [0.3.2] - 2025-04-25
- Consolidate response handling into Response class (similar to Request)
- Update build info w/ SSL

## [0.3.1] - 2025-04-25
- Add default config

## [0.3.0] - 2025-04-25
- Add OpenSSL support for Linux builds
    - See conf/ssl/cert.pem and conf/ssl/key.pem
    - Uses multithreaded sockets

## [0.2.8] - 2025-04-23
- Switch to static build for Linux

## [0.2.7] - 2025-02-14
- File I/O improvements
- Improved latency on systems with slower disk I/O speeds

## [0.2.6] - 2025-02-14
- Improved handling of blank packets

## [0.2.5] - 2025-01-30
- Added IndexFile config directive

## [0.2.4] - 2025-01-22
- Added access & error log files
- Updated default config file headers
- Added icon & favicon for default HTML sites

## [0.2.3] - 2025-01-22
- Extended control of request buffer size & backlog to config file

## [0.2.2] - 2025-01-22
- Extended content encoding support for both gzip & zlib deflate
- Fixed terminology of Host node in config file
- Global refactor of C++ source files & binaries

## [0.2.1] - 2025-01-22
- Improved parsing support for Accept header

## [0.2.0] - 2025-01-22
- **URI decoding is now supported**
- Added directory index file listings
- Fixed Windows bug where Document Root not terminated by a forward slash fails to recognize index.html in root directory

## [0.1.8] - 2025-01-21
- Improved handling of program exits
- Fixed memory leak w/ deflateText calling delete instead of delete[]
- Fixed major memory leak w/ deflateText calling deflateInit2_ twice
- Refactored string tools
- Added Match & Header nodes to config

## [0.1.7] - 2025-01-21
- Fixed config loader error checking
- Added Host node to config data to control hosted IPv4 address
- Welcome message now shows after config data loads

## [0.1.6] - 2025-01-21
- Fixed [#3](https://github.com/travis-heavener/mercury/issues/3) (socket bind failure in immediate re-execution)
- Added Table of Contents ([#4](https://github.com/travis-heavener/mercury/issues/4))

## [0.1.5] - 2025-01-21
- Added mercury.conf configuration XML file
    - Config file has logfile directives that don't currently do anything yet
    - Parsed by [PugiXML](https://github.com/zeux/pugixml)!
- Refactored & reduced unused methods
    - Known MIME types are now loaded by the loadConfig method in conf.hpp

## [0.1.4] - 2025-01-21
- Accept header now processes `*/*` as intended
- Now explicitly prevents users from accessing files above the document root
- Improved handling of 405 Method Not Allowed status codes
    - Response now appropriately specifies Allow header
    - Now only returns HTML display if explicitly Accepted by the request

## [0.1.3] - 2025-01-21
- Fixed MAJOR file compression bug on Windows systems
- Improved overall handling of file compression

## [0.1.2] - 2025-01-21
- File paths with query strings are now handled as intended
- Improved handling of index files

## [0.1.1] - 2025-01-21
- Added initial welcome note
- Fixed [Dependencies](README.md#build-info) with Windows info
- Windows port now allows deflate compresesion via zlib port

## [0.1.0] - 2025-01-21
- Added changelog (lol)
- Added Windows port
