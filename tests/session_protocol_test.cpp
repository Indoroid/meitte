#include "session_protocol.h"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char * message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

} // namespace

int main() {
    meitte::RunConfig cfg;
    cfg.n_predict = 17;
    meitte::SessionCommand command;
    std::string error;

    check(meitte::parse_session_command(R"({"cmd":"generate","id":4,"prompt":"hello"})", cfg, command, error),
          "generate command parses");
    check(command.kind == meitte::SessionCommand::kGenerate && command.id == 4 && command.n_predict == 17,
          "generate command retains defaults");
    check(!command.clear_kv.has_value(), "clear_kv is omitted when the command leaves the session default intact");

    error.clear();
    check(meitte::parse_session_command(R"({"cmd":"generate","prompt":"continue","clear_kv":false})", cfg, command,
                                        error) &&
              command.clear_kv && !*command.clear_kv,
          "explicit clear_kv=false requests KV continuation");

    error.clear();
    check(meitte::parse_session_command(R"({"cmd":"cancel"})", cfg, command, error) &&
              command.kind == meitte::SessionCommand::kCancel,
          "cancel command parses once and is identifiable by the reader");

    error.clear();
    check(
        !meitte::parse_session_command(
            R"({"cmd":"generate","messages":[{"role":"assistant","tool_calls":[{"id":7,"function":{"name":"f","arguments":"{}"}}]}]})",
            cfg, command, error),
        "wrong tool-call id type is rejected");

    error.clear();
    check(!meitte::parse_session_command(R"({"cmd":"generate","id":9223372036854775808,"prompt":"x"})", cfg, command,
                                         error),
          "out-of-range command ids are rejected without throwing");

    return failures == 0 ? 0 : 1;
}
