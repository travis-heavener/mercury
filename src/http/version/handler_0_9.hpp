#ifndef __HTTP_VERSION_HANDLER_0_9_HPP
#define __HTTP_VERSION_HANDLER_0_9_HPP

#include <memory>

#include "../request.hpp"
#include "../response.hpp"

namespace http::version::handler_0_9 {
    std::unique_ptr<Response> genResponse(Request&);
}

#endif