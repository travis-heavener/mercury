#ifndef __HTTP_VERSION_HANDLER_1_0_HPP
#define __HTTP_VERSION_HANDLER_1_0_HPP

#include "../request.hpp"
#include "../response.hpp"

namespace http {
    namespace version {
        namespace handler_1_0 {
            Response* genResponse(Request&);
        }
    }
}

#endif