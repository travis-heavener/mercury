# Mercury Config

The Mercury HTTP server provides a highly customizable configuration interface in `conf/mercury.conf`.

All configuration nodes must be wrapped within a singular `<Mercury>` node.

## DocumentRoot
Specifies where files are served from, relative to the Mercury directory.

Default: `./public/`

Example:

```xml
<DocumentRoot> ./public/ </DocumentRoot>
```

## BindAddressIPv4
Controls the socket bind address for IPv4, or "off" if disabled.

Default: `0.0.0.0`

Example:

```xml
<BindAddressIPv4> 0.0.0.0 </BindAddressIPv4>
```

## BindAddressIPv6
Controls the socket bind address for IPv6, or "off" if disabled.

Default: `::`

Example:

```xml
<BindAddressIPv6> :: </BindAddressIPv6>
```

## Port
Specifies what port will be used to serve cleartext (non-HTTPS) content

Default: `80`

Example:

```xml
<Port> 80 </Port>
```

## TLSPort
Specifies which port to serve SSL/TLS traffic over, or "off" if disabled.

See `conf/ssl/cert.pem` and `conf/ssl/key.pem`.

Default: `off`

Example:

```xml
<TLSPort> off </TLSPort>
```

## EnableLegacyHTTPVersions
Enables or disables legacy HTTP versions (HTTP/0.9, HTTP/1.0)

Default: `on`

Example:

```xml
<EnableLegacyHTTPVersions> on </EnableLegacyHTTPVersions>
```

## IndexFiles
Specifies what name for index files will be served when accessing a directory by name.

The order (left to right) specifies which files will be searched for first, or empty if desired.

Default: `index.html, index.htm, index.php`

Example:

```xml
<IndexFiles> index.html, index.htm, index.php </IndexFiles>
```

## AccessLogFile
Specifies where server access logs are stored.

Default: `./logs/access.log`

Example:

```xml
<AccessLogFile> ./logs/access.log </AccessLogFile>
```

## ErrorLogFile
Specifies where server error logs are stored.

Default: `./logs/access.log`

Example:

```xml
<ErrorLogFile> ./logs/error.log </ErrorLogFile>
```

## RedactLogIPs
Controls whether IP addresses in log files are anonymized or not.

Default: `false`

Example:

```xml
<RedactLogIPs> false </RedactLogIPs>
```

## EnablePHPCGI
Whether or not to pass through requests for PHP files to a PHP-CGI process.

If disabled, the value of the WinPHPCGIPath node is ignored.

Default: `off`

Example:

```xml
<EnablePHPCGI> off </EnablePHPCGI>
```

## WinPHPCGIPath
Points to the php-cgi.exe executable to handle PHP requests on.

NOTE: This node applies to Windows only.

Default: `./php/php-cgi.exe`

Example:

```xml
<WinPHPCGIPath> ./php/php-cgi.exe </WinPHPCGIPath>
```

## Match
Match allows individual file/directory control over resource access via regex matches for its `pattern` attribute.

Note that the pattern matches the URI w/o the query string from the HTTP request, NOT an absolute path.

Default: `on`

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

## Match > FilterIfHeaderMatch
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which have a header that matches the following pattern (Regex match).

Note: header names are case insensitive.

Example:

```xml
<Match pattern="^.*$">
    <FilterIfHeaderMatch name="X-Header-To-Match" pattern="text\/plain; charset\=utf\-8" />
</Match>
```

## Match > FilterIfNotHeaderMatch
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which have a header that does not match the following pattern (Regex match).

Note: header names are case insensitive.

Example:

```xml
<Match pattern="^.*$">
    <FilterIfNotHeaderMatch name="X-Header-To-Not-Match" pattern="^curl\/.+$" />
</Match>
```

## Match > FilterIfHeaderExist
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which have a certain header.

Note: header names are case insensitive.

Example:

```xml
<Match pattern="^.*$">
    <FilterIfHeaderExist name="X-Header-To-Accept" />
</Match>
```

## Match > FilterIfNotHeaderExist
NOTE: Only valid within a Match block.

Modifies the encasing Match block to restrict access to only Requests which do not have a certain header.

Note: header names are case insensitive.

Example:

```xml
<Match pattern="^.*$">
    <FilterIfNotHeaderExist name="X-Header-To-Reject" />
</Match>
```

## Match > Header
NOTE: Only valid within a Match block.

Allows HTTP response headers to be set, only valid within a Match.

Example:

```xml
<Match pattern="^.*$">
    <Header name="Cache-Control"> public, must-revalidate, max-age=300 </Header>
</Match>
```

## Match > ShowDirectoryIndexes
NOTE: Only valid within a Match block.

Enables or disables directory index listings, only valid within a Match (either "on" or "off").

Only the first ShowDirectoryIndexes node will be recognized.

Example:

```xml
<Match pattern="^.*$">
    <ShowDirectoryIndexes> on </ShowDirectoryIndexes>
</Match>
```

## Match > Access
NOTE: Only valid within a Match block.

Controls access to content, only valid within a Match.

The "mode" attribute controls the default policy, either to deny all except for hosts in the Allow nodes or to allow all except for hosts in the Deny nodes.

Note that if the mode is set to "deny all" then any Deny nodes are skipped, and vice versa for "allow all" and Allow nodes.

Valid modes: `"deny all" | "allow all"`

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

