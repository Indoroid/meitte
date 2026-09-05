#pragma once

#include "bmoe/session.h"

#include <string>

namespace meitte {
bool load_media_file(const std::string & path, MediaInput & out, std::string & error);
} // namespace meitte
