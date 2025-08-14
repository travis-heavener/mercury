#ifndef __HTTP_VERSION_HANDLER_1_1_HPP
#define __HTTP_VERSION_HANDLER_1_1_HPP

#include "../request.hpp"
#include "../response.hpp"

namespace http {
    namespace version {
        namespace handler_1_1 {
            Response* genResponse(Request&);
        }
    }
}

#endif