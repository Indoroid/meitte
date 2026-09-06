#include "media_io.h"

#include <fstream>
#include <algorithm>
#include <cctype>

namespace meitte {

MediaKind media_kind_from_name(const std::string & name) {
    const size_t dot = name.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (extension == ".mp4" || extension == ".mkv" || extension == ".webm" || extension == ".mov" ||
        extension == ".avi")
        return MediaKind::Video;
    if (extension == ".wav" || extension == ".mp3" || extension == ".flac" || extension == ".ogg" ||
        extension == ".m4a" || extension == ".aac" || extension == ".opus")
        return MediaKind::Audio;
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp" ||
        extension == ".bmp")
        return MediaKind::Image;
    return MediaKind::Auto;
}

bool load_media_file(const std::string & path, uint64_t max_bytes, MediaInput & out, std::string & error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        error = "cannot open media file: " + path;
        return false;
    }
    const std::streamoff end = f.tellg();
    if (end <= 0 || !max_bytes || static_cast<uint64_t>(end) > max_bytes) {
        error = "media file is empty or exceeds the configured byte limit: " + path;
        return false;
    }
    out.bytes.resize(static_cast<size_t>(end));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char *>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()))) {
        error = "failed to read media file: " + path;
        return false;
    }
    out.name = path;
    out.kind = media_kind_from_name(path);
    return true;
}

} // namespace meitte
