# Architecture

Meitte is a small ports-and-adapters engine that sits **on top of** llama.cpp's
public API. Its guiding constraint: drive streaming through the public API so upstream
updates cost a submodule pointer bump and nothing else. The serial streamer holds to this
against stock upstream. Optional expert overlap uses a CPU readiness callback on the
synced fork branch (see [seam.md § 4](seam.md)).

## Layers

```
cli/            meitte-cli and meitte-server — parse flags; the only place env vars are read
core/
  include/bmoe/ ports (interfaces) + config, pure policy, no llama.cpp dependency
    config.h        RunConfig + validate()
    params.h        the parameter table: every RunConfig tunable described once (flag, type,
                    bounds, help); the CLI parses from it, front-ends render settings from its JSON
    expert_source.h IExpertSource — the residency strategy port
    row_source.h    IRowSource - the row-gathered residency port
    recipe.h        MoeRecipe + registry
    metrics.h       TokenMetrics / RunSummary + IMetricsSink
    runtime.h       run() entry point
    hardware_profile.h, model_profile.h, placement.h
                    the planner's inputs: a machine, a model and llama.cpp's capacity placement as
                    facts, with no platform, vendor or architecture name in them
    planner.h, allocate.h, plan.h
                    the planning rules: (machine, model, request) -> a resolved RunConfig with the
                    reason for each knob; the cost model and the memory ledger; the plan as a value
    probe.h         the adapters' entry points: what fills the two profiles
    auto_plan.h     one call that probes in order and plans: what --auto and /api/plan run
    decide.h        DecideRequest / DecideResult: choose from a list with one prefill
  src/
    io/         the platform layer — every OS conditional in core/ lives here
                platform_io — O_DIRECT reads + reserve/commit/evict VM, CPU spin hint
                file_reader — pooled positioned reader, per-consumer O_DIRECT
                mapping_release — safe model-file unmap and teardown placeholders
                subprocess — run a helper program and capture its output (FFmpeg audio)
    moe/        gguf_offsets (tensor → (shard, offset), split ggufs included), arch_registry,
                expert_stream_source (one reader per shard)
                router_hook — capture, eval-callback dispatch, route/compute traces
                router_policy — cache-aware dropping and substitution
                router_predict — next-layer prediction, its probe, route-ahead
                dense_weights — non-expert weight policy + the residency sensor
                row_stream - row-gathered tables served from flash (see row-gathered-tables.md)
                device_arena - one or two layer slots on a prefill device, held for the session or
                  only during a prefill, filled from flash, whole layers or only the routed experts
                  (npu-prefill.md)
                decide_probe - experimental per-decision expert usage and layer-exit answers (decide.md)
    engine/     session — the Session pimpl; state in session_impl.h, split by phase:
                  session_open (load, contexts, capture, binding), session_generate (prefill +
                  decode/speculation loop), session_perplexity, session_context (growth,
                  summarization, MTP draft context), session_decide
                prefill_device - moves layer weights and model state onto a device per graph
                prefill_path - the prefill device for one session: setup, placement, device decode
                runtime — the one-shot run() wrapper over a Session
                c_api — stable opaque handles for C and FFI callers
                llama_glue — enum mapping, batches, tokenization, sampler chain (llama.h only)
                chat_render — request messages/tools ↔ chat template inputs (llama.cpp `common`)
                chat_parse — reasoning-parser wiring (llama.cpp `common`, see seam.md)
                thinking_control — how "thinking off" is honoured, probed per model
                logits, prefill_support — log-softmax, batch filling and prefill attribution,
                  shared by generate / perplexity / decide
                decide/ — Session::decide() (see decide.md): pure policy over an IDecideBackend
                  port (prompt split, choice scoring, IPrefixCache), plus one llama.cpp adapter
    plan/       the hardware planner (hardware-planning.md). Two halves that must not mix:
                planner, allocate, plan - pure rules over the profiles; no I/O, no platform names
                hardware_probe, model_probe, storage_probe, headroom_probe, bandwidth_probe,
                  device_probe, placement_probe, isolated_probe (a device asked in a second
                  process, so a driver that ends it ends only that) - the adapters that measure a
                  machine and read a
                  gguf; the only place in the planner where a platform or llama.cpp may appear
                auto_plan - the probe order and the plan, one call shared by every frontend
    params.cpp  the parameter table's rows (see params.h)
    multimodal/ mtmd projector adapter + bounded file, FFmpeg audio, and sampled-video input
    metrics/    csv_metrics_sink, route_trace_sink, decode_trace_sink
third_party/
  llama.cpp     upstream submodule; public-API consumer, plus optional CPU readiness hooks
tests/          byte-identity gates
examples/android an APK that drives meitte-cli via ProcessBuilder
```

