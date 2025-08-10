# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `428b55d9fd1d98bd1628398fa1ac77aae72e5ccee1a5091b16ed368396d9a588` |
| Windows | `9dde0576c8a23bceb335bd97e450137fd2705c253aa2ecf3665947b32445ed8a` |