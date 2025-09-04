#ifndef __HTTP_VERSION_HANDLER_1_0_HPP
#define __HTTP_VERSION_HANDLER_1_0_HPP

#include "../request.hpp"
#include "../response.hpp"

#include "../cgi/process.hpp"

namespace http::version::handler_1_0 {
    Response* genResponse(Request&);
}

#endif