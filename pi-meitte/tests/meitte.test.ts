import assert from "node:assert/strict";
import test from "node:test";
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import registerPiMeitte from "../index.ts";

test("registers a configured meitte-server without control endpoints", async (t) => {
	const requestedUrls: string[] = [];
	const originalFetch = globalThis.fetch;
	const originalBaseUrl = process.env.MEITTE_BASE_URL;
	const originalApiKey = process.env.MEITTE_API_KEY;
	process.env.MEITTE_BASE_URL = "http://meitte.test:9090/";
	process.env.MEITTE_API_KEY = "test-key";
	globalThis.fetch = async (input) => {
		requestedUrls.push(input.toString());
		return Response.json({ data: [{ id: "model.gguf", meta: { n_ctx: 4096 } }] });
	};
	t.after(() => {
		globalThis.fetch = originalFetch;
		if (originalBaseUrl === undefined) delete process.env.MEITTE_BASE_URL;
		else process.env.MEITTE_BASE_URL = originalBaseUrl;
		if (originalApiKey === undefined) delete process.env.MEITTE_API_KEY;
		else process.env.MEITTE_API_KEY = originalApiKey;
	});

	const handlers = new Map<string, (...args: any[]) => unknown>();
	const registrations: Array<{ id: string; config: any }> = [];
	const pi = {
		registerCommand() {},
		registerProvider(id: string, config: any) { registrations.push({ id, config }); },
		on(name: string, handler: (...args: any[]) => unknown) { handlers.set(name, handler); },
	} as unknown as ExtensionAPI;

	await registerPiMeitte(pi);
	assert.equal(registrations[0].id, "meitte");
	assert.equal(registrations[0].config.name, "Meitte");
	assert.equal(registrations[0].config.baseUrl, "http://meitte.test:9090/v1");
	assert.equal(registrations[0].config.apiKey, "test-key");
	assert.equal(registrations[0].config.models[0].contextWindow, 4096);
	assert.equal(registrations[0].config.models[0].compat.supportsReasoningEffort, true);
	assert.deepEqual(requestedUrls, ["http://meitte.test:9090/v1/models"]);

	const payload = { model: "model.gguf", messages: [{ role: "user", content: "hello" }], max_tokens: 1 };
	handlers.get("before_provider_request")?.({ payload }, { model: registrations[0].config.models[0] });
	assert.equal(payload.max_tokens, 1024);
});
