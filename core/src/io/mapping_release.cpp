#include "mapping_release.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <algorithm>
#include <cwctype>
#include <map>
#else
#include "platform_io.h"
#include <sys/mman.h>
#endif

namespace meitte::pio {

MappingPlaceholders::~MappingPlaceholders() {
    release();
}

MappingPlaceholders::MappingPlaceholders(MappingPlaceholders && other) noexcept
    : reserved_(std::move(other.reserved_)), plugs_(std::move(other.plugs_)) {
    other.reserved_.clear();
    other.plugs_.clear();
}

MappingPlaceholders & MappingPlaceholders::operator=(MappingPlaceholders && other) noexcept {
    if (this != &other) {
        release();
        reserved_ = std::move(other.reserved_);
        plugs_ = std::move(other.plugs_);
        other.reserved_.clear();
        other.plugs_.clear();
    }
    return *this;
}

void MappingPlaceholders::release() {
#if defined(_WIN32)
    for (const auto & range : reserved_)
        VirtualFree(range.first, 0, MEM_RELEASE);
#else
    // The model's late munmap removes each POSIX placeholder. A second munmap here could remove a
    // newer allocation at the same address, so forget the ranges after model destruction.
#endif
    // Windows handle plugs are also closed by the model's late teardown. Closing them here could
    // close a newer object if the model already consumed the plug.
    reserved_.clear();
    plugs_.clear();
}

#if defined(_WIN32)

namespace {

std::wstring widen(const std::string & value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int) value.size(), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide((size_t) size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), (int) value.size(), &wide[0], size);
    return wide;
}

std::wstring lower(std::wstring value) {
    for (wchar_t & character : value)
        character = (wchar_t) std::towlower(character);
    return value;
}

std::wstring nt_path_of(const std::wstring & path) {
    HANDLE file = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    wchar_t buffer[32768];
    const DWORD size =
        GetFinalPathNameByHandleW(file, buffer, (DWORD) (sizeof(buffer) / sizeof(buffer[0])), VOLUME_NAME_NT);
    CloseHandle(file);
    if (size == 0 || size >= sizeof(buffer) / sizeof(buffer[0])) return {};
    return lower(std::wstring(buffer, size));
}

using GetMappedFileNameW_t = DWORD(WINAPI *)(HANDLE, LPVOID, LPWSTR, DWORD);
using NtQueryInformationProcess_t = NTSTATUS(NTAPI *)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
using NtQueryObject_t = NTSTATUS(NTAPI *)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);

std::wstring mapped_name(GetMappedFileNameW_t function, void * address) {
    wchar_t buffer[32768];
    const DWORD size = function(GetCurrentProcess(), address, buffer, (DWORD) (sizeof(buffer) / sizeof(buffer[0])));
    if (size == 0) return {};
    return lower(std::wstring(buffer, size));
}

bool is_target(const std::vector<std::wstring> & targets, const std::wstring & name) {
    return !name.empty() && std::find(targets.begin(), targets.end(), name) != targets.end();
}

std::vector<std::pair<void *, size_t>> find_views(GetMappedFileNameW_t function,
                                                  const std::vector<std::wstring> & targets) {
    std::map<void *, uintptr_t> ends;
    MEMORY_BASIC_INFORMATION info;
    const char * address = nullptr;
    while (VirtualQuery(address, &info, sizeof(info)) == sizeof(info)) {
        if (info.Type == MEM_MAPPED && info.State == MEM_COMMIT &&
            is_target(targets, mapped_name(function, info.BaseAddress))) {
            const uintptr_t end = (uintptr_t) info.BaseAddress + info.RegionSize;
            uintptr_t & current = ends[info.AllocationBase];
            current = std::max(current, end);
        }
        const char * next = (const char *) info.BaseAddress + info.RegionSize;
        if (next <= address) break;
        address = next;
    }
    std::vector<std::pair<void *, size_t>> result;
    for (const auto & entry : ends)
        result.emplace_back(entry.first, (size_t) (entry.second - (uintptr_t) entry.first));
    return result;
}

