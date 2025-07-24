# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

## v0.7.5
- Add support for HTTP/1.0 ([#16](https://github.com/travis-heavener/mercury/issues/16))
- Normalized response header plaintext formatting (ex. cOntENt-LeNGTh --> Content-Length)

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
| Linux | `a37e73e4791f9058b96fd379cdea8c251fa0da6d3e560bdb16feacb9b2850f5d` |
| Windows | `4a5e7d2cdc6de7548be10473649aabbb0912c96b73634926439dc609dda5b2ae` |