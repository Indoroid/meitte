#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace meitte {
bool decode_audio_ffmpeg(const std::vector<uint8_t> & input,
                         const std::string & bin_dir,
                         int sample_rate,
                         size_t max_bytes,
                         const std::function<bool()> & cancelled,
                         std::vector<float> & output,
                         std::string & error);
}
