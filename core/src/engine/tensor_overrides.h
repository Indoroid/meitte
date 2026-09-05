#pragma once

#include "bmoe/config.h"

#include "llama.h"

#include <string>
#include <vector>

namespace meitte {

// Return the buffer types registered by llama.cpp that may safely be named by a caller.
std::vector<std::string> available_tensor_buffer_types();

// Translate user-facing tensor/buffer pairs into the null-terminated array required by llama.cpp.
// Overrides are validated before model loading so an invalid name cannot leave a partially loaded
// model behind. The caller owns `resolved` for as long as llama_model_load_from_file() uses it.
bool resolve_tensor_buffer_overrides(const std::vector<TensorBufferOverride> & requested,
                                     std::vector<llama_model_tensor_buft_override> & resolved,
                                     std::string & error);

} // namespace meitte
