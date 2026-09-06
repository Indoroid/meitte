# Changelog

All notable changes to this project are documented here.

## [0.24.0] - unreleased

### Added

- Opt-in context growth, lossy summarization, and complete-turn trimming with rollback-safe
  overflow errors and matching CLI/server controls, including explicit `--dyn-min-ctx <=
  --ctx-size <= --dyn-max-ctx` bounds. Defaults remain off.
- Sampled video input through upstream mtmd and FFmpeg, plus a bounded Unix FFmpeg fallback for
  standalone audio formats that mtmd cannot decode directly.
- Multimodal prefill route, compute, and I/O tracing, with an optional real-model CTest fixture.
- A shared `libmeitte` build option and an opaque C ABI for FFI callers, with size-tagged
  trailing defaults, ABI/version queries, and a Python `ctypes` smoke example.
- A vendor-neutral unified-KV configuration adapter with `--kv-unified` and
  `--no-kv-unified` in both frontends, plus `meitte-server --alias NAME` for API model identity.

### Changed

- N-gram speculative decoding can follow media prefill. MTP with media fails explicitly until
  upstream supports forwarding embedding batches to the draft context.
- Row streaming accepts graph-computed contiguous `I32` indices and restores the original mmap on
  an unsupported access instead of materializing a second whole-table copy.
- Model, projector, and draft execution are CPU-only.