## Redirect
Controls temporary or permanent redirects for specific files or paths via Regex matching.

You can use capture groups like $1 and $2 to reference specific capture groups in the pattern (or $0 for the whole matched string).

Note A: the "to" attribute is NOT a regular expression, but does support referencing capture groups from the pattern.

Note B: the pattern attribute does NOT capture query strings. Query strings are preserved from the original request.

The example below permanently redirects any HTML files in any directory under /foo/bar to the corresponding HTML file and directory in /baz/qux (see commented-out example below).

Example:

```xml
<Redirect pattern="/foo/bar/(.*?)/(.*?).html" to="/baz/qux/$1/$2.html"> 308 </Redirect>
```

## Rewrite
Controls internal rewrites for URIs for specific files or paths via Regex matching.

You can use capture groups like $1 and $2 to reference specific capture groups in the pattern (or $0 for the whole matched string).

Note A: the "to" attribute is NOT a regular expression, but does support referencing capture groups from the pattern.

Note B: the pattern attribute does NOT capture query strings. Query strings are preserved from the original request.

The example below rewrites traffic to any HTML files in any directory under /foo/bar to the corresponding HTML file and directory in /baz/qux (see commented-out example below).

Example:

```xml
<Rewrite pattern="/foo/bar/(.*?)/(.*?).html" to="/baz/qux/$1/$2.html" />
```

## KeepAlive
Controls whether or not to honor keep-alive headers in requests to maintain a connection that later requests can come through.

Default: `on`

Example:

```xml
<KeepAlive> on </KeepAlive>
```

## KeepAliveMaxTimeout
Specifies how long a keep-alive connection will wait for subsequent requests in SECONDS, if keep-alive connections are enabled.

Default: `3`

Example:

```xml
<KeepAliveMaxTimeout> 3 </KeepAliveMaxTimeout>
```

## KeepAliveMaxTimeout
Specifies how many requests are allowed per keep-alive connection, if keep-alive connections are enabled.

Default: `100`

Example:

```xml
<KeepAliveMaxRequests> 100 </KeepAliveMaxRequests>
```

## MaxRequestLineLength
Specifies how long the request line is allowed to be, in bytes.

Includes the method, protocol version, and URI.

Default: `4096`

Example:

```xml
<MaxRequestLineLength> 4096 </MaxRequestLineLength>
```

## MaxRequestBacklog
Specifies how many queued requests the server can handle simultaneously.

Default: `100`

Example:

```xml
<MaxRequestBacklog> 100 </MaxRequestBacklog>
```

## RequestBufferSize
Specifies how large the read buffer is for requests, in bytes.

Default: `16384`

Example:

```xml
<RequestBufferSize> 16384 </RequestBufferSize>
```

## ResponseBufferSize
Specifies how large the write buffer is for responses, in bytes.

Default: `16384`

Example:

```xml
<ResponseBufferSize> 16384 </ResponseBufferSize>
```

## MaxRequestBody
Specifies how large an incoming request's body is allowed to be, in bytes.

If you experience 413 Content Too Large statuses being returned, you can increase this value but beware that increasing this value too large may slow down your machine and/or cause issues.

Default: `268435456`

Example:

```xml
<MaxRequestBody> 268435456 </MaxRequestBody>
```

## MaxResponseBody
Specifies how large an outgoing response's body is allowed to be, in bytes.

If you experience 413 Content Too Large statuses being returned, you can increase this value but beware that increasing this value too large may slow down your machine and/or cause issues.

Default: `268435456`

Example:

```xml
<MaxResponseBody> 268435456 </MaxResponseBody>
```

## MinResponseCompressionSize
How small a response body must be to bypass using any compression methods (in bytes).

This avoids the overhead and bloat of using compression algorithms on really small bodies, however values that are too large (ie. > 1000 bytes) will increase network overhead.

Default: `750`

Example:

```xml
<MinResponseCompressionSize> 750 </MinResponseCompressionSize>
```

## IdleThreadsPerChild
Specifies how many connection threads exist for each server thread.

Note that under heavy load, up to MaxBurstThreadsPerChild threads may be created per server thread--this value is purely the default "idle" amount of threads available.

There are four server threads: IPv4, IPv4 w/ TLS, IPv6, and IPv6 w/ TLS.

Default: `12`

Example:

```xml
<IdleThreadsPerChild> 12 </IdleThreadsPerChild>
```

## MaxThreadsPerChild
Specifies the maximum number of connection threads exist for each server thread under load.

Note that under normal circumstances, only the amount of IdleThreadsPerChild threads will be used--up to MaxBurstThreadsPerChild threads will be utilized to decrease the overall size of the connection backlog on each server thread to improve client performance.

There are four server threads: IPv4, IPv4 w/ TLS, IPv6, and IPv6 w/ TLS.

Default: `60`

Example:

```xml
<MaxThreadsPerChild> 60 </MaxThreadsPerChild>
```

## ShowWelcomeBanner
Whether or not to print the welcome banner on startup (true/false).

Default: `true`

Example:

```xml
<ShowWelcomeBanner> true </ShowWelcomeBanner>
```

## StartupCheckLatestRelease
Whether or not to check for a new version on startup (true/false).

Disabling this often will improve startup speed, but it is recommended to leave this on unless you know what you're doing.

Default: `true`

Example:

```xml
<StartupCheckLatestRelease> true </StartupCheckLatestRelease>
```