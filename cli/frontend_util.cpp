#include "frontend_util.h"

#include "bmoe/auto_plan.h"
#include "bmoe/params.h"
#include "bmoe/probe.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace meitte::frontend {

using json = nlohmann::json;

std::string json_escape(const std::string & s) {
    const std::string quoted = json(s).dump(-1, ' ', false, json::error_handler_t::replace);
    return quoted.size() >= 2 ? quoted.substr(1, quoted.size() - 2) : std::string{};
}

bool read_text_file(const std::string & path, std::string & out, std::string & error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot read " + path;
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

std::string normalize_reasoning_effort(std::string value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "low" || lower == "medium" || lower == "high" || lower == "none") return lower;
    return value;
}

bool parse_template_kwargs(const json & value,
                           std::map<std::string, std::string> & out,
                           std::optional<bool> & generic_think,
                           std::optional<std::string> & generic_effort,
                           std::string & error) {
    if (!value.is_object()) {
        error = "chat_template_kwargs must be an object";
        return false;
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (it.key().empty() || it.key().size() > 128) {
            error = "chat_template_kwargs keys must be 1..128 bytes";
            return false;
        }
        if (it.key() == "enable_thinking") {
            if (!it.value().is_boolean()) {
                error = "chat_template_kwargs.enable_thinking must be a boolean";
                return false;
            }
            generic_think = it.value().get<bool>();
        } else if (it.key() == "reasoning_effort") {
            if (!it.value().is_string()) {
                error = "chat_template_kwargs.reasoning_effort must be a string";
                return false;
            }
            generic_effort = normalize_reasoning_effort(it.value().get<std::string>());
            if (generic_effort->empty()) {
                error = "chat_template_kwargs.reasoning_effort must be non-empty";
                return false;
            }
        } else {
            out[it.key()] = it.value().dump();
        }
    }
    return true;
}

static bool is_extension(const std::string & full, const std::string & prev) {
    return full.size() >= prev.size() && full.compare(0, prev.size(), prev) == 0;
}

void emit_progress_line(const TokenMetrics & m, ProgressDelta & st) {
    if (m.read_bytes || m.io_ms > 0.0)
        std::printf("BMOE_LOAD {\"mb\":%.2f,\"ms\":%.1f}\n", m.read_bytes / (1024.0 * 1024.0), m.io_ms);
    const bool ext = is_extension(m.reasoning, st.reasoning) && is_extension(m.text, st.text);
    const std::string d_reason = ext ? m.reasoning.substr(st.reasoning.size()) : m.reasoning;
    const std::string d_text = ext ? m.text.substr(st.text.size()) : m.text;
    std::printf("BMOE_PROGRESS {\"step\":%d,\"steps\":%d,\"wall_ms\":%.1f,\"io_ms\":%.1f,"
                "\"compute_ms\":%.1f,\"mgmt_ms\":%.1f,\"stall_ms\":%.1f,\"read_mb\":%.2f,"
                "\"cache_hit_pct\":%.1f,\"majflt\":%llu,\"cpu_ms\":%.1f,\"dense_resident_frac\":%.3f,"
                "%s\"delta_reasoning\":\"%s\",\"delta_text\":\"%s\"}\n",
                m.step, m.steps, m.wall_ms, m.io_ms, m.compute_ms, m.mgmt_ms, m.stall_ms,
                m.read_bytes / (1024.0 * 1024.0), m.cache_hit_pct, (unsigned long long) m.majflt, m.cpu_ms,
                m.dense_resident_frac, ext ? "" : "\"reset\":1,", json_escape(d_reason).c_str(),
                json_escape(d_text).c_str());
    st.reasoning = m.reasoning;
    st.text = m.text;
    std::fflush(stdout);
}

std::vector<std::string> pinned_param_keys(const std::set<std::string> & typed_flags) {
    std::vector<std::string> keys;
    for (const ParamDesc & d : params()) {
        bool typed = (!d.flag.empty() && typed_flags.count(d.flag)) ||
                     (!d.short_flag.empty() && typed_flags.count(d.short_flag));
        for (const ParamSwitch & sw : d.switches)
            typed = typed || typed_flags.count(sw.flag);
        if (typed) keys.push_back(d.key);
    }
    return keys;
}