void * reserve_placeholder(void * base, size_t size) {
    if (void * result = VirtualAlloc(base, size, MEM_RESERVE, PAGE_NOACCESS)) return result;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const size_t granularity = info.dwAllocationGranularity;
    const size_t reduced = size & ~(granularity - 1);
    return reduced && reduced < size ? VirtualAlloc(base, reduced, MEM_RESERVE, PAGE_NOACCESS) : nullptr;
}

struct HandleEntry {
    HANDLE HandleValue;
    ULONG_PTR HandleCount;
    ULONG_PTR PointerCount;
    ULONG GrantedAccess;
    ULONG ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
};

struct HandleSnapshot {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    HandleEntry Handles[1];
};

constexpr int kProcessHandleInformation = 51;
constexpr NTSTATUS kStatusInfoLengthMismatch = (NTSTATUS) 0xC0000004L;
constexpr DWORD kSectionMapRead = 0x0004;
constexpr ULONG kProtectFromClose = 0x2;

std::vector<char> handle_snapshot(NtQueryInformationProcess_t query) {
    std::vector<char> buffer(1 << 16);
    for (int attempt = 0; attempt < 8; ++attempt) {
        ULONG needed = 0;
        const NTSTATUS status = query(GetCurrentProcess(), (PROCESSINFOCLASS) kProcessHandleInformation, buffer.data(),
                                      (ULONG) buffer.size(), &needed);
        if (status == kStatusInfoLengthMismatch) {
            buffer.resize((size_t) needed + (1 << 14));
            continue;
        }
        if (status < 0) return {};
        return buffer;
    }
    return {};
}

bool is_section(NtQueryObject_t query, HANDLE handle) {
    alignas(PUBLIC_OBJECT_TYPE_INFORMATION) char buffer[sizeof(PUBLIC_OBJECT_TYPE_INFORMATION) + 512];
    ULONG length = 0;
    if (query(handle, ObjectTypeInformation, buffer, (ULONG) sizeof(buffer), &length) < 0) return false;
    const auto * info = (const PUBLIC_OBJECT_TYPE_INFORMATION *) buffer;
    const size_t size = info->TypeName.Length / sizeof(wchar_t);
    return size == 7 && std::wstring(info->TypeName.Buffer, size) == L"Section";
}

HANDLE plug_handle_slot(HANDLE value) {
    std::vector<HANDLE> misses;
    HANDLE plug = nullptr;
    for (int attempt = 0; attempt < 16 && !plug; ++attempt) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) break;
        if (event == value)
            plug = event;
        else
            misses.push_back(event);
    }
    for (HANDLE miss : misses)
        CloseHandle(miss);
    return plug;
}

} // namespace

size_t addresses_in_file_mappings(const std::vector<std::string> & paths, const std::vector<const void *> & addresses) {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto mapped = kernel ? (GetMappedFileNameW_t) (void *) GetProcAddress(kernel, "K32GetMappedFileNameW") : nullptr;
    if (!mapped) return 0;
    std::vector<std::wstring> targets;
    for (const std::string & path : paths) {
        std::wstring target = nt_path_of(widen(path));
        if (!target.empty()) targets.push_back(std::move(target));
    }
    size_t hits = 0;
    for (const void * address : addresses) {
        if (!address) continue;
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.Type != MEM_MAPPED) continue;
        if (is_target(targets, mapped_name(mapped, (void *) address))) ++hits;
    }
    return hits;
}

