#include "handler_1_1.hpp"

#include "../cgi/process.hpp"
#include "../../conf/conf.hpp"
#include "../../util/toolbox.hpp"
#include "../../logs/logger.hpp"

#define ALLOWED_STATIC_METHODS "GET, HEAD, OPTIONS"
#define ALLOWED_METHODS "GET, HEAD, OPTIONS, POST, PUT, PATCH, DELETE"

namespace http {
    namespace version {
        namespace handler_1_1 {

            void setStatusMaybeErrorDoc(Request& req, Response& res, const int status) {
                res.setStatus(status);
                if (req.isMIMEAccepted("text/html"))
                    res.loadBodyFromErrorDoc(status);
            }

            Response* genResponse(Request& request) {
                // Create Response object
                Response* pResponse = new Response("HTTP/1.1");

                // Check if method is valid
                const METHOD method = request.getMethod();
                if (method != METHOD::GET && method != METHOD::HEAD && method != METHOD::OPTIONS
                    && method != METHOD::POST && method != METHOD::PUT && method != METHOD::DEL
                    && method != METHOD::PATCH) {
                    setStatusMaybeErrorDoc(request, *pResponse, 501); // Not Implemented
                    return pResponse;
                }

                // Verify no 400 errors have been met
                if ( request.has400Error() ) {
                    pResponse->loadBodyFromErrorDoc(400);
                    return pResponse;
                }

                // Verify access is permitted
                if (!conf::matchConfigs.empty()) {
                    // Format raw path & remove query string
                    std::string rawPath = request.getPathStr();
                    const size_t queryIndex = rawPath.find('/');
                    while (queryIndex != std::string::npos && rawPath.size() > queryIndex)
                        rawPath.pop_back();

                    // Create sanitized IP address
                    try {
                        conf::SanitizedIP sip( conf::parseSanitizedClientIP(request.getIPStr()) );

                        // Check Match patterns
                        for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs) {
                            if (std::regex_match(rawPath, pMatch->getPattern())) {
                                // Verify access is permitted
                                if (!pMatch->getAccessControl()->isIPAccepted(sip)) {
                                    setStatusMaybeErrorDoc(request, *pResponse, 403);
                                    return pResponse;
                                }
                            }
                        }
                    } catch (std::invalid_argument&) {
                        ERROR_LOG << "Invalid IP address while parsing sanitized IP (" << request.getIPStr() << ')' << std::endl;
                        setStatusMaybeErrorDoc(request, *pResponse, 500);
                        return pResponse;
                    }
                }

                // Verify the body (which is ignored in the Request object) wasn't too large
                if (request.isContentTooLarge()) {
                    setStatusMaybeErrorDoc(request, *pResponse, 413);
                    return pResponse;
                }

                File file(request.getPathStr());

                // Bypass document root checks, file checks, and PHP for OPTIONS * (server-wide edge case)
                if (method != METHOD::OPTIONS || request.getRawPathStr() != "*") {
                    // Verify path is restricted to document root
                    if (!request.isInDocumentRoot(*pResponse, ALLOWED_STATIC_METHODS))
                        return pResponse;

                    // Lookup file & validate it doesn't have anything wrong with it
                    if (!request.isFileValid(*pResponse, file))
                        return pResponse;

                    // Check for PHP files
                    if (conf::IS_PHP_ENABLED && file.path.ends_with(".php")) {
                        cgi::handlePHPRequest(file, request, *pResponse);

                        // Load additional headers
                        for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs)
                            if (std::regex_match(file.rawPath, pMatch->getPattern()))
                                for (auto [name, value] : pMatch->getHeaders())
                                    pResponse->setHeader(name, value);

                        return pResponse;
                    }
                }

                // Switch on method
                switch (request.getMethod()) {
                    case METHOD::HEAD:
                    case METHOD::GET: {
                        // Check accepted MIMES
                        if (!request.isMIMEAccepted(file.MIME)) {
                            setStatusMaybeErrorDoc(request, *pResponse, 406);
                            break;
                        }

                        // Check if previously cached
                        const auto pLastModTS = request.getHeader("IF-MODIFIED-SINCE");
                        if (pLastModTS != nullptr) {
                            // Compare timestamps
                            try {
                                // serverTime <= clientTime
                                if (getFileModTimeT(file.path) <= getTimeTFromGMT(*pLastModTS)) {
                                    pResponse->setStatus(304);
                                    break;
                                }
                            } catch (...) {
                                // Comparison failed, re-send content as if updated
                            }
                        }

                        // Attempt to buffer resource
                        if (pResponse->loadBodyFromFile(file) == IO_FAILURE) {
                            setStatusMaybeErrorDoc(request, *pResponse, 500);
                            break;
                        }

                        // Otherwise, body loaded successfully
                        pResponse->setStatus(200);

                        // Set MIME if there is content to return
                        if (pResponse->getContentLength() > 0)
                            pResponse->setHeader("Content-Type", file.MIME);

                        // Load additional headers for body loading
                        for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs)
                            if (std::regex_match(file.rawPath, pMatch->getPattern()))
                                for (auto [name, value] : pMatch->getHeaders())
                                    pResponse->setHeader(name, value);
                        break;
                    }
                    case METHOD::OPTIONS: { // OPTIONS introduced in HTTP/1.1
                        if (request.getRawPathStr() == "*") // Server-wide edge case
                            pResponse->setHeader("Allow", ALLOWED_METHODS);
                        else
                            pResponse->setHeader("Allow", ALLOWED_STATIC_METHODS);
                        pResponse->setStatus(204);
                        break;
                    }
                    default: { // Method is allowed but invalid for static file
                        // If the request allows HTML, return an HTML display
                        pResponse->setHeader("Allow", ALLOWED_STATIC_METHODS);
                        setStatusMaybeErrorDoc(request, *pResponse, 405);
                        break;
                    }
                }

                // Return Response ptr
                return pResponse;
            }

        }
    }
}

#undef ALLOWED_STATIC_METHODS
#undef ALLOWED_METHODS