std::string param_key_for_env(const std::string & env) {
    static const std::map<std::string, std::string> keys = {
        {"BMOE_CACHE_MB", "cache-mb"},
        {"BMOE_IO_THREADS", "io-threads"},
        {"BMOE_OVERLAP", "overlap"},
        {"BMOE_PREFETCH", "prefetch"},
        {"BMOE_N_EXPERT_USED", "n-expert-used"},
        {"BMOE_PREDICT_LOG", "predict-log"},
        {"BMOE_PREDICT_PREFETCH", "predict-prefetch"},
    };
    auto it = keys.find(env);
    return it == keys.end() ? std::string() : it->second;
}

bool parse_planner_flag(const std::string & arg,
                        PlannerFlags & f,
                        const std::function<const char *(const char *)> & next) {
    if (arg == "--describe-params")
        f.describe_params = true;
    else if (arg == "--show-config")
        f.show_config = true;
    else if (arg == "--auto" || arg == "--plan-run")
        f.auto_plan = true;
    else if (arg == "--plan" || arg == "--plan-only")
        f.auto_plan = f.plan_explain = f.plan_only = true;
    else if (arg == "--probe") {
        f.auto_plan = f.plan_explain = f.plan_only = true;
        f.probe_io = f.probe_mem = true;
    } else if (arg == "--plan-explain")
        f.plan_explain = true;
    else if (arg == "--probe-io")
        f.auto_plan = f.probe_io = true;
    else if (arg == "--no-probe-io")
        f.probe_io = false;
    else if (arg == "--probe-device")
        f.probe_device = next("--probe-device");
    else if (arg == "--no-isolated-probes")
        f.isolated_probes = false;
    else if (arg == "--probe-mem")
        f.auto_plan = f.probe_mem = true;
    else
        return false;
    return true;
}

void print_planner_usage() {
    std::printf("\n  Hardware planner (docs/hardware-planning.md):\n"
                "      --auto              measure the machine and the model, plan the knobs you did not set, run\n"
                "      --plan              print the plan and the flags that reproduce it, then exit\n"
                "      --plan-explain      with --auto: print the plan, then run it\n"
                "      --probe             --plan, plus the storage and memory probes\n"
                "      --no-probe-io       plan without the storage read probe (touches no drive)\n"
                "      --probe-mem         hold memory until the kernel takes some back (intrusive)\n"
                "      --no-isolated-probes  probe compute devices in this process instead of a child each\n"
                "      --describe-params   print every engine parameter as JSON and exit\n"
                "      --show-config       print the resolved configuration as JSON and exit\n"
                "\n  Prefill device, decisions and placement (from the engine's parameter table):\n");
    static const std::set<std::string> keys = {"threads-batch",
                                               "gpu-layers",
                                               "devices",
                                               "dense-on-device",
                                               "tensor-overrides",
                                               "prefill-device",
                                               "prefill-best-effort",
                                               "prefill-min-tokens",
                                               "prefill-slots",
                                               "prefill-slots-on-demand",
                                               "prefill-loaders",
                                               "prefill-routed",
                                               "prefill-routed-full",
                                               "decide",
                                               "decide-prefix-cache",
                                               "decide-probe",
                                               "cache-pin",
                                               "cache-pin-fit"};
    for (const ParamDesc & d : params()) {
        if (!keys.count(d.key)) continue;
        std::string spelled =
            d.flag.empty() ? std::string() : d.flag + (d.value_hint.empty() ? "" : " " + d.value_hint);
        for (const ParamSwitch & sw : d.switches)
            if (!sw.deprecated) spelled += (spelled.empty() ? "" : ", ") + sw.flag;
        std::printf("      %-26s %s\n", spelled.c_str(), d.label.c_str());
    }
}

