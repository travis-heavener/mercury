# Changelog
### [Read Full Changelog](https://github.com/travis-heavener/mercury/blob/main/CHANGELOG.md)

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

# SHA-256 Hashes
| System | SHA-256 Hash Digest |
|--------|---------------------|
| Linux | `4b5af6e118154dc9937ce31f571a3192e875ffac32b7d3b7e94595001ee121a4` |
| Windows | `8b470eaf334f713d51a3ee62f752ca527787cbbaa046e52f99c59839330526ae` |