# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `a51a67e3f5c0d409e2c1f4a903c752f12547fd8dc0f214daf738874f669725e1` |
| Windows | `b66477bc7b48f2cf3d6e80c81ae28d591ed1184a4991165ef75b123f1b92a656` |