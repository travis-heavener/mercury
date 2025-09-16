#include "handler_0_9.hpp"

#include "../../conf/conf.hpp"
#include "../../util/toolbox.hpp"
#include "../../logs/logger.hpp"

namespace http::version::handler_0_9 {

    Response* genResponse(Request& request) {
        // Create Response object
        Response* pResponse = new Response("HTTP/0.9");

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
        File file(request.getPathStr());
        if (!request.isFileValid(*pResponse, file))
            return pResponse;

        // Switch on method
        switch (request.getMethod()) {
            case METHOD::GET: {
                // Attempt to buffer resource
                if (pResponse->loadBodyFromFile(file) == IO_FAILURE) {
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