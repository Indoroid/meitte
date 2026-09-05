#pragma once

#include "bmoe/config.h"
#include "bmoe/session.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace meitte {

struct SessionCommand {
    enum Kind { kGenerate, kError, kClose, kCancel } kind = kError;
    std::string prompt;
    std::vector<ChatMessage> messages;
    int id = 0;
    int n_predict = 128;
    bool think = true;
    std::string reasoning_effort;
    std::map<std::string, std::string> chat_template_kwargs;
    std::string error;
    // Unspecified follows the session's default. This lets --kv-preserve continue after the
    // first turn while retaining an explicit {"clear_kv":true} reset escape hatch.
    std::optional<bool> clear_kv;
};

// Parse one complete JSON line from the interactive-session protocol. Keeping the parser separate
// from stdin ownership makes cancellation and shutdown lifetime-safe in the CLI loop.
bool parse_session_command(const std::string & line, const RunConfig & cfg, SessionCommand & out, std::string & error);

} // namespace meitte
