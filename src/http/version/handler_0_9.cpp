#include "handler_0_9.hpp"

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

        // Verify path is restricted to document root
        if (!request.isInDocumentRoot(*pResponse, "GET"))
            return pResponse;

        // Lookup file & validate it doesn't have anything wrong with it
        File file(request.getPathStr());
        if (!request.isFileValid(*pResponse, file))
            return pResponse;

        // Check for PHP files
        if (conf::USE_PHP_FPM && file.path.ends_with(".php")) {
            fcgi::handlePHPRequest(file, request, *pResponse);
            return pResponse;
        }

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