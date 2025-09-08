# Changelog

## v0.16.0
- Refactored & streamlined config loader script (#128)

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
