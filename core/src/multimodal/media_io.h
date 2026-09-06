#pragma once

#include "bmoe/session.h"

#include <cstdint>
#include <string>

namespace meitte {
MediaKind media_kind_from_name(const std::string & name);
bool load_media_file(const std::string & path, uint64_t max_bytes, MediaInput & out, std::string & error);
} // namespace meitte