int apply_table_flag(RunConfig & cfg, int argc, char ** argv, int & i, const char * who) {
    const FlagResult r = apply_flag(cfg, argv[i], i + 1 < argc ? argv[i + 1] : nullptr);
    if (!r.matched) {
        std::fprintf(stderr, "%s: unknown arg: %s\n", who, argv[i]);
        return 1;
    }
    if (!r.error.empty()) {
        std::fprintf(stderr, "%s: %s: %s\n", who, argv[i], r.error.c_str());
        return 2;
    }
    if (r.consumed_value) ++i;
    return 0;
}

int run_planner_steps(RunConfig & cfg,
                      const PlannerFlags & f,
                      const std::set<std::string> & typed_flags,
                      const char * self_name,
                      double & predicted_s_per_token) {
    predicted_s_per_token = 0.0;
    if (f.describe_params) {
        std::printf("%s\n", params_json(cfg).c_str());
        return 0;
    }
    // The child of an isolated device probe: one device, its facts on one line, nothing else. If the
    // driver ends this process, or it never returns, that is the parent's answer about the device.
    if (!f.probe_device.empty()) {
        std::string facts;
        if (!probe_device_for_parent(cfg.model_path.c_str(), f.probe_device, facts)) {
            std::fprintf(stderr, "%s: no such device to probe: %s\n", self_name, f.probe_device.c_str());
            return 3;
        }
        std::printf("%s %s\n", device_facts_marker, facts.c_str());
        std::fflush(stdout);
        return 0;
    }
    // The planner runs after the flags, the env overrides and the frontend's own defaults, so
    // anything the caller expressed either way is a pin it may not touch.
    const bool plan = f.auto_plan || typed_flags.count("--dense-on-device");
    if (plan && !cfg.model_path.empty()) {
        AutoPlanOptions options;
        options.pinned = pinned_param_keys(typed_flags);
        for (const char * env : {"BMOE_CACHE_MB", "BMOE_IO_THREADS", "BMOE_OVERLAP", "BMOE_PREFETCH",
                                 "BMOE_N_EXPERT_USED", "BMOE_PREDICT_LOG", "BMOE_PREDICT_PREFETCH"})
            if (const char * v = std::getenv(env); v && *v) options.pinned.push_back(param_key_for_env(env));
        options.probe_io = f.probe_io;
        options.probe_mem = f.probe_mem;
        if (f.isolated_probes) {
            const std::string model = cfg.model_path;
            // Generous on purpose: a device is given time to load the tensors it measures on.
            options.device_runner = self_process_runner(
                [model](const std::string & dev) {
                    return std::vector<std::string>{"-m", model, "--probe-device", dev};
                },
                /*timeout_seconds*/ 120.0);
        }
        const AutoPlanResult r = auto_plan(cfg, options);
        if (!r.ok) {
            std::fprintf(stderr, "%s: plan failed: %s\n", self_name, r.error.c_str());
            return 2;
        }
        cfg = r.plan.config;
        predicted_s_per_token = r.predicted_s_per_token;
        if (f.plan_explain) {
            std::fputs(r.report.c_str(), stderr);
            // The line that reproduces this plan by hand, printed last so it stays on screen.
            std::fprintf(stderr, "plan: reproduce with\n  %s -m %s %s\n", self_name, cfg.model_path.c_str(),
                         r.plan.to_flags().c_str());
        }
        if (f.plan_only) return 0;
    }
    // The configuration exactly as a run would get it, and whether validate() accepts it.
    if (f.show_config) {
        const ValidationResult v = validate(cfg);
        std::string args;
        for (const std::string & arg : to_args(cfg))
            args += (args.empty() ? "\"" : ",\"") + json_escape(arg) + "\"";
        std::printf("{\"config\":%s,\"args\":[%s],\"valid\":%s,\"error\":\"%s\"}\n", config_json(cfg).c_str(),
                    args.c_str(), v.ok ? "true" : "false", json_escape(v.error).c_str());
        return v.ok ? 0 : 1;
    }
    return -1;
}

} // namespace meitte::frontend
