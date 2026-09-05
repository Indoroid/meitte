# pi-meitte

`pi-meitte` registers a locally running `meitte-server` as the **Meitte** provider in
pi-coding-agent. It discovers models from `http://127.0.0.1:8080/v1/models` and sends requests to
the server's OpenAI-compatible chat-completions endpoint.

## Configuration

The extension follows the current `pi-llama` pattern for endpoint configuration, with Meitte-specific
environment variable names:

- `MEITTE_BASE_URL`: server root or OpenAI API root, default `http://127.0.0.1:8080/v1`.
- `MEITTE_API_KEY`: API key passed to Pi, default `local`.

`meitte-server` does not require authentication today; the API-key setting makes the extension ready
for a reverse proxy that does.

Start the server first:

```bash
build/cli/meitte-server -m /path/to/model.gguf --moe-stream --host 127.0.0.1 --port 8080
```

Load the extension from a checkout:

```bash
pi -e ./pi-meitte/index.ts
```

Use `/model` in Pi to refresh the server's model list. The `meitte-version` command verifies that
the local server is reachable.

Run the extension tests with:

```bash
cd pi-meitte
npm install
npm test
```