Dependencies point inward: adapters depend on the port headers, the CLI and the server compose
them. The server and the web UI meet only at the HTTP API; the UI knows no engine parameter by
name, it renders whatever `GET /api/params` describes.
The pure-policy code (`config.cpp`, `arch_registry.cpp`) compiles with no native
dependency, so a subset of the project builds and is testable before llama.cpp is
fetched.

## Why the streaming seam needs no fork

Streaming experts serially needs three things from the inference engine. All three are
already public in llama.cpp:

1. **A hook at routing time.** `llama_context_params.cb_eval` is called for every graph
   node. We ask for the routing nodes (`ffn_moe_topk-<il>`); ggml computes and
   synchronizes each alone, then calls us back with the selected expert ids materialized.
   The route trace and [cache-aware dropping](expert-dropping.md) additionally ask for each
   layer's `ffn_moe_weights*-<il>` chain — and dropping and
   [substitution](cache-aware-substitution.md) are the two paths that *write into* a graph
   tensor's contents (the weights, and the ids) rather than only rebinding `->data`. See
   [seam.md](seam.md).
2. **The expert tensor pointers.** During a one-token warm-up we scan each graph node's
   sources for recipe-named tensors such as `blk.<il>.ffn_{gate,up,down}_exps.weight`,
   `blk.<il>.ffn_gate_up_exps.weight`, or `blk.<il>.ffn_{up,down}_exps.weight` and record
   the live `ggml_tensor*`. We then rebind their `->data`.
3. **The file layout.** `gguf_get_tensor_offset` (public) gives each tensor's byte offset
   so we can `pread` individual expert slices.

Loading with `load_mode=LLAMA_LOAD_MODE_MMAP, use_extra_bufts=false` keeps the weights in
their native gguf layout (a repacked buffer would break the rebind). That is a public
model parameter.

Because none of this touches llama.cpp internals, the serial streaming path runs against
the unmodified upstream repository. Contrast with approaches that patch the model files:
those must be rebased on every release. Here, `git submodule update --remote` and a rebuild
is the whole upgrade.

The one place we do carry an extension is the optional `--overlap` feature. Overlapping a
token's expert reads with its expert matmuls needs a per-expert wait point *inside* the CPU
MoE kernel, which no public API exposes, so the submodule pins a fork branch adding one
readiness hook on top of the upstream commit. The hook is zero-cost when unregistered,
the serial path still builds against stock upstream, and it can be dropped when upstream
ships an equivalent callback. Details and the sunset condition are in
[seam.md § 4](seam.md).

See [seam.md](seam.md) for the exact callback contract and the ggml behaviour it relies
on.

## The generation loop

The composition root is `Session` (core/src/engine/session_*.cpp):

1. `open()` — load model (mmap on, repack off, experts on CPU); if streaming, resolve the
   architecture recipe, install the router hook, do the capture warm-up, bind the expert
   source, clear the warm-up KV. Done **once** per model.
2. `generate()` — prefill the prompt, then greedily decode `n_predict` tokens, reporting
   per-token metrics. Callable repeatedly; the expert cache stays warm between calls (see
   [session.md](session.md)). Cancellable mid-flight via the abort callback.
   `decide()` is the other way to use an open session: one prefill and no decode, reading which of
   a list of choices the model would answer (off unless the session is opened with it; see
   [decide.md](decide.md)).
3. Destructor — tear down in order: I/O pool, context, hook, model, backend.

`run()` (core/src/engine/runtime.cpp) is a thin one-shot wrapper — open, one generate, close —
so the gates and the interactive session share the same code path.

When a projector is configured, `Session` owns it beside the text model. mtmd preprocesses
ordered image/audio bytes or sampled video frames and evaluates their embeddings during prefill; ordinary text decode,
expert routing, cache policy, and token generation remain on the same text-model path. The public
session API carries byte buffers and content-part indices, not llama.cpp mtmd types.

`BMOE_BUILD_SHARED=ON` builds `libmeitte` with the C++ API and the small C ABI in `bmoe/meitte.h`.
The C ABI uses opaque session/result handles, copies request media before generation, reports errors
as owned result strings or caller-provided buffers, and does not let exceptions or llama.cpp types cross it.
Installation places the matching llama, mtmd, and ggml runtime libraries beside `libmeitte`.

The build separates the library from its frontends. `BMOE_BUILD_CLI=OFF` builds `libmeitte` and
its core tests (gates, session, ABI) without compiling `cli/`; the host tests (CLI line protocol,
HTTP server, CLI/server flag parity) are added only when `BMOE_BUILD_CLI=ON`.

Greedy sampling makes the output a deterministic function of the graph — the property the
[byte-identity gates](../tests/moe_gates.cpp) assert. That holds with the lossy knobs off. Under
[`--drop-cold-experts`](expert-dropping.md) and
[`--expert-substitute`](cache-aware-substitution.md) the hook edits routing weights or ids from
live cache state, which is not in the graph, so output becomes a function of the graph *and* the
run's history.
