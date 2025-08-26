#ifndef __HTTP_VERSION_HANDLER_1_1_HPP
#define __HTTP_VERSION_HANDLER_1_1_HPP

#include "../request.hpp"
#include "../response.hpp"

#include "../cgi/client.hpp"

namespace http::version::handler_1_1 {
    Response* genResponse(Request&);
}

#endif