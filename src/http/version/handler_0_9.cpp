#include "handler_0_9.hpp"

#include "../../conf/conf.hpp"
#include "../../util/toolbox.hpp"
#include "../../logs/logger.hpp"

namespace http::version::handler_0_9 {

    std::unique_ptr<Response> genResponse(Request& request) {
        // Create Response object
        std::unique_ptr<Response> pResponse = std::unique_ptr<Response>(new Response("HTTP/0.9"));

        // Check if method is valid
        const METHOD method = request.getMethod();
        if (method != METHOD::GET) {
            pResponse->loadBodyFromErrorDoc(501); // Not Implemented
            return pResponse;
        }

        // Verify no 400 errors have been met
        if ( request.has400Error() ) {
            pResponse->loadBodyFromErrorDoc(400);
            return pResponse;
        }

        // Verify access is permitted
        if (!conf::matchConfigs.empty()) {
            // Create sanitized IP address
            const std::string decodedURI = request.getDecodedURI();
            const headers_map_t& headers = request.getHeaders();
            try {
                conf::SanitizedIP sip( conf::parseSanitizedClientIP(request.getIPStr()) );

                // Check Match patterns
                for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs) {
                    if (pMatch->doesRequestMatch(decodedURI, headers)) {
                        // Verify access is permitted
                        if (!pMatch->getAccessControl()->isIPAccepted(sip)) {
                            pResponse->loadBodyFromErrorDoc(403);
                            return pResponse;
                        }
                    }
                }
            } catch (std::invalid_argument&) {
                ERROR_LOG << "Invalid IP address while parsing sanitized IP" << std::endl;
                pResponse->loadBodyFromErrorDoc(500);
                return pResponse;
            }
        }

        // Verify path is restricted to document root
        if (!request.isInDocumentRoot(*pResponse, "GET"))
            return pResponse;

        // Lookup file & validate it doesn't have anything wrong with it
        File file(request.getPaths());
        if (!request.isFileValid(*pResponse, file))
            return pResponse;

        // Switch on method
        switch (request.getMethod()) {
            case METHOD::GET: {
                // Attempt to buffer resource
                if (pResponse->loadBodyFromFile(file) == IO_FAILURE) {
                    ERROR_LOG << "HTTP/0.9 loadBodyFromFile IO failure" << std::endl;
                    pResponse->loadBodyFromErrorDoc(500);
                    break;
                }
                break;
            }
            default: {
                pResponse->loadBodyFromErrorDoc(400);
                break;
            }
        }

        // Return Response ptr
        return pResponse;
    }

}

#undef ALLOWED_METHODS