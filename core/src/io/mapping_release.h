#pragma once

// Release a model file's memory mapping after every observed model weight has moved to an
// engine-owned buffer. On Windows, a live file section serializes concurrent unbuffered reads.
// The model still believes that it owns the mapping, so this module replaces released views and
// handle slots with inert placeholders. The caller must keep them until after model destruction.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace meitte::pio {

struct MappingReleaseReport {
    bool supported = false;
    int views_unmapped = 0;
    int sections_closed = 0;
    uint64_t bytes = 0;
    int plugs_missed = 0;
    std::string error;
};

class MappingPlaceholders {
public:
    MappingPlaceholders() = default;
    ~MappingPlaceholders();
    MappingPlaceholders(MappingPlaceholders &&) noexcept;
    MappingPlaceholders & operator=(MappingPlaceholders &&) noexcept;
    MappingPlaceholders(const MappingPlaceholders &) = delete;
    MappingPlaceholders & operator=(const MappingPlaceholders &) = delete;

    void release();
    bool empty() const { return reserved_.empty() && plugs_.empty(); }

private:
    friend MappingReleaseReport release_file_mappings(const std::vector<std::string> &, MappingPlaceholders *);
    std::vector<std::pair<void *, size_t>> reserved_;
    std::vector<void *> plugs_;
};

MappingReleaseReport release_file_mappings(const std::vector<std::string> & paths, MappingPlaceholders * out);
size_t addresses_in_file_mappings(const std::vector<std::string> & paths, const std::vector<const void *> & addresses);

} // namespace meitte::pio
