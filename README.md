# Meitte

Meitte runs large Mixture-of-Experts (MoE) GGUF models locally when keeping every expert in RAM is
impractical. It builds on llama.cpp and streams the expert weights selected for each token from
local storage, while keeping the model's native GGUF layout intact.

Meitte provides both a command-line runner and an OpenAI-compatible local server. It also supports
multimodal models, persistent conversations, KV-cache configuration, custom chat templates, and
runtime telemetry.

## What It Solves

An MoE model may contain far more parameters than it uses for any one token. Conventional loading
still requires the full expert set to reside in memory. That is a poor fit for local systems with
limited RAM, even when their storage is fast enough to supply the few selected experts on demand.

Meitte treats local storage as another level of the memory hierarchy. It is intended for CPU-first
local inference where an MoE model exceeds available RAM but its active experts can be read and
reused efficiently.

This is not a way to make every model fast on every device. If the whole model fits comfortably in
RAM, normal llama.cpp loading is usually simpler. Meitte is useful when expert residency is the
constraint and storage latency is an acceptable tradeoff.

## How It Works

1. The model is loaded through llama.cpp's public APIs with its native GGUF tensor layout.
2. Meitte discovers the expert tensor offsets and captures the live expert tensors during prompt
   evaluation.
3. When llama.cpp's router selects experts for a token, Meitte reads only those expert slices from
   storage into a bounded cache and rebinds the matching tensor data.
4. Frequently used experts remain cached; dense weights use the configured mmap, warm, or anonymous
   residency policy.
5. llama.cpp evaluates the resulting graph normally, so model loading, tokenization, sampling, and
   model architecture support stay on the upstream path.

The streaming path does not repack GGUF files or invent a second model format. Repacking increases
storage and breaks the native-layout invariant that makes direct expert reads possible. Tensor
overrides, when used, are limited to fully resident tensors rather than the streamed expert path.

## Capabilities

- `bmoe-cli` for local interactive and one-shot inference.
- `bmoe-server` for OpenAI-compatible completions and chat-completions endpoints.
- MoE expert streaming with direct I/O where the platform supports it, bounded expert caching, and
  optional cache-aware routing.
- Multimodal prompt support with an `--mmproj` projector for supported models.
- Persistent server sessions, custom Jinja chat templates, and configurable KV-cache types.
- Reasoning controls, reasoning budgets, and preserved reasoning/KV state where the selected model
  and template support them.
- Per-token progress, CSV metrics, route traces, and compute diagnostics for measuring the I/O and
  cache tradeoffs instead of guessing at them.

## Quick Start

Initialize the llama.cpp submodule and build the host binaries:

```bash
git submodule update --init --recursive
scripts/build-host.sh
```

Run a streamed model from the CLI:

```bash
build/cli/bmoe-cli \
  -m /path/to/model.gguf \
  --moe-stream --cache-mb auto \
  --chatml -p "Explain what a mixture-of-experts model is."
```

Start an OpenAI-compatible server:

```bash
build/cli/bmoe-server \
  -m /path/to/model.gguf \
  --moe-stream --cache-mb auto \
  --host 127.0.0.1 --port 8080
```

For a supported multimodal model, supply its projector:

```bash
build/cli/bmoe-server \
  -m /path/to/model.gguf \
  --mmproj /path/to/mmproj.gguf \
  --moe-stream --cache-mb auto --port 8080
```

The model architecture, available storage bandwidth, dense-weight policy, context size, and expert
cache budget all affect results. Start with `--cache-mb auto`, collect telemetry with `--progress`,
then tune from measured cache hit rate and I/O time.

## Build And Test

```bash
scripts/build-host.sh
cd build
ctest --output-on-failure
```

The `bmoe_moe_gates` tests verify that streamed experts produce byte-identical results to resident
experts. Run them after changing the streamer, the llama.cpp seam, or model recipes.

## Documentation

- [Architecture](docs/architecture.md): component boundaries and the loading/evaluation path.
- [llama.cpp seam](docs/seam.md): the public API boundary and supported integration points.
- [Session behavior](docs/session.md): persistent context, templates, KV cache, and reasoning.
- [Multimodal usage](docs/multimodal.md): projector setup and request formats.
- [Telemetry](docs/telemetry.md): progress output, CSV fields, and diagnostic traces.
- [Adding a model](docs/adding-a-model.md): architecture recipes and validation expectations.
- [Limitations](docs/limitations.md): workload and platform constraints.

## Credits

Meitte is built on [BigMoeOnEdge](https://github.com/Helldez/BigMoeOnEdge) by
[Helldez](https://github.com/Helldez). The original project established the expert-streaming design
and llama.cpp integration that this project preserves.

Meitte also relies on [llama.cpp](https://github.com/ggml-org/llama.cpp), GGUF, and the research and
engineering work behind storage-backed LLM inference, including AirLLM, LLM in a Flash, FlexGen,
PowerInfer, EdgeMoE, and flash-moe.

## License

Apache-2.0. See [LICENSE](LICENSE).
