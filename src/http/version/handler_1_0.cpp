#include "handler_1_0.hpp"

#include <optional>

#include "../../conf/conf.hpp"
#include "../../util/toolbox.hpp"
#include "../../logs/logger.hpp"

#define ALLOWED_STATIC_METHODS "GET, HEAD"

namespace http::version::handler_1_0 {

    void setStatusMaybeErrorDoc(Request& req, Response& res, const int status) {
        res.setStatus(status);
        if (req.isMIMEAccepted("text/html"))
            res.loadBodyFromErrorDoc(status);
    }

    std::unique_ptr<Response> genResponse(Request& request) {
        // Create Response object
        std::unique_ptr<Response> pResponse = std::unique_ptr<Response>(new Response("HTTP/1.0"));

        // Check if method is valid
        const METHOD method = request.getMethod();
        if (method != METHOD::GET && method != METHOD::HEAD && method != METHOD::POST) {
            setStatusMaybeErrorDoc(request, *pResponse, 501); // Not Implemented
            return pResponse;
        }

        // Verify no 400 errors have been met
        if ( request.has400Error() ) {
            setStatusMaybeErrorDoc(request, *pResponse, 400);
            return pResponse;
        }

        // Verify access is permitted
        if (!conf::matchConfigs.empty()) {
            // Format raw path & remove query string
            std::string rawPath = request.getPathStr();
            const size_t queryIndex = rawPath.find('?');
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
                ERROR_LOG << "Invalid IP address while parsing sanitized IP" << std::endl;
                setStatusMaybeErrorDoc(request, *pResponse, 500);
                return pResponse;
            }
        }

        // Verify the body (which is ignored in the Request object) wasn't too large
        if (request.isContentTooLarge()) {
            setStatusMaybeErrorDoc(request, *pResponse, 413);
            return pResponse;
        }

        // Handle redirects
        if (!conf::redirectRules.empty()) {
            // Format raw path & break off query string
            std::string rawPath = request.getPathStr();
            const size_t queryIndex = rawPath.find('?');
            const std::string queryString = queryIndex != std::string::npos ? rawPath.substr(queryIndex) : "";
            while (queryIndex != std::string::npos && rawPath.size() > queryIndex)
                rawPath.pop_back();

            // Check redirect rule patterns
            std::string locationBuf;
            for (const std::unique_ptr<conf::Redirect>& pRedirect : conf::redirectRules) {
                pRedirect->loadRedirectedPath(rawPath, locationBuf);
                if (locationBuf.empty()) continue;

                // New location found, set status (only 300-302 available for HTTP/1.0)
                const unsigned int status = pRedirect->getStatus();
                if (status > 302) ERROR_LOG << "HTTP/1.0 falling back from " << status << " status to 302 status" << std::endl;
                pResponse->setStatus( status > 302 ? 302 : status );
                pResponse->setHeader( "Location", locationBuf );
                return pResponse;
            }
        }

        // Verify path is restricted to document root
        if (!request.isInDocumentRoot(*pResponse, ALLOWED_STATIC_METHODS))
            return pResponse;

        // Lookup file & validate it doesn't have anything wrong with it
        File file(request.getRawPathStr());
        if (!request.isFileValid(*pResponse, file))
            return pResponse;

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
                if (pLastModTS.has_value()) { // Compare timestamps
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
                    ERROR_LOG << "HTTP/1.0 loadBodyFromFile IO failure" << std::endl;
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

#undef ALLOWED_STATIC_METHODS