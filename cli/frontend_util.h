#pragma once

// Helpers shared by the meitte-cli and meitte-server frontends: the BMOE_* line-protocol progress
// line, JSON string escaping, and the request-control parsing both protocols accept. One copy, so the
// two frontends cannot drift apart. Frontend code only: libmeitte never links this.

#include "bmoe/config.h"
#include "bmoe/metrics.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace meitte::frontend {

// `s` as the inside of a JSON string literal; invalid UTF-8 is replaced, never emitted raw.
std::string json_escape(const std::string & s);

bool read_text_file(const std::string & path, std::string & out, std::string & error);

// "low" / "medium" / "high" / "none" in any case become lowercase; other values pass through.
std::string normalize_reasoning_effort(std::string value);

// Parse a chat_template_kwargs object into serialized JSON values. enable_thinking and
// reasoning_effort are generic controls, returned separately rather than passed to the template.
bool parse_template_kwargs(const nlohmann::json & value,
                           std::map<std::string, std::string> & out,
                           std::optional<bool> & think,
                           std::optional<std::string> & effort,
                           std::string & error);

// What BMOE_PROGRESS already delivered this generation, so each line carries only the new tail.
// One state per generation: start each generation with a fresh object.
struct ProgressDelta {
    std::string reasoning;
    std::string text;
};

// One token's line-protocol output: the optional BMOE_LOAD, then BMOE_PROGRESS (docs/telemetry.md).
//
// The answer travels as a DELTA: sending the cumulative text every token made a generation of n
// tokens write, escape and parse O(n^2) bytes (#119). The common case appends the suffix since the
// last line; when a closing tag makes common_chat_parse retroactively reclassify answer text as
// reasoning, an append cannot express it, so the line carries the full snapshot with "reset":1 and
// the reader replaces instead of appending. The full final text still travels in BMOE_DONE.
void emit_progress_line(const TokenMetrics & m, ProgressDelta & state);

// The parameter keys (bmoe/params.h) of the flags a person typed, for AutoPlanOptions::pinned. A
// value flag maps to its key, a bare switch (--no-odirect) to the key it sets (o-direct). Flags the
// table does not describe are frontend options and pin nothing.
std::vector<std::string> pinned_param_keys(const std::set<std::string> & typed_flags);

// The hardware planner and the parameter-table introspection, shared by meitte-cli and
// meitte-server so both accept the same spellings and plan the same way (docs/hardware-planning.md).
struct PlannerFlags {
    bool auto_plan = false;    // --auto: measure, plan the knobs nobody set, then run
    bool plan_explain = false; // print the plan before running it
    bool plan_only = false;    // --plan: print the plan and the flags that reproduce it, then exit
    bool probe_io = true;      // the storage read probe
    bool probe_mem = false;    // the intrusive memory-holding probe
    bool isolated_probes = true;
    bool describe_params = false;
    bool show_config = false;
    std::string probe_device; // the child mode of an isolated device probe
};

// True when `arg` is one of the flags above; a value is taken through `next`.
bool parse_planner_flag(const std::string & arg,
                        PlannerFlags & flags,
                        const std::function<const char *(const char *)> & next);

// The flags above in a usage text, then the engine parameters only the table spells out.
void print_planner_usage();

// Match argv[i] against the engine's parameter table and apply it to `cfg`, advancing `i` past a
// consumed value. 0 on success; 1 for an unknown flag (the caller prints its usage); 2 for a known
// flag with a rejected value. `who` prefixes the message.
int apply_table_flag(RunConfig & cfg, int argc, char ** argv, int & i, const char * who);

// After the flags and the env overrides are resolved: the isolated-probe child, --describe-params,
// the planner and --show-config, in that order. Returns an exit code when the process is done, or -1
// to go on with `cfg` (planned when asked). `self_name` names the binary in the reproduce line.
int run_planner_steps(RunConfig & cfg,
                      const PlannerFlags & flags,
                      const std::set<std::string> & typed_flags,
                      const char * self_name,
                      double & predicted_s_per_token);

// The parameter key an environment override sets ("BMOE_CACHE_MB" -> "cache-mb"), or empty.
std::string param_key_for_env(const std::string & env);

} // namespace meitte::frontend
