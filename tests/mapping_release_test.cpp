#include "mapping_release.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

static int failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#if defined(_WIN32)

namespace {

std::string scratch_path() {
    char directory[MAX_PATH];
    const DWORD size = GetTempPathA(MAX_PATH, directory);
    if (size == 0 || size >= MAX_PATH) return {};
    char path[MAX_PATH];
    return GetTempFileNameA(directory, "mmr", 0, path) ? path : std::string{};
}

bool fill(const std::string & path, size_t bytes) {
    FILE * file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    std::vector<char> chunk(1 << 16, 'x');
    for (size_t done = 0; done < bytes; done += chunk.size())
        if (std::fwrite(chunk.data(), 1, chunk.size(), file) != chunk.size()) {
            std::fclose(file);
            return false;
        }
    std::fclose(file);
    return true;
}

struct ModelMapping {
    HANDLE section = nullptr;
    void * view = nullptr;

    bool open(const std::string & path) {
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        section = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        CloseHandle(file);
        if (!section) return false;
        view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
        return view != nullptr;
    }
};

DWORD state_at(void * address) {
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(address, &info, sizeof(info)) == sizeof(info) ? info.State : 0;
}

} // namespace

int main() {
    const size_t size = 8u << 20;
    const std::string path = scratch_path();
    CHECK(!path.empty());
    CHECK(fill(path, size));

    ModelMapping mapping;
    CHECK(mapping.open(path));
    CHECK(state_at(mapping.view) == MEM_COMMIT);
    CHECK(((const volatile char *) mapping.view)[0] == 'x');
    CHECK(meitte::pio::addresses_in_file_mappings({path}, {mapping.view}) == 1);

    meitte::pio::MappingPlaceholders placeholders;
    meitte::pio::MappingReleaseReport report = meitte::pio::release_file_mappings({path}, &placeholders);
    CHECK(report.supported);
    CHECK(report.error.empty());
    CHECK(report.views_unmapped == 1);
    CHECK(report.bytes == size);
    CHECK(report.sections_closed == 1);
    CHECK(report.plugs_missed == 0);
    CHECK(!placeholders.empty());
    CHECK(state_at(mapping.view) == MEM_RESERVE);
    CHECK(meitte::pio::addresses_in_file_mappings({path}, {mapping.view}) == 0);
    CHECK(WaitForSingleObject(mapping.section, 0) == WAIT_TIMEOUT);

    CHECK(UnmapViewOfFile(mapping.view) == FALSE);
    CHECK(CloseHandle(mapping.section) == TRUE);

    meitte::pio::MappingPlaceholders second;
    report = meitte::pio::release_file_mappings({path}, &second);
    CHECK(report.supported && report.views_unmapped == 0 && report.sections_closed == 0);
    CHECK(second.empty());

    placeholders.release();
    CHECK(placeholders.empty());
    CHECK(state_at(mapping.view) == MEM_FREE);

    DeleteFileA(path.c_str());
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mapping_release: ok\n");
    return 0;
}

#else

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    char path[] = "/tmp/meitte-mapping-release-XXXXXX";
    const int file = mkstemp(path);
    CHECK(file >= 0);
    const size_t size = 8u << 20;
    CHECK(ftruncate(file, (off_t) size) == 0);
    void * view = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, file, 0);
    CHECK(view != MAP_FAILED);
    close(file);
    CHECK(meitte::pio::addresses_in_file_mappings({path}, {view}) == 1);

    meitte::pio::MappingPlaceholders placeholders;
    meitte::pio::MappingReleaseReport report = meitte::pio::release_file_mappings({path}, &placeholders);
    CHECK(report.supported);
    CHECK(report.error.empty());
    CHECK(report.views_unmapped == 1);
    CHECK(report.bytes == size);
    CHECK(report.sections_closed == 0);
    CHECK(!placeholders.empty());
    CHECK(meitte::pio::addresses_in_file_mappings({path}, {view}) == 0);

#ifdef MAP_FIXED_NOREPLACE
    void * clash = mmap(view, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    CHECK(clash == MAP_FAILED);
#endif
    CHECK(munmap(view, size) == 0);

    meitte::pio::MappingPlaceholders second;
    report = meitte::pio::release_file_mappings({path}, &second);
    CHECK(report.supported && report.views_unmapped == 0);
    placeholders.release();
    CHECK(placeholders.empty());

    unlink(path);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mapping_release: ok\n");
    return 0;
}

#endif
