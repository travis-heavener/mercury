# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `2b0b9dba88ee35abef3379526e8dc738d38431db1652b55789742fcfe8fb9b15` |
| Windows | `9dd84c7aa41190885f2fb8d9f2c17ea4ef1b666de693a8d22032c9437f4a9f72` |