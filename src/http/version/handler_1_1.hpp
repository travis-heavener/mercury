#ifndef __HTTP_VERSION_HANDLER_1_1_HPP
#define __HTTP_VERSION_HANDLER_1_1_HPP

#include <memory>

#include "../request.hpp"
#include "../response.hpp"

namespace http::version::handler_1_1 {
    std::unique_ptr<Response> genResponse(Request&);
}

#endif