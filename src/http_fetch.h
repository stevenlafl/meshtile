#pragma once
#include <string>

namespace meshtile {

bool is_url(const std::string& s);
std::string fetch_url(const std::string& url);
std::string read_file(const std::string& path);

// Fetch from URL or read from file, depending on the source string.
std::string fetch_or_read(const std::string& source);

} // namespace meshtile
