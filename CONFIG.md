# CONFIG.md

The Mercury HTTP server provides a highly customizable configuration interface in `conf/mercury.conf`.

All configuration nodes must be wrapped within a singular `<Mercury>` root node. All child nodes are optional; any omitted setting automatically falls back to its documented default value. Command-line flags (such as `-p, --port` and `-r, --root`) take precedence over both configured and default values.

## Table of Contents

### General Config
- [DocumentRoot](#documentroot)
- [BindAddressIPv4](#bindaddressipv4)
- [BindAddressIPv6](#bindaddressipv6)
- [Port](#port)
- [TLSPort](#tlsport)
- [Redirect](#redirect)
- [Rewrite](#rewrite)

### Logging
- [AccessLogFile](#accesslogfile)
- [ErrorLogFile](#errorlogfile)
- [ClientSecurityMode](#clientsecuritymode)
- [ClientSecurityIPSalt](#clientsecurityipsalt)

### PHP
- [EnablePHPCGI](#enablephpcgi)
- [CGITimeout](#cgitimeout)
- [WinPHPCGIPath](#winphpcgipath)
- [EnableFastCGI](#enablefastcgi)
- [FastCGIAddress](#fastcgiaddress)
- [FastCGITimeout](#fastcgitimeout)

### Matching & Conditional Access Control
- [EnableLegacyHTTPVersions](#enablelegacyhttpversions)
- [Match](#match)
    - [FilterIfHeaderMatch](#match--filterifheaderexist)
    - [FilterIfNotHeaderMatch](#match--filterifnotheadermatch)
    - [FilterIfHeaderExist](#match--filterifheaderexist)
    - [FilterIfNotHeaderExist](#match--filterifnotheaderexist)
    - [Header](#match--header)
    - [ShowDirectoryIndexes](#match--showdirectoryindexes)
    - [Access](#match--access)

### HTTP Behavior
- [KeepAlive](#keepalive)
- [KeepAliveMaxTimeout](#keepalivemaxtimeout)
- [KeepAliveMaxRequests](#keepalivemaxrequests)
- [EnableCustomStatusDocs](#enablecustomstatusdocs)

### File & Socket I/O
- [IndexFiles](#indexfiles)
- [MaxRequestLineLength](#maxrequestlinelength)
- [MaxRequestBacklog](#maxrequestbacklog)
- [RequestBufferSize](#requestbuffersize)
- [ResponseBufferSize](#responsebuffersize)
- [MaxRequestBody](#maxrequestbody)
- [MaxResponseBody](#maxresponsebody)

### Performance
- [MinResponseCompressionSize](#minresponsecompressionsize)
- [IdleThreadsPerChild](#idlethreadsperchild)
- [MaxThreadsPerChild](#maxthreadsperchild)

### Misc.
- [ShowWelcomeBanner](#showwelcomebanner)
- [ShowDonationBanner](#showdonationbanner)
- [ShowPrivilegeDeescalationMessage](#showprivilegedeescalationmessage)
- [StartupCheckLatestRelease](#startupchecklatestrelease)

# Config

### DocumentRoot
Specifies where files are served from, relative to the Mercury directory. Can be overridden on the command line via `-r, --root <path>`.

Default: `./public/`

Example:

```xml
<DocumentRoot> ./public/ </DocumentRoot>
```

### BindAddressIPv4
Controls the socket bind address for IPv4, or "off" if disabled.

Default: `0.0.0.0`

Example:

```xml
<BindAddressIPv4> 0.0.0.0 </BindAddressIPv4>
```

### BindAddressIPv6
Controls the socket bind address for IPv6, or "off" if disabled.

Default: `::`

Example:

```xml
<BindAddressIPv6> :: </BindAddressIPv6>
```

### Port
Specifies what port will be used to serve cleartext (non-HTTPS) content (valid range: 1–65535). Can be overridden on the command line via `-p, --port <port>`.

Default: `80`

Example:

```xml
<Port> 80 </Port>
```

### TLSPort
Specifies which port to serve SSL/TLS traffic over (valid range: 1–65535), or "off" if disabled.

See `conf/ssl/cert.pem` and `conf/ssl/key.pem`.

Default: `off`

Example:

```xml
<TLSPort> off </TLSPort>
```

### EnableLegacyHTTPVersions
Enables or disables legacy HTTP versions (HTTP/0.9, HTTP/1.0).

Note that the minimum HTTP version supported for PHP responses is HTTP/1.1.
**When PHP is enabled** and the client requests a PHP file on HTTP/0.9, the connection is closed, and on HTTP/1.0 a 403 Forbidden status is returned.
This prevents leaking sensitive PHP source code as static files, however if PHP is disabled then PHP source files are returned in responses **regardless of HTTP version**.

Default: `on`

Example:

```xml
<EnableLegacyHTTPVersions> on </EnableLegacyHTTPVersions>
```

### IndexFiles
Specifies what name for index files will be served when accessing a directory by name.

The order (left to right) specifies which files will be searched for first, or empty if desired (e.g. `<IndexFiles></IndexFiles>`). If this node is omitted, the default list is used.

Default: `index.html, index.htm, index.php`

Example:

```xml
<IndexFiles> index.html, index.htm, index.php </IndexFiles>
```

### AccessLogFile
Specifies where server access logs are stored.

Default: `./logs/access.log`

Example:

```xml
<AccessLogFile> ./logs/access.log </AccessLogFile>
```

### ErrorLogFile
Specifies where server error logs are stored.

Default: `./logs/error.log`

Example:

```xml
<ErrorLogFile> ./logs/error.log </ErrorLogFile>
```

### ClientSecurityMode
Controls how incoming client IP addresses are logged.

- Minimal: doesn't obfuscate **ANY** client IPs from logs
- GPCMasked: doesn't obfuscate any client IPs from logs **EXCEPT** those with a DNT/Sec-GPC header, zeroing the last few bits (8 for IPv4, 80 for IPv6)
- GPCHashed: same as GPCMasked except DNT/Sec-GPC client IPs are completely anomymized by logging a hash of their IP (useful for rate limiting via hash comparison but doesn't reveal their IP directly)
- Masked: **ALL** client IPs have their last few bits masked in logs (8 for IPv4, 80 for IPv6)
- Hashed: **ALL** client IPs are hashed in logs

Default: `GPCMasked`

Example:

```xml
<ClientSecurityMode> GPCMasked </ClientSecurityMode>
```

### ClientSecurityIPSalt
The salt for the ClientSecurityMode hashing algorithm for anonymous IP addresses (must be at least 16 characters / 128 bits).

Default: `fAhGD0OnCe4YhSyx`, although **this value is arbitrary and should be changed for production**

Example:

```xml
<ClientSecurityIPSalt> CXt6NX36UdsVCl5o </ClientSecurityIPSalt>
```

### EnablePHPCGI
Whether or not to pass through requests for PHP files to a PHP-CGI process.

`WinPHPCGIPath` is used when either `EnablePHPCGI` or `EnableFastCGI` is enabled.

Default: `off`

Example:

```xml
<EnablePHPCGI> off </EnablePHPCGI>
```

### CGITimeout
Sets total execution timeout in seconds for each traditional PHP-CGI request on Linux.

The timeout covers request-body delivery, stdout/stderr capture, and worker exit.
Timed-out workers are stopped and the request returns `504 Gateway Timeout`.
PHP diagnostics are written to the error log, separately from response content.

NOTE: This node applies to Linux CGI only. FastCGI uses `FastCGITimeout`.

Default: `30`

Example:

```xml
<CGITimeout> 30 </CGITimeout>
```

### WinPHPCGIPath
Points to the `php-cgi.exe` executable used for CGI requests and automatic Windows FastCGI startup.

`conf/setup_php.ps1` generates two PHP configurations beside this executable:

- `php.ini`: Used by FastCGI, with OPcache enabled
- `mercury-cgi.ini`: Used by legacy CGI, with OPcache unloaded to avoid Windows worker crashes

Edit the corresponding INI file to customize each mode. Legacy CGI ignores additional INI scan directories when `mercury-cgi.ini` exists; otherwise, it uses PHP's normal configuration lookup.

NOTE: This node applies to Windows only.

Default: `./php/php-cgi.exe`

Example:

```xml
<WinPHPCGIPath> ./php/php-cgi.exe </WinPHPCGIPath>
```

### EnableFastCGI
Whether or not to send PHP requests to a FastCGI backend instead of starting a CGI process for each request.

When enabled:

- FastCGI takes precedence over `EnablePHPCGI`. Backend failures do not fall back to CGI.
- PHP execution remains limited to HTTP/1.1. HTTP/1.0 and HTTP/0.9 PHP requests return `403`.
- **Windows:** Mercury reuses an existing listener at `127.0.0.1` or `localhost`, or starts `WinPHPCGIPath` with `-b` on the configured port. Mercury stops only the PHP process it started when Mercury exits.
- **Linux:** With `FastCGIAddress` omitted or set to `auto`, Mercury starts a private PHP-FPM pool as its runtime user and stops it on shutdown. Explicit addresses use separately managed backends.

NOTE: Linux automatic startup requires `php-fpm` to be installed and a non-root runtime user. Mercury drops sudo/doas privileges to the invoking account before starting PHP-FPM. No system service, file ownership, or socket ACL changes are required.

NOTE: Windows endpoints other than `127.0.0.1` or `localhost` require a separately started backend. UNIX domain sockets are supported on Linux only.

Default: `off`

Example:

```xml
<EnableFastCGI> on </EnableFastCGI>
```

### FastCGIAddress
Specifies where Mercury connects to the FastCGI backend. Used when `EnableFastCGI` is enabled.

Supported formats:

- `auto`: On Linux, starts a private PHP-FPM pool using the highest-version executable found in PATH and standard install directories. Mercury creates a private temporary socket and configuration. On Windows, uses `127.0.0.1:9000`.
- UNIX domain socket: `unix:/path/to/socket.sock` or `/path/to/socket.sock` (Linux only).
- TCP socket: `host:port`, `ip:port`, or `[IPv6]:port`, e.g. `localhost:9000`, `127.0.0.1:9000`, or `[::1]:9000`.

For an explicitly configured Linux backend, match its pool's `listen` setting. Mercury must be able to access the socket, and the pool's worker user must be able to read your PHP files. Explicit addresses are never automatically started or stopped.

If automatic startup fails, Mercury prints diagnostics and PHP requests return `502`; it does not switch to a system pool. Static files remain available.

Default: `auto`

Example (automatic Linux development pool):

```xml
<EnableFastCGI> on </EnableFastCGI>
<FastCGIAddress> auto </FastCGIAddress>
```

Example (externally managed Linux pool):

```xml
<FastCGIAddress> unix:/run/php/php8.3-fpm.sock </FastCGIAddress>
```

Example (Windows or Linux):

```xml
<FastCGIAddress> 127.0.0.1:9000 </FastCGIAddress>
```

### FastCGITimeout
Timeout in seconds for FastCGI socket reads and writes. A timed-out PHP request returns `504 Gateway Timeout`.

Must be a positive whole number. Used when `EnableFastCGI` is enabled.

Default: `10`

Example:

```xml
<FastCGITimeout> 10 </FastCGITimeout>
```

### Match
Match allows individual file/directory control over resource access via regex matches for its `pattern` attribute.

Note that the pattern matches the URI w/o the query string from the HTTP request, NOT an absolute path.

The URI is percent-decoded once before matching. Backslashes and repeated slashes are normalized to forward slashes, and single-dot path segments are removed before access checks. Parent-directory segments (`..`), malformed percent escapes, and decoded control characters are rejected. A double-encoded escape remains literal text after the single decoding pass (e.g. `%2520` becomes `%20`, not a space).

Default: *None* (no routing or header rules defined)

Example:

```xml
<Match pattern="^.*$">
    <Header name="Cache-Control"> public, must-revalidate, max-age=300 </Header>
    <ShowDirectoryIndexes> on </ShowDirectoryIndexes>
    <Access mode="deny all">
        <Allow> 127.0.0.1 </Allow>
        <Allow> ::1 </Allow>
    </Access>
</Match>
```

### Match > FilterIfHeaderMatch
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which have a header that matches the following pattern (Regex match).

Note: header names are case insensitive.

Default: *None* (all matching URIs are processed regardless of header values)

Example:

```xml
<Match pattern="^.*$">
    <FilterIfHeaderMatch name="X-Header-To-Match" pattern="text\/plain; charset\=utf\-8" />
</Match>
```

### Match > FilterIfNotHeaderMatch
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which have a header that does not match the following pattern (Regex match).

Note: header names are case insensitive.

Default: *None* (no negative header match filter applied)

Example:

```xml
<Match pattern="^.*$">
    <FilterIfNotHeaderMatch name="X-Header-To-Not-Match" pattern="^curl\/.+$" />
</Match>
```

### Match > FilterIfHeaderExist
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which have a certain header.

Note: header names are case insensitive.

Default: *None* (no header existence requirement)

Example:

```xml
<Match pattern="^.*$">
    <FilterIfHeaderExist name="X-Header-To-Accept" />
</Match>
```

### Match > FilterIfNotHeaderExist
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which do not have a certain header.

Note: header names are case insensitive.

Default: *None* (no header absence requirement)

Example:

```xml
<Match pattern="^.*$">
    <FilterIfNotHeaderExist name="X-Header-To-Reject" />
</Match>
```

### Match > Header
NOTE: Only valid within a Match block.

Allows HTTP response headers to be set, only valid within a Match.

Default: *None* (no additional custom response headers injected)

Example:

```xml
<Match pattern="^.*$">
    <Header name="Cache-Control"> public, must-revalidate, max-age=300 </Header>
</Match>
```

### Match > ShowDirectoryIndexes
NOTE: Only valid within a Match block.

Enables or disables directory index listings, only valid within a Match (either "on" or "off").

Only the first ShowDirectoryIndexes node will be recognized.

Default: `on`

Example:

```xml
<Match pattern="^.*$">
    <ShowDirectoryIndexes> on </ShowDirectoryIndexes>
</Match>
```

### Match > Access
NOTE: Only valid within a Match block.

Controls access to content, only valid within a Match.

The "mode" attribute controls the default policy, either to deny all except for hosts in the Allow nodes or to allow all except for hosts in the Deny nodes.

Note that if the mode is set to "deny all" then any Deny nodes are skipped, and vice versa for "allow all" and Allow nodes.

Valid modes: `"deny all" | "allow all"`

Default: `allow all` (if omitted, unrestricted access is permitted to all clients)

Example:

```xml
<Match pattern="^.*$">
    <Header name="X-Dummy-Header"> 1 </Header>
    <Access mode="deny all">
        <Allow> 127.0.0.1 </Allow>
        <Allow> ::1 </Allow>
    </Access>
</Match>
```

### Redirect
Controls temporary or permanent redirects for specific files or paths via Regex matching.

You can use capture groups like $1 and $2 to reference specific capture groups in the pattern (or $0 for the whole matched string).

Note A: the "to" attribute is NOT a regular expression, but does support referencing capture groups from the pattern.

Note B: the pattern attribute matches the decoded, normalized URI and does NOT capture query strings. Query strings are preserved in their original encoded form, so `%26` stays `%26` rather than becoming a new parameter separator.

The example below permanently redirects any HTML files in any directory under /foo/bar to the corresponding HTML file and directory in /baz/qux (see commented-out example below).

Default: *None* (no HTTP redirects configured)

Example:

```xml
<Redirect pattern="/foo/bar/(.*?)/(.*?).html" to="/baz/qux/$1/$2.html"> 308 </Redirect>
```

### Rewrite
Controls internal rewrites for URIs for specific files or paths via Regex matching.

You can use capture groups like $1 and $2 to reference specific capture groups in the pattern (or $0 for the whole matched string).

Note A: the "to" attribute is NOT a regular expression, but does support referencing capture groups from the pattern.

Note B: the pattern attribute matches the decoded, normalized URI and does NOT capture query strings. Query strings are preserved in their original encoded form, so `%26` stays `%26` rather than becoming a new parameter separator. Rewrites do not percent-decode the replacement path again.

The example below rewrites traffic to any HTML files in any directory under /foo/bar to the corresponding HTML file and directory in /baz/qux (see commented-out example below).

Default: *None* (no internal URI rewrites configured)

Example:

```xml
<Rewrite pattern="/foo/bar/(.*?)/(.*?).html" to="/baz/qux/$1/$2.html" />
```

### KeepAlive
Controls whether connections may remain open for later requests.

When enabled, HTTP/1.1 connections are persistent by default; HTTP/1.0 requests must explicitly include the `keep-alive` Connection option. Option names are case-insensitive, and `close` takes precedence over `keep-alive`, including across repeated Connection headers. HTTP/0.9 connections close after the response.

When disabled, connections close after each response. KeepAliveMaxTimeout and KeepAliveMaxRequests still limit persistence when enabled.

Default: `on`

Example:

```xml
<KeepAlive> on </KeepAlive>
```

### KeepAliveMaxTimeout
Specifies how long a keep-alive connection will wait for subsequent requests in SECONDS, if keep-alive connections are enabled.

Default: `3`

Example:

```xml
<KeepAliveMaxTimeout> 3 </KeepAliveMaxTimeout>
```

### KeepAliveMaxRequests
Specifies how many requests are allowed per keep-alive connection, if keep-alive connections are enabled.

Default: `100`

Example:

```xml
<KeepAliveMaxRequests> 100 </KeepAliveMaxRequests>
```

### EnableCustomStatusDocs
When enabled, overrides default HTTP status & error documents with user-defined ones.
Custom status documents must be immediate children of the document root and must have the ".html" extension.

Each status document should be named with the 3-digit HTTP status code, such as `/500.html`, `/404.html`, and so forth.

Default: `on`

Example:

```xml
<EnableCustomStatusDocs> on </EnableCustomStatusDocs>
```

### MaxRequestLineLength
Specifies how long the request line is allowed to be, in bytes.

Includes the method, protocol version, and URI.

Default: `4096`

Example:

```xml
<MaxRequestLineLength> 4096 </MaxRequestLineLength>
```

### MaxRequestBacklog
Specifies how many queued requests the server can handle simultaneously (valid range: 1–65535).

Default: `100`

Example:

```xml
<MaxRequestBacklog> 100 </MaxRequestBacklog>
```

### RequestBufferSize
Specifies how large the read buffer is for requests, in bytes.

This does not change the fixed 64 KiB limit for the request line and header section, including its terminating blank line. Exceeding that limit returns 431 Request Header Fields Too Large; MaxRequestLineLength separately limits the request line.

Default: `16384`

Example:

```xml
<RequestBufferSize> 16384 </RequestBufferSize>
```

### ResponseBufferSize
Specifies how large the write buffer is for responses, in bytes.

Default: `16384`

Example:

```xml
<ResponseBufferSize> 16384 </ResponseBufferSize>
```

### MaxRequestBody
Specifies how large an incoming request's body is allowed to be, in bytes.

Request bodies use Content-Length framing. Invalid or overflowing lengths, duplicate Content-Length headers, and unsupported request Transfer-Encoding are rejected with 400 Bad Request. Chunked request bodies are not supported; this does not affect chunked responses.

If you experience 413 Content Too Large statuses being returned, you can increase this value but beware that increasing this value too large may slow down your machine and/or cause issues.

Default: `268435456`

Example:

```xml
<MaxRequestBody> 268435456 </MaxRequestBody>
```

### MaxResponseBody
Specifies how large an outgoing response's body is allowed to be, in bytes.

If you experience 413 Content Too Large statuses being returned, you can increase this value but beware that increasing this value too large may slow down your machine and/or cause issues.

Default: `268435456`

Example:

```xml
<MaxResponseBody> 268435456 </MaxResponseBody>
```

### MinResponseCompressionSize
How small a response body must be to bypass using any compression methods (in bytes).

This avoids the overhead and bloat of using compression algorithms on really small bodies, however values that are too large (ie. > 1000 bytes) will increase network overhead.

Default: `750`

Example:

```xml
<MinResponseCompressionSize> 750 </MinResponseCompressionSize>
```

### IdleThreadsPerChild
Specifies how many connection threads exist for each server thread.

Note that under heavy load, up to MaxThreadsPerChild threads may be created per server thread--this value is purely the default "idle" amount of threads available.

There are four server threads: IPv4, IPv4 w/ TLS, IPv6, and IPv6 w/ TLS.

Default: `12`

Example:

```xml
<IdleThreadsPerChild> 12 </IdleThreadsPerChild>
```

### MaxThreadsPerChild
Specifies the maximum number of connection threads that exist for each server thread under load. Must be greater than `IdleThreadsPerChild`.

Note that under normal circumstances, only the amount of IdleThreadsPerChild threads will be used--up to MaxThreadsPerChild threads will be utilized to decrease the overall size of the connection backlog on each server thread to improve client performance.

There are four server threads: IPv4, IPv4 w/ TLS, IPv6, and IPv6 w/ TLS.

Default: `60`

Example:

```xml
<MaxThreadsPerChild> 60 </MaxThreadsPerChild>
```

### ShowWelcomeBanner
Whether or not to print the welcome banner on startup (true/false).

Default: `true`

Example:

```xml
<ShowWelcomeBanner> true </ShowWelcomeBanner>
```

### ShowDonationBanner
Whether or not to print the optional donation banner on startup (true/false).

Default: `true`

Example:

```xml
<ShowDonationBanner> true </ShowDonationBanner>
```

### ShowPrivilegeDeescalationMessage
Whether or not to print the privilege deescalation notice to the console upon dropping root privileges after binding privileged ports (true/false).

Default: `true`

Example:

```xml
<ShowPrivilegeDeescalationMessage> true </ShowPrivilegeDeescalationMessage>
```

### StartupCheckLatestRelease
Whether or not to check for a new version on startup (true/false).

Disabling this often will improve startup speed, but it is recommended to leave this on unless you know what you're doing.

Default: `true`

Example:

```xml
<StartupCheckLatestRelease> true </StartupCheckLatestRelease>
```

