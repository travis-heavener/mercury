#include "handler_1_0.hpp"

namespace http {
    namespace version {
        namespace handler_0_9 {

            Response* genResponse(Request& request) {
                // Create Response object
                Response* pResponse = new Response("HTTP/0.9");

                // Verify path is restricted to document root
                if (!request.isInDocumentRoot(*pResponse, ""))
                    return pResponse;

                // Lookup file & validate it doesn't have anything wrong with it
                File file(request.getPathStr());
                if (!request.isFileValid(*pResponse, file))
                    return pResponse;

                // Switch on method
                switch (request.getMethod()) {
                    case METHOD::GET: {
                        // Check accepted MIMES
                        if (!request.isMIMEAccepted(file.MIME)) {
                            pResponse->loadBodyFromErrorDoc(406);
                            break;
                        }

                        // Attempt to buffer resource
                        if (pResponse->loadBodyFromFile(file) == IO_FAILURE) {
                            pResponse->loadBodyFromErrorDoc(500);
                            break;
                        }
                        break;
                    }
                    default: {
                        pResponse->loadBodyFromErrorDoc(405);
                        break;
                    }
                }

                // Return Response ptr
                return pResponse;
            }

        }
    }
}

#undef ALLOWED_METHODS