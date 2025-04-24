# Changelog

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