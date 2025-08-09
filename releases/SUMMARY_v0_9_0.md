# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `aa6c92c0d104f7a2423312cad20658dd85afa0096de0353a803f3b4db04f4d86` |
| Windows | `68b4ac683660dc2f0f1714aeb387e6a9130927a0bccbaeeb1073cbfb9130a7e3` |