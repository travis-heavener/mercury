#ifndef __CONF_HPP
#define __CONF_HPP

#include <filesystem>

#define VERSION "Mercury v0.1.4"

static std::filesystem::path DOCUMENT_ROOT = std::filesystem::current_path() / "public";

#endif