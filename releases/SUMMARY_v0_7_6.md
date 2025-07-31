# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `d639e7c76ffb346306daf0ff9b274086334be76221f9bb5da570e6274845d761` |
| Windows | `44452c16d4450941022140819b2dfda98732187abe9b225a8179e646459697df` |