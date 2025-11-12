# Changelog

## v0.28.1
- Updated README table for CLI commands

## v0.28.0
- Added rich CLI w/ command history (#347)
- Fixed sending Ctrl+C to Windows executable running via WSL
- Explicitly disabled Content-Encoding for byte range responses
    - If the Range header is present in a request, Content-Encoding is not honored

## v0.27.0
- Added "status" command as an alias for "info" (#357)
- Added "phpinit" CLI command (#357)
    - Works for Windows and Linux
        - Invokes conf/setup_php.ps1 from the program on Windows
        - Runs `sudo apt install php-cgi` on Linux

## v0.26.0
- Add CONFIG.md for config file documentation (#305)
- Add header matching support for Match nodes (#350)
- Extend MIME types list (#351)
- Improved internal path handling (#353)
- Fix using regex_search instead of regex_match for Redirect & Rewrite rules
- Rewrite and Redirect classes now have separate header files

## v0.25.0
- Slightly reduced executable size
    - Removed unused -lbrotlidec library
- Update Brotli (v1.1.0 -> v1.2.0)
- Added safeguard against long URIs (#343)

## v0.24.0
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

## v0.23.10
- Fixed crash when URI was a "?" (#344)
- Improved error logging for 500 Internal Server Error
- Now takes path to config file as an optional CMD argument
    - Should ideally only used by the testing script
    - File path is relative to the Mercury project root

## v0.23.9
- Fixed byte range size calculations for FileStream objects (#338)
- Fixed byte ranges for PHP files (#339)
- Remove Range header support from HTTP/1.0
    - Didn't exist in spec

## v0.23.8
- Switch to fully, locally static Zlib for Linux (#326)

## v0.23.7
- Now properly intercepts SIGTERM on Linux & exits gracefully
- Now handles more Windows termination signals instead of POSIX ones
    - E.g. CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT

## v0.23.6
- Debloated README (#307)
- Fixed min. compression size config control for HTTP/1.0 (#308)

## v0.23.5
- Now resolves `//` as `/` in request paths (#298)
- Fixed Windows PHP setup script using relative `tmp` path (#299)
    - Previously broke file uploads
- Fixed HTTP body uploads for PHP files on Linux caused by pipe deadlock (#301)

## v0.23.4
- Improve consistency of file paths in docs (#284)

## v0.23.3
- Fix broken website hyperlink (#273)

## v0.23.2
- Add Fun Facts section to README (#132)
- Fix typo in README Privacy Commitment (#267)
- Fix test suite config file path in README (#268)
- Fixed phrasing in CREDITS.md for PHP usage (#269)
- Normalized "Issue" and "Pull Request" capitalization in docs (#270)

## v0.23.1
- Bump OpenSSL to v3.6.0 (#258)

## v0.23.0
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

## v0.22.2
- Removed update.ps1 and update.sh update scripts
    - Introduced a number of problems and disk I/O risks
    - Could lock up and crash if the directory was open by another program

## v0.22.1
- Minor performance boost for repeated mallocs with server buffers
- Added performance comparison w/ Apache to README (#220)
- Added "CLI" section to README Table of Contents (#236)
- Added Ztandard license

## v0.22.0
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

## v0.21.0
- Add support for running Mercury outside the `bin` directory (#215)
    - Working across UNC paths for Windows as well
- Added support for typing "exit" to exit (#221)
    - Greatly improve thread closure handling
- Now checks the individual version pattern (vX.X.X) when comparing against remote latest version (#195)
- Stylistically improved welcome banner :)

## v0.20.1
- Fix Windows port checking if WinPHPCGIPath exists even if EnablePHPCGI is set to off (#217)

## v0.20.0
- Migrate PugiXML to build_tools install with all other libs (#212)
- Add Zstandard (Zstd) compression (#159)
    - New compression preference order goes Zstd -> Brotli (if HTTPS) -> gzip -> deflate -> plaintext
- Update CREDITS.md to reflect PHP setup script from [v0.15.0](#v0150)
- Fix PugiXML license hyperlink in CREDITS.md

## v0.19.0
- Added streaming for PHP CGI response bodies (#147)
- Added support for PHP path info URIs (#127)

## v0.18.4
- Fixed directory index lookups on UNC paths (#201)
- Fixed directory index listings returning last modified GMT timestamps (#201)
- Fixed document root `.` not running in Mercury root (#200)
- Fixed temp files being created in the bin directory (#202)
- Migrate Contributing section to CONTRIBUTING.md (#197)
- Reviewed and updated SECURITY.md security policy (#199)

## v0.18.3
- Removed canonicalization of tmp directory
- Added support for UNC paths in DocumentRoot (#186)
    - Allows Windows executable execution from within a WSL environment
- Fixed PHP scripts returning 204 No Content instead of 502 Bad Gateway on Linux if PHP-CGI is not installed (#183)
- Improved plaintext viewing for README.md (now README.txt) in releases (#176)
- Bump OpenSSL to v3.5.3 (#184)

## v0.18.2
- Stylistic README updates
    - Added Support Mercury section
    - Cleaned Table of Contents
    - Added Privacy Commitment section
- Added RedactLogIPs config node (#169)
- Subtly improved error handling for mercury.conf with value hints

## v0.18.1
- Fixed precompression making an extra temp file if no compression method was to be used (#166)
- More thoroughly removes temp files if precompression fails

## v0.18.0
- Minor performance improvements for frequently used methods
- Improved default public HTML
    - Distinguished index.php from index.html
- Update default error doc & directory index HTML templates (#155)
- Improve directory index & error doc streaming (#148)
    - Now precompiled into a tmp file and buffered via FileStream
- Updated Windows PHP path to an archived 8.4.12 version
    - This recently broke as PHP updated to 8.4.13, but this archived version should remain parked

## v0.17.0
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

## v0.16.0
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

## v0.15.1
- Remove unused string compression methods
- Remove unused brotli-cpp lib

## v0.15.0
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

## v0.14.0
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

## v0.13.1
- Fix Response object headers not being overwritten if multiple of the same name are applied in succession
    - This causes the default MIME type for PHP scripts to not function as intended
- Add lazy MIME inferring for PHP CGI responses
- Removed extra debug log in CGI client
- Fixed HEAD requests w/ PHP-FPM returning no body internally
    - Resulted in Content-Length of 0 and broke early type inferring
- Now applies Match node headers for PHP files

## v0.13.0
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

## v0.12.0
- Add basic php-fpm support for Linux (#97)
    - Install via `sudo apt install php8.3-fpm`
- Bump C++ version from C++17 to C++20
- Add "Getting Started" section to README
- Fix explicitly declaring HTTP/0.9 as the version in the status line (#108)
- Fix connection: close handling for HTTP/1.1 (forcing as keep-alive) (#109)
- Fix access logging w/ HTTP/0.9 status codes (now prints all status codes as 0)
- Fix stoull throwing std::invalid_argument when passing non-numeric port in mercury.conf

## v0.11.0
- Now checks for a new version at startup (#91)
    - Can be toggled off in mercury.conf
    - Can now toggle startup welcome banner
- Added support for toggling legacy HTTP version support (HTTP/0.9, HTTP/1.0) (#79)
- Now makes immediate log file directory if missing (#5)
- Updated default public HTML (#92)

## v0.10.0
- Modularized request handlers (#39)
    - Added foundation for implementing future HTTP versions > HTTP/1.1
- Add support for HTTP/0.9 (finally closed #16!)

## v0.9.2
- Bump Brotli to v1.1.0
- Bump OpenSSL to v3.5.2
- Bump zlib to v1.3.1

## v0.9.1
- Fixed IPv6 for Windows (again)
    - Now zeroes out sockaddr_in6 which could sometimes fail by using garbage data as an invalid socket
- Now properly closes an IPv6 socket while trying to rebind in startup loop

## v0.9.0
- Fixed IPv6 for Windows (#57)
- Now returns proper Allow header methods per the server's HTTP version (not hard-coded as a macrodef)
- Global codebase refactor (#51)
- Added precompiled headers to reduce compilation overhead (#37)
- Minor performance improvements (#51)

## v0.8.0
- Fix cross-platform segmentation fault (#42)
    - Resulted from race condition when concurrently modifying STL structures (unordered_set, queue) across threads
- Implemented ThreadPool to prevent thread explosion
- Minor performance improvements

## v0.7.8
- Remove OPTIONS method support from HTTP/1.0 (#40)
- Fix path checking logic before checking for invalid methods (#41)

## v0.7.7
- Reduce excessive error logging
- Improved clarity of socket error & init logs (now includes TLS status & IPv4/IPv6 mode)
- Now writes access logs after processing the request, not before
    - Added request HTTP version
    - Added response status

## v0.7.6
- Add support for toggling directory index listings in config file Match nodes
- Can now toggle IPv4 & IPv6 independently in config file
- Now explicitly rejects accessing symlinked content
    - Symlinking the document root itself is still allowed
    - Note than an Internal Server Error (500) may occur if attempting to access Linux symlinks on Windows

## v0.7.5
- Add support for HTTP/1.0 ([#16](https://github.com/travis-heavener/mercury/issues/16))
- Normalized response header plaintext formatting (ex. cOntENt-LeNGTh --> Content-Length)
- Fix socket init failure handling (now properly exits if all sockets fail)
- Improved handling of individual main threads for each socket

## v0.7.4
- Replaced existing logger w/ dedicated, thread-safe Logger class

## v0.7.3
- Added Keep-Alive connection support
- Now detaches threads for each accepted client connection (vastly improves support for Keep-Alive connections)

## v0.7.2
- Fixed bug where invalid/incomplete query strings would cause fatal crash

## v0.7.1
- Improved support against CRLF injection
- Force CRLF for request & response

## v0.7.0
- Fixed major path injection bug
- Added explicit support for forward slashes in document roots
- Fixed status code handling when text/html isn't accepted as a MIME
- Improved support with Windows-style directory separators '\\'
- Now enforces HTTP/1.1 and returns 505 status when there's a version mismatch

## v0.6.4
- Refactored executable names (main -> mercury)

## v0.6.3
- Add MIT license

## v0.6.2
- Fully migrated to static Linux binaries
- Port OpenSSL for Windows (now allows TLS certs)
- Port Brotli for Windows (now allows Brotli compression)
- All build tools are now shell scripts!

## v0.6.1
- Fix connections hanging on Windows port & handling of client sockets closing
    - Now polls client sockets for incoming data

## v0.6.0
- Add IPv6 support (fixes additional overhead from localhost resolution in Chrome)
- Increase max request backlog size from 25 to 100
- Fix error log buffer not flushing
- Remove Host node from config file

## v0.5.1
- Add support for If-Modified-Since headers and 304 Not Modified responses

## v0.5.0
- Add HEAD verb support
- Add OPTIONS verb support
- Fix handling of invalid methods (405 status code)

## v0.4.3
- Add Date header to outgoing requests

## v0.4.2
- Fix RFC2616 compliance by enforcing Host header on requests

## v0.4.1
- Add Last-Modified header built-in support for GET requests

## v0.4.0
- Add support for Brotli compression (only over HTTPS)
- Enabled compression for non-text MIMEs
- Revert Linux static build (resolves OpenSSL errors)

## v0.3.3
- Fix errno 13 additional error text

## v0.3.2
- Consolidate response handling into Response class (similar to Request)
- Update build info w/ SSL

## v0.3.1
- Add default config

## v0.3.0
- Add OpenSSL support for Linux builds
    - See conf/ssl/cert.pem and conf/ssl/key.pem
    - Uses multithreaded sockets

## v0.2.8
- Switch to static build for Linux

## v0.2.7
- File I/O improvements
- Improved latency on systems with slower disk I/O speeds

## v0.2.6
- Improved handling of blank packets

## v0.2.5
- Added IndexFile config directive

## v0.2.4
- Added access & error log files
- Updated default config file headers
- Added icon & favicon for default HTML sites

## v0.2.3
- Extended control of request buffer size & backlog to config file

## v0.2.2
- Extended content encoding support for both gzip & zlib deflate
- Fixed terminology of Host node in config file
- Global refactor of C++ source files & binaries

## v0.2.1
- Improved parsing support for Accept header

## v0.2.0
- **URI decoding is now supported**
- Added directory index file listings
- Fixed Windows bug where Document Root not terminated by a forward slash fails to recognize index.html in root directory

## v0.1.8
- Improved handling of program exits
- Fixed memory leak w/ deflateText calling delete instead of delete[]
- Fixed major memory leak w/ deflateText calling deflateInit2_ twice
- Refactored string tools
- Added Match & Header nodes to config

## v0.1.7
- Fixed config loader error checking
- Added Host node to config data to control hosted IPv4 address
- Welcome message now shows after config data loads

## v0.1.6
- Fixed [#3](https://github.com/travis-heavener/mercury/issues/3) (socket bind failure in immediate re-execution)
- Added Table of Contents ([#4](https://github.com/travis-heavener/mercury/issues/4))

## v0.1.5
- Added mercury.conf configuration XML file
    - Config file has logfile directives that don't currently do anything yet
    - Parsed by [PugiXML](https://github.com/zeux/pugixml)!
- Refactored & reduced unused methods
    - Known MIME types are now loaded by the loadConfig method in conf.hpp

## v0.1.4
- Accept header now processes `*/*` as intended
- Now explicitly prevents users from accessing files above the document root
- Improved handling of 405 Method Not Allowed status codes
    - Response now appropriately specifies Allow header
    - Now only returns HTML display if explicitly Accepted by the request

## v0.1.3
- Fixed MAJOR file compression bug on Windows systems
- Improved overall handling of file compression

## v0.1.2
- File paths with query strings are now handled as intended
- Improved handling of index files

## v0.1.1
- Added initial welcome note
- Fixed [Dependencies](README.md#build-info) with Windows info
- Windows port now allows deflate compresesion via zlib port

## v0.1.0
- Added changelog (lol)
- Added Windows port
