#ifndef __HTTP_VERSION_HANDLER_0_9_HPP
#define __HTTP_VERSION_HANDLER_0_9_HPP

#include "../request.hpp"
#include "../response.hpp"

#include "../cgi/client.hpp"

namespace http::version::handler_0_9 {
    Response* genResponse(Request&);
}

#endif