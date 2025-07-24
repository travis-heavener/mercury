# Changelog

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
