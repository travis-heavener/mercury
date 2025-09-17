#ifndef __HTTP_VERSION_HANDLER_1_0_HPP
#define __HTTP_VERSION_HANDLER_1_0_HPP

#include <memory>

#include "../request.hpp"
#include "../response.hpp"

namespace http::version::handler_1_0 {
    std::unique_ptr<Response> genResponse(Request&);
}

#endif