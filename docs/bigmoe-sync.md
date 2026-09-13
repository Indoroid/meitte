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
