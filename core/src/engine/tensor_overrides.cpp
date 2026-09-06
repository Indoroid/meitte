#include "tensor_overrides.h"

#include "ggml-backend.h"

#include <map>
#include <regex>
#include <sstream>

namespace meitte {

namespace {

std::map<std::string, ggml_backend_buffer_type_t> registered_buffer_types() {
    ggml_backend_load_all();

    std::map<std::string, ggml_backend_buffer_type_t> types;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t device = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(device) != GGML_BACKEND_DEVICE_TYPE_CPU) continue;
        ggml_backend_buffer_type_t type = ggml_backend_dev_buffer_type(device);
        if (type) types.emplace(ggml_backend_buft_name(type), type);
    }
    return types;
}

std::string known_types(const std::map<std::string, ggml_backend_buffer_type_t> & types) {
    std::ostringstream out;
    for (auto it = types.begin(); it != types.end(); ++it) {
        if (it != types.begin()) out << ", ";
        out << it->first;
    }
    return out.str();
}

} // namespace

std::vector<std::string> available_tensor_buffer_types() {
    const auto types = registered_buffer_types();
    std::vector<std::string> out;
    out.reserve(types.size());
    for (const auto & type : types)
        out.push_back(type.first);
    return out;
}

bool resolve_tensor_buffer_overrides(const std::vector<TensorBufferOverride> & requested,
                                     std::vector<llama_model_tensor_buft_override> & resolved,
                                     std::string & error) {
    resolved.clear();
    if (requested.empty()) return true;

    const size_t limit = llama_max_tensor_buft_overrides();
    if (requested.size() >= limit) {
        error = "too many tensor buffer overrides (maximum " + std::to_string(limit - 1) + ")";
        return false;
    }

    const auto types = registered_buffer_types();
    for (const TensorBufferOverride & override : requested) {
        try {
            std::regex pattern(override.pattern);
            (void) pattern;
        } catch (const std::regex_error &) {
            error = "invalid tensor override pattern: " + override.pattern;
            return false;
        }

        const auto type = types.find(override.buffer_type);
        if (type == types.end()) {
            error = "unknown tensor buffer type '" + override.buffer_type + "' (available: " + known_types(types) + ")";
            return false;
        }
        resolved.push_back({override.pattern.c_str(), type->second});
    }
    resolved.push_back({nullptr, nullptr});
    return true;
}

} // namespace meitte
