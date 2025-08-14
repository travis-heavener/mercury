#ifndef __HTTP_VERSION_HANDLER_0_9_HPP
#define __HTTP_VERSION_HANDLER_0_9_HPP

#include "../request.hpp"
#include "../response.hpp"

namespace http {
    namespace version {
        namespace handler_0_9 {
            Response* genResponse(Request&);
        }
    }
}

#endif