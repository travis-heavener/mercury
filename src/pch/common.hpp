#ifndef __COMMON_HPP
#define __COMMON_HPP

#ifdef _WIN32
    #include "common-win.hpp"
#else
    #include "common-linux.hpp"
#endif

#endif