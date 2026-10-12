# BigMoeOnEdge sync ledger

This ledger records the BigMoeOnEdge delta ported after Meitte's upstream sync (`d2961e9`,
2026-08-31). The comparison target is BigMoeOnEdge `v0.24.0` (`74ba18f`). The Android `examples/`
tree and both repositories' `third_party/` trees are intentionally outside this port.

## Core behavior

- `MoeStreamConfig::release_mmap` adds an opt-in mapping-release policy. It defaults to false.
- `mapping_release` locates model-file mappings, releases their views and Windows section handles,
  and holds inert placeholders until the model is destroyed.
- `Session::open` releases only after captured weight addresses no longer use those mappings. MTP
  sessions also require every GGUF tensor to belong to a captured policy.
- `FileReader::reopen`, `ExpertStreamSource::reopen_readers`, and `RowStream::reopen_readers` reopen
  every decode reader after release. This is required for Windows lanes opened under a live section.
- Capture now retains every distinct weight object. Dense placement groups objects by GGUF range and
  rebinds all aliases to one buffer. This fixes synthesized tied output heads even when mapping
  release is disabled.

## Adapters

- `meitte-cli --release-mmap` exposes the core policy for one-shot and session CLI use.
- `meitte-server --release-mmap` exposes the same startup policy for HTTP inference.
- `meitte_config.release_mmap` appends the option to the size-versioned C ABI. Older callers with a
  shorter structure keep the false default. C++ callers set `RunConfig::moe.release_mmap` directly.

## Tools, tests, and documentation

- `bmoe-iobench` adds `--mmap`, `--reopen-lanes`, `--range-mb`, and `--fresh` to isolate mapping,
  reader-open timing, locality, and fresh-page costs.
- `scripts/bench-report.sh` follows model symlinks, totals split GGUF shards, uses the bytes that
  `dd` actually read, and rejects implausible cached results instead of reporting them as storage.
- `mapping_release_test` checks release, placeholder lifetime, idempotence, and model-like late
  cleanup without loading llama.cpp or a model.
- The architecture, streaming, roadmap, community benchmark, and README pages describe the new
  behavior. The original measurements are preserved under
  `docs/bench-data/2026-08-29-mmap-serialisation/`.

The upstream README wording change that removes a blanket "not as a fork" claim was already true in
Meitte's wording: Meitte builds on llama.cpp and documents its optional overlap fork separately.
BigMoeOnEdge's release changelog is represented by this ledger because `CHANGELOG.md` was already
deleted in the Meitte working tree and that user-owned deletion was preserved.

## BigMoeOnEdge 0.25–0.28 and the planner branches

The second port takes BigMoeOnEdge `main` at `374f562` (0.28.0, 2026-09-29) plus the open branches
that build on it: `feat/prefill-plan`, `feat/prefill-slots`, `feat/prefill-memory-turns`,
`feat/isolated-probes` and `feat/auto-planner` (tips `4271e3b`, `9b00258`, `c018e21`, `fb9737c`,
`772b190`, all based on `a63af95`). The histories are unrelated, so this is a three-way file merge
against `v0.24.0`, not a git merge.

- **Taken whole:** the decide policy (`core/src/engine/decide/`), the prefill device
  (`prefill_device`, `prefill_path`, `moe/device_arena`), the decide probe, the parameter table
  (`params.h`, `params.cpp`), and the planner (`core/src/plan/`, its profile, placement, plan and
  probe headers). The namespace is `meitte`. The planner, parameter, decide and config tests come
  with them.
- **Merged into Meitte's own units:** upstream's monolithic `session.cpp` hunks went to
  `session_impl.h` (the prefill path and the decide state), `session_open.cpp` (device selection,
  `n_gpu_layers`, lazy mode, planner overrides appended after `--override-tensor`, the prefill path
  open and best-effort fallback), `session_generate.cpp` (placed prefill pieces and device
  telemetry), `session_perplexity.cpp` (placed scoring) and the new `session_decide.cpp`. Every full
  clear of the target context goes through `Impl::clear_memory()`. `prefill_support.h` forwards to
  Meitte's `llama_glue.h` and `run_tally.h` instead of duplicating them.
- **Frontends:** the planner's probe order moved from upstream's `bmoe-cli` into the core
  (`bmoe/auto_plan.h`), and the planner and table flags into `cli/frontend_util`, so `meitte-cli` and
  `meitte-server` parse and plan identically. Both fall back to the parameter table for flags they do
  not parse by hand.
- **Not taken:** `feat/ios-harness` (iOS is out of scope), upstream's `bmoe-server` binary (its HTTP
  surface goes into `meitte-server` instead), the macOS Metal build default, and the build-output
  move to `build/bin`. `perf/odirect-zero-copy` predates 0.24 and was ported in its own change: a three-way merge
  against its merge base `0375318`, with its flag added as a parameter-table row.
- **Meitte-only refusals:** the prefill device is refused with multimodal input and with dynamic
  context policies until each has a gate (`docs/limitations.md`).
- **Gate numbering:** upstream's G16–G18 are the prefill device and decide. Meitte's earlier
  `--dense-weights ahwb` gate is now G19.
