# Multimodal input

Driftwood can combine a text GGUF with a llama.cpp mtmd projector. The projector handles image or
audio preprocessing and prefill; token generation and MoE expert streaming continue through the
normal text-model path.

## meitte-cli

```bash
build/cli/meitte-cli -m model.gguf --mmproj mmproj.gguf \
  --image photo.png -p "Describe this image."
```

- `--image PATH`, `--audio PATH`, and the format-detecting `--media PATH` alias are repeatable.
- `--mmproj-offload` is the default; `--no-mmproj-offload` keeps projector work on CPU.
- `--image-min-tokens` and `--image-max-tokens` override dynamic image-token bounds; `-1`
  retains projector metadata.
- `--mtmd-batch-max-tokens N` limits projector output per prefill batch.
- File-backed media is one-shot only. `--session` rejects these flags rather than ignoring them.

## meitte-server

```bash
build/cli/meitte-server -m model.gguf --mmproj mmproj.gguf --port 8080
```

`POST /v1/chat/completions` accepts ordered OpenAI content parts:

```json
{
  "model": "model.gguf",
  "messages": [{
    "role": "user",
    "content": [
      {"type": "text", "text": "Describe: "},
      {"type": "image_url", "image_url": {"url": "data:image/png;base64,..."}}
    ]
  }]
}
```

Audio uses `{"type":"input_audio","input_audio":{"data":"...","format":"wav"}}`. Image URLs must
be base64 `data:` URLs: the local server never fetches remote resources. A decoded media item is
limited to 32 MiB, all decoded media in one request to 64 MiB, and the HTTP body defaults to a
64 MiB cap configurable with `--max-request-mb`.

## Current boundaries

- A request containing media starts from a clear KV cache. It can then be continued with text-only
  turns when the caller preserves the KV; a continuation cannot add or replace media, so reset with
  `clear_kv: true` before sending another image or audio input.
- `meitte-server --kv-preserve` retains one incremental conversation. After the image request, send
  only the next user message to `POST /v1/chat/completions`; `{"clear_kv":true}` begins a new
  conversation. This state is server-wide, not isolated per HTTP client.
- MTP and n-gram speculation are rejected for media requests.
- Route, compute, and I/O tracing are rejected during multimodal prefill.
- Video input is not built.

These combinations fail with explicit errors. Text-only requests remain available while the
projector is loaded.
