// GenerateRequest::stop and GenerateRequest::reuse_kv_prefix on a tiny model.
//
// stop: the answer ends before the first stop sequence, the turn reports StopSequence, and no token
// callback ever sees a byte of the sequence. reuse_kv_prefix: a stateless request that repeats the
// previous prompt prefills only what is new, and answers bit for bit as a fresh session does.
#include "bmoe/runtime.h"
#include "bmoe/session.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char * name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

const char * k_template = R"({% for message in messages %}{{ message.role }}: {{ message.content }}
{% endfor %}{% if add_generation_prompt %}assistant: {% endif %})";

std::unique_ptr<meitte::Session> open_session(const char * model) {
    meitte::RunConfig cfg;
    cfg.model_path = model;
    cfg.n_ctx = 256;
    cfg.chatml = true;
    cfg.chat_template = k_template;
    std::string error;
    auto session = meitte::Session::open(meitte::session_config_from(cfg), error);
    if (!session) std::fprintf(stderr, "open failed: %s\n", error.c_str());
    return session;
}

meitte::GenerateRequest chat(const std::vector<meitte::ChatMessage> & messages, int n_predict) {
    meitte::GenerateRequest req;
    req.messages = messages;
    req.n_predict = n_predict;
    req.clear_kv = true;
    req.render_text = false;
    return req;
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 2) return 2;
    const char * model = argv[1];

    // ── stop sequences ──
    // The fixture's vocabulary is single bytes, so its text need not be valid UTF-8 and the parsed
    // answer may carry replacement characters. Stop sequences match the raw bytes, so the test reads
    // the raw stream (TokenMetrics::piece) and takes its stop sequence from there.
    {
        auto s = open_session(model);
        if (!s) return 1;
        const meitte::GenerateRequest base = chat({{"user", "hello there"}}, 16);
        std::string raw;
        const meitte::RunResult ref = s->generate(base, [&](const meitte::TokenMetrics & m) { raw += m.piece; });
        check(ref.ok && raw.size() >= 6, "reference answer");
        if (ref.ok && raw.size() >= 6) {
            // A stop sequence taken from the middle of the reference: the run must end at its first
            // occurrence, whichever token boundary that falls on.
            const std::string stop = raw.substr(3, 3);
            const size_t at = raw.find(stop);
            meitte::GenerateRequest req = base;
            req.stop = {"never-produced-sequence", stop};
            std::string streamed;
            const meitte::RunResult r = s->generate(req, [&](const meitte::TokenMetrics & m) { streamed += m.piece; });
            check(r.ok && r.finish == meitte::FinishReason::StopSequence, "stop sequence ends the turn");
            check(r.stop_sequence == stop, "result names the matched stop sequence");
            check(streamed == raw.substr(0, at), "the stream ends exactly before the stop sequence");
            check(r.generated_text.find(stop) == std::string::npos, "the answer does not carry the stop sequence");

            meitte::GenerateRequest none = base;
            none.stop = {"never-produced-sequence"};
            std::string unmatched;
            const meitte::RunResult n =
                s->generate(none, [&](const meitte::TokenMetrics & m) { unmatched += m.piece; });
            check(n.ok && n.generated_text == ref.generated_text && n.finish == ref.finish,
                  "an unmatched stop sequence changes nothing");
            check(raw.compare(0, unmatched.size(), unmatched) == 0, "held-back text is never altered");
        }
    }

    // ── KV prefix reuse across stateless requests ──
    {
        auto s = open_session(model);
        auto fresh = open_session(model);
        if (!s || !fresh) return 1;
        meitte::GenerateRequest first = chat({{"user", "hello there"}}, 6);
        first.reuse_kv_prefix = true;
        const meitte::RunResult r1 = s->generate(first);
        check(r1.ok, "first stateless request");

        const std::vector<meitte::ChatMessage> transcript = {
            {"user", "hello there"}, {"assistant", r1.generated_text}, {"user", "and then"}};
        meitte::GenerateRequest second = chat(transcript, 6);
        second.reuse_kv_prefix = true;
        const meitte::RunResult r2 = s->generate(second);
        const std::vector<float> reused_logits = s->copy_logits();
        check(r2.ok && r2.summary.n_prompt_reused > 0, "second request reuses the KV prefix");

        // The reference continues the same conversation through the existing clear_kv=false path, so
        // the prompt is prefilled in the same pieces: a full prefill splits it differently, and two
        // splits of one prompt need not agree to the last bit.
        meitte::GenerateRequest ref_first = chat({{"user", "hello there"}}, 6);
        const meitte::RunResult f1 = fresh->generate(ref_first);
        meitte::GenerateRequest ref_second = chat(transcript, 6);
        ref_second.clear_kv = false;
        const meitte::RunResult ref = fresh->generate(ref_second);
        check(f1.ok && f1.generated_text == r1.generated_text && ref.ok, "reference continues the conversation");
        check(r2.generated_text == ref.generated_text && reused_logits == fresh->copy_logits(),
              "reused prefix answers bit for bit as a continued conversation");
        check(r2.summary.n_prompt_reused == ref.summary.n_prompt_reused && r2.summary.n_prompt == ref.summary.n_prompt,
              "the same prefix is reused");
        const meitte::RunResult full = fresh->generate(chat(transcript, 6));
        check(full.ok && full.summary.n_prompt_reused == 0 &&
                  r2.summary.n_prompt + r2.summary.n_prompt_reused == full.summary.n_prompt,
              "reused + prefilled covers the whole prompt");

        // A request that shares nothing must not be confused by the kept KV.
        meitte::GenerateRequest other = chat({{"user", "something unrelated"}}, 6);
        other.reuse_kv_prefix = true;
        const meitte::RunResult r3 = s->generate(other);
        const meitte::RunResult ref3 = fresh->generate(chat({{"user", "something unrelated"}}, 6));
        check(r3.ok && ref3.ok && r3.generated_text == ref3.generated_text, "an unrelated request answers as fresh");
    }

    return failures ? 1 : 0;
}