MappingReleaseReport release_file_mappings(const std::vector<std::string> & paths, MappingPlaceholders * out) {
    MappingReleaseReport report;
    report.supported = true;

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto mapped = kernel ? (GetMappedFileNameW_t) (void *) GetProcAddress(kernel, "K32GetMappedFileNameW") : nullptr;
    auto query_process =
        ntdll ? (NtQueryInformationProcess_t) (void *) GetProcAddress(ntdll, "NtQueryInformationProcess") : nullptr;
    auto query_object = ntdll ? (NtQueryObject_t) (void *) GetProcAddress(ntdll, "NtQueryObject") : nullptr;
    if (!mapped || !query_process || !query_object) {
        report.error = "required kernel32/ntdll entry points unavailable";
        return report;
    }

    std::vector<std::wstring> targets;
    for (const std::string & path : paths) {
        std::wstring target = nt_path_of(widen(path));
        if (target.empty()) {
            if (report.error.empty()) report.error = "cannot identify " + path;
        } else {
            targets.push_back(std::move(target));
        }
    }
    if (targets.empty()) return report;

    for (const auto & view : find_views(mapped, targets)) {
        if (!UnmapViewOfFile(view.first)) {
            if (report.error.empty()) report.error = "UnmapViewOfFile failed";
            continue;
        }
        ++report.views_unmapped;
        report.bytes += view.second;
        if (void * placeholder = reserve_placeholder(view.first, view.second)) {
            if (out) out->reserved_.emplace_back(placeholder, view.second);
        } else if (report.error.empty()) {
            report.error = "placeholder reservation failed";
        }
    }

    const std::vector<char> snapshot = handle_snapshot(query_process);
    if (snapshot.empty()) {
        if (report.error.empty()) report.error = "process handle snapshot unavailable";
        return report;
    }
    const auto * handles = (const HandleSnapshot *) snapshot.data();
    for (ULONG_PTR index = 0; index < handles->NumberOfHandles; ++index) {
        const HandleEntry & entry = handles->Handles[index];
        if ((entry.HandleAttributes & kProtectFromClose) || !(entry.GrantedAccess & kSectionMapRead)) continue;
        if (!is_section(query_object, entry.HandleValue)) continue;
        void * probe = MapViewOfFile(entry.HandleValue, FILE_MAP_READ, 0, 0, 1);
        if (!probe) continue;
        const bool match = is_target(targets, mapped_name(mapped, probe));
        UnmapViewOfFile(probe);
        if (!match) continue;
        if (!CloseHandle(entry.HandleValue)) {
            if (report.error.empty()) report.error = "CloseHandle on section failed";
            continue;
        }
        ++report.sections_closed;
        if (HANDLE plug = plug_handle_slot(entry.HandleValue)) {
            if (out) out->plugs_.push_back(plug);
        } else {
            ++report.plugs_missed;
        }
    }
    return report;
}

#else

size_t addresses_in_file_mappings(const std::vector<std::string> & paths, const std::vector<const void *> & addresses) {
    std::vector<MappedRegion> all;
    for (const std::string & path : paths) {
        const size_t slash = path.find_last_of('/');
        const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);
        std::vector<MappedRegion> regions;
        if (file_mapped_regions(basename.c_str(), regions)) all.insert(all.end(), regions.begin(), regions.end());
    }
    size_t hits = 0;
    for (const void * address : addresses) {
        const uintptr_t value = (uintptr_t) address;
        for (const MappedRegion & region : all)
            if (value >= region.start && value < region.end) {
                ++hits;
                break;
            }
    }
    return hits;
}

MappingReleaseReport release_file_mappings(const std::vector<std::string> & paths, MappingPlaceholders * out) {
    MappingReleaseReport report;
    report.supported = true;
    for (const std::string & path : paths) {
        const size_t slash = path.find_last_of('/');
        const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);
        std::vector<MappedRegion> regions;
        if (!file_mapped_regions(basename.c_str(), regions)) {
            if (report.error.empty()) report.error = "cannot read /proc/self/maps";
            continue;
        }
        for (const MappedRegion & region : regions) {
            void * address = (void *) region.start;
            const size_t size = (size_t) (region.end - region.start);
            if (munmap(address, size) != 0) {
                if (report.error.empty()) report.error = "munmap failed";
                continue;
            }
            ++report.views_unmapped;
            report.bytes += size;
            void * placeholder =
                mmap(address, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
            if (placeholder == MAP_FAILED) {
                if (report.error.empty()) report.error = "placeholder mapping failed";
            } else if (out) {
                out->reserved_.emplace_back(placeholder, size);
            }
        }
    }
    return report;
}

#endif

} // namespace meitte::pio
