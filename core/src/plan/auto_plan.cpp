#include "bmoe/auto_plan.h"

#include "bmoe/planner.h"

#include "llama.h"

#include <cstdio>
#include <string>

namespace meitte {

namespace {

std::string report_of(const HardwareProfile & hw, const ModelProfile & mp, const Plan & plan) {
    std::string out;
    char line[512];
    std::snprintf(line, sizeof(line), "plan: machine %s\n", hw.label.c_str());
    out += line;
    // Every device with what it measured, so a decision about one can be checked against the figures
    // it was made from. 0 is "not measured", never "slow".
    for (const ComputeDevice & d : hw.devices) {
        const auto verdict = [](Tri t) {
            return t == Tri::Yes ? "same result" : t == Tri::No ? "WRONG result" : "unverified";
        };
        if (d.usable == Tri::No) {
            std::snprintf(line, sizeof(line), "plan: device %s: UNUSABLE - %s\n", d.name.c_str(),
                          d.unusable_reason.c_str());
            out += line;
            continue;
        }
        std::snprintf(line, sizeof(line),
                      "plan: device %s%s: one token %.0f GiB/s (%s), %u tokens wide %.0f GiB/s (%s)%s\n",
                      d.name.c_str(), d.is_cpu ? " (host)" : "", d.memory_bandwidth_gibs, verdict(d.identity_ok),
                      hw.wide_batch, d.wide_matmul_gibs, verdict(d.wide_identity_ok),
                      d.probed_out_of_process ? ", probed in a process of its own" : "");
        out += line;
    }
    if (mp.ok) {
        std::snprintf(line, sizeof(line), "plan: model %s, %u experts top-%u over %u of %u blocks\n", mp.arch.c_str(),
                      mp.n_expert, mp.n_expert_used, mp.n_moe_layer, mp.n_layer);
        out += line;
        for (int gi = 0; gi < (int) WeightGroup::count; ++gi) {
            const GroupDemand & d = mp.groups[gi];
            if (d.bytes == 0) continue;
            std::snprintf(line, sizeof(line), "plan:   %-10s %7llu MiB, %7llu MiB/token%s\n",
                          group_name((WeightGroup) gi), (unsigned long long) (d.bytes >> 20),
                          (unsigned long long) (d.bytes_per_token >> 20),
                          d.row_gatherable ? "  (row-gathered)"
                          : d.streamable   ? "  (streamable)"
                                           : "");
            out += line;
        }
    }
    out += plan.explain();
    return out;
}

} // namespace

AutoPlanResult auto_plan(const RunConfig & base, const AutoPlanOptions & options) {
    AutoPlanResult r;
    if (base.model_path.empty()) {
        r.error = "a plan needs a model path";
        return r;
    }
    PlanRequest req;
    req.pinned = options.pinned;

    register_backends();
    r.hw = probe_hardware(base.model_path.c_str());
    // Devices beyond the host are asked in a process of their own first, when the caller can start
    // one. A driver that ends the process it is used from then ends only the child; the probes below
    // leave such a device alone.
    if (options.device_runner) probe_devices_isolated(r.hw, options.device_runner);
    r.model = probe_model(base.model_path.c_str());
    if (!r.model.ok) {
        r.error = r.model.error.empty() ? "cannot read the model" : r.model.error;
        return r;
    }
    probe_device_support(r.hw, r.model);
    // The cache term is the FLOOR the engine enforces, not one token cycle. Written without std::max:
    // windows.h defines `max` as a macro in translation units that include it.
    const uint64_t guard_bytes = (uint64_t) MoeStreamConfig::cache_min_mb << 20;
    const uint64_t cache_floor = r.model.token_cycle_bytes > guard_bytes ? r.model.token_cycle_bytes : guard_bytes;
    llama_backend_init();
    probe_bandwidth(r.hw, r.model);
    probe_device_costs(r.hw, r.model);
    if (options.probe_io) probe_storage(r.hw, base.model_path.c_str(), r.model.expert_slice_bytes);
    probe_headroom(r.hw, options.probe_mem, r.model.dense_bytes + cache_floor);
    const Placement placement = probe_placement(base.model_path.c_str(), r.model, r.hw, (uint32_t) base.n_ctx);
    r.plan = plan_run(base, r.hw, r.model, placement, req);
    r.predicted_s_per_token = r.plan.allocation.seconds_per_token;
    if (r.plan.allocation.cache_bytes > 0 && r.model.expert_bytes > 0)
        r.predicted_hit_pct = 100.0 * (double) r.plan.allocation.cache_bytes / (double) r.model.expert_bytes;
    r.report = report_of(r.hw, r.model, r.plan);
    r.ok = true;
    return r;
}

bool probe_device_for_parent(const char * model_path, const std::string & device, std::string & facts) {
    register_backends();
    llama_backend_init();
    return probe_one_device(model_path, device, facts);
}

} // namespace meitte
