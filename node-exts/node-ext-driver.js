/**
 * The Node Extension Driver for Mercury.
 * 
 * Facilitates data transmission to and from the Mercury flie server via C-sockets.
 */

// Returns a description string for an HTTP status code
const lookupStatusDesc = (status) => {
    // From https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";
        case 226: return "IM Used";

        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";

        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 418: return "I'm a teapot";
        case 421: return "Misdirected Request";
        case 422: return "Unprocessable Content";
        case 423: return "Locked";
        case 424: return "Failed Dependency";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 451: return "Unavailable For Legal Reasons";

        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 506: return "Variant Also Negotiates";
        case 507: return "Insufficient Storage";
        case 508: return "Loop Detected";
        case 510: return "Not Extended";
        case 511: return "Network Authentication Required";

        default: return "Unknown";
    }
};

// Node Extension Response object
class NodeExtensionResponse {
    #status; // Status code
    #headers; // JSON object of response headers
    #body; // Response payload string

    #endCallback; // Internal callback to end request and send to socket

    constructor(endCallback) {
        this.#endCallback = endCallback;

        // Default values
        this.#status = 200; // OK
        this.#body = ""; // Empty body
        this.#headers = {}; // No headers (extras such as Server are filled in by Mercury socket handler)
    }

    // Setters
    status(s) {
        this.#status = parseInt(s);
    }

    setHeader(name, value) {
        // Normalize all headers to uppercase
        this.#headers[name.toUpperCase()] = value;
    }

    // Send methods
    send(text) {
        this.#body = text;
        this.#endCallback( this.#stringify() );
    }

    // Shorthand for JSON payloads
    json(json) {
        this.setHeader("Content-Type", "application/json");
        this.send(JSON.stringify(json));
    }

    // Used to abort the response and return an empty one
    __abort() {
        this.#endCallback( "" );
    }

    // Used to stringify into an HTTP response
    #stringify() {
        // Write status line
        const statusLine = `${this.#status} ${lookupStatusDesc(this.#status)} HTTP/1.1`;

        // Compose headers
        const headers = Object.entries(this.#headers)
            .map(pair => pair[0] + ": " + pair[1])
            .join("\n");

        // Return full response
        return statusLine + "\n" + headers + "\n\n" + this.#body;
    }
};

// Node Extension Request object
class NodeExtensionRequest {
    #verb; // Request HTTP verb
    #path; // Request resource path
    #headers; // JSON object of request headers
    #body; // Body payload as a JSON object
    #timestamp; // Timestamp received

    // Parse raw socket data
    constructor(raw) {
        // TODO
        this.#timestamp = Date.now();
    }

    // Getters
    get statusLine() { return this.#verb + " " + this.#path; }
    get verb() { return this.#verb; }
    get path() { return this.#path; }
    get headers() { return this.#headers; }
    get body() { return this.#body; }
    get timestamp() { return this.#timestamp; }
};

// Node Extension wrapper object
class NodeExtension {
    #host; // Mercury connection host (usually 127.0.0.1)
    #port; // Node Extension socket port on Mercury (usually port 9220)
    #sock; // Reference to Mercury socket
    #endpoints; // Object of verb + path keys and callback function values

    constructor(host, port) {
        this.#host = host;
        this.#port = port;
        this.#endpoints = {};

        // Establish connection with Mercury socket
        // TODO

        // Create request from raw socket data
        // const endCallback = () => {...};
        // this.#onRequestReceived( new NodeExtensionRequest(raw), new NodeExtensionResponse(endCallback) );
    }

    // Getters/setters
    get host() { return this.#host; }
    get port() { return this.#port; }

    // Used to bind a listener on the socket
    bind(verb, path, callback) {
        this.#endpoints[verb + " " + path] = callback;
    }

    // Bind shorthands
    get(path, callback)    { this.bind("GET", path, callback); }
    post(path, callback)   { this.bind("POST", path, callback); }
    put(path, callback)    { this.bind("PUT", path, callback); }
    delete(path, callback) { this.bind("DELETE", path, callback); }

    // Handles the incoming request
    #onRequestReceived(req, res) {
        // Lookup path & verb status line
        const callback = this.#endpoints[ req.statusLine ];

        // Handle request
        if (callback) // Handle callback
            callback(req, res);
        else // Return empty response
            res.__abort();
    }
};

// Establish connection with Mercury socket
export const connect = (host, port) => {
    return new NodeExtension(host, port);
};