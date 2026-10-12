// One call that measures the machine and the model and plans a run: the probe order, the isolated
// device probes and the composition with llama.cpp's capacity fitter, in the core so that every
// frontend (meitte-cli --auto, meitte-server /api/plan, a libmeitte caller) plans the same way.
//
// The rules live in planner.h and the measurements in probe.h; this file only fixes their order.
// The order is a measurement, not a preference: backends are registered before anything looks at
// devices, bandwidth and device costs are measured before the storage probe reads gigabytes, and the
// intrusive headroom probe comes last, because it leaves the kernel busy and anything measured after
// it measures the recovery.
#pragma once

#include "bmoe/config.h"
#include "bmoe/hardware_profile.h"
#include "bmoe/model_profile.h"
#include "bmoe/plan.h"
#include "bmoe/probe.h"

#include <string>
#include <vector>

namespace meitte {

struct AutoPlanOptions {
    // Parameter keys (bmoe/params.h) the caller set. The planner never changes a pinned knob.
    std::vector<std::string> pinned;
    // Read from the drive that holds the model, to measure the storage rate by request size.
    bool probe_io = true;
    // Hold memory until the kernel takes some back. The one probe that puts a live machine under real
    // pressure, so it is off unless asked for.
    bool probe_mem = false;
    // Probes each compute device beyond the host in a process of its own (see probe.h). Empty: the
    // devices are probed in this process, which a faulty driver can end.
    DeviceProbeRunner device_runner;
};

struct AutoPlanResult {
    bool ok = false;
    std::string error;
    Plan plan;          // plan.config is the resolved run
    HardwareProfile hw; // what the machine reported
    ModelProfile model; // what the model reported
    std::string report; // the machine, every device with its figures, the model, then plan.explain()
    double predicted_s_per_token = 0.0;
    double predicted_hit_pct = -1.0; // -1: no streamed cache in this plan
};

// Plan `base` for this machine. Initializes the llama.cpp backend (process-global, idempotent).
// Fails only when the model cannot be read; a machine that reports little gets a plan that touches
// little, and every knob the plan leaves alone keeps the value it had in `base`.
AutoPlanResult auto_plan(const RunConfig & base, const AutoPlanOptions & options);

// The child side of an isolated device probe (AutoPlanOptions::device_runner): registers the
// backends, initializes llama.cpp and measures `device` alone. A frontend's child mode prints
// `device_facts_marker` and `facts` on one line and exits. False when the device is not there.
bool probe_device_for_parent(const char * model_path, const std::string & device, std::string & facts);

} // namespace meitte
