#ifndef __HTTP_VERSION_HANDLER_1_1_HPP
#define __HTTP_VERSION_HANDLER_1_1_HPP

#include <memory>

#include "../message/request.hpp"
#include "../message/response.hpp"

namespace http::version::handler_1_1 {
    std::unique_ptr<Response> genResponse(Request&);
}

#endif