# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `805ad618197f4e18e06fc70aa89438f670a6183b6bf443832722057493a9f212` |
| Windows | `c046d12687fa32c5aee5aef387ac9b6ca10f25eae14651dae583114543bc3088` |