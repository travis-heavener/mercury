#include "handler_1_0.hpp"

#define ALLOWED_STATIC_METHODS "GET, HEAD"

namespace http {
    namespace version {
        namespace handler_1_0 {

            void setStatusMaybeErrorDoc(Request& req, Response& res, const int status) {
                res.setStatus(status);
                if (req.isMIMEAccepted("text/html"))
                    res.loadBodyFromErrorDoc(status);
            }

            Response* genResponse(Request& request) {
                // Create Response object
                Response* pResponse = new Response("HTTP/1.0");

                // Check if method is valid
                const METHOD method = request.getMethod();
                if (method != METHOD::GET && method != METHOD::HEAD && method != METHOD::POST) {
                    setStatusMaybeErrorDoc(request, *pResponse, 501); // Not Implemented
                    return pResponse;
                }

                // Verify the body (which is ignored in the Request object) wasn't too large
                if (request.isContentTooLarge()) {
                    setStatusMaybeErrorDoc(request, *pResponse, 413);
                    return pResponse;
                }

                // Verify path is restricted to document root
                if (!request.isInDocumentRoot(*pResponse, ALLOWED_STATIC_METHODS))
                    return pResponse;

                // Lookup file & validate it doesn't have anything wrong with it
                File file(request.getPathStr());
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
                        for (conf::Match* pMatch : conf::matchConfigs)
                            if (std::regex_match(file.path, pMatch->getPattern()))
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
    }
}

#undef ALLOWED_STATIC_METHODS