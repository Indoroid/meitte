// Session::decide(): choose from a list with one prefill and no decode (bmoe/decide.h). The policy
// lives in decide/ and is tested without a model; this unit only hands it the live session.
#include "session_impl.h"
#include "decide/decider.h"
#include "decide/llama_backend.h"

#include <string>
#include <vector>

namespace meitte {

DecideResult Session::decide(const DecideRequest & req) {
    Impl & im = *impl_;
    if (!im.cfg.decide.enabled) {
        DecideResult off;
        off.error = "decide is off for this session (open it with decide enabled)";
        return off;
    }
    im.cancel_requested.store(false, std::memory_order_relaxed);

    // A decision is not a conversation turn. The backend overwrites the context, so the records of
    // what generate() was continuing go now; a later turn must rebuild instead of diffing against
    // tokens the context no longer holds. The memory itself is the backend's to clear or restore.
    im.chat_history.clear();
    im.kv_tokens.clear();
    im.kv_n_past = 0;
    im.kv_last_generation_start = 0;
    im.kv_has_media = false;
    im.retained_media.clear();

    detail::LlamaDecideDeps deps;
    deps.ctx = im.ctx.get();
    deps.ctx_dft = im.ctx_dft.get();
    deps.vocab = im.vocab;
    deps.n_vocab = im.n_vocab;
    deps.n_ctx = im.cfg.n_ctx;
    // With a prefill device each piece is one graph whose width we choose, as in generate().
    deps.n_batch = im.prefill ? detail::prefill_piece(im.cfg) : im.cfg.n_batch;
    deps.hook = im.hook.get();
    deps.source = &im.source;
    deps.moe_on = im.cfg.moe.enabled;
    deps.tmpls = im.chat_on ? im.chat_tmpls.get() : nullptr;
    deps.think_ctl = im.think_ctl;
    deps.prefill = im.prefill.get();
    deps.prefill_min_tokens = im.cfg.prefill.min_tokens;
    detail::LlamaDecideBackend backend(deps);

    if (!im.decide_cache_chosen) {
        // No kept state with a prefill device: llama.cpp would save and restore it through KV views
        // that still point where the state was before PrefillPath moved it (validation refuses `on`).
        im.decide_cache = detail::make_prefix_cache(im.prefill ? PrefixCacheMode::Off : im.cfg.decide.prefix_cache,
                                                    backend.prefill_cost_scales_with_tokens());
        im.decide_cache_chosen = true;
    }
    if (im.decide_probe) {
        std::vector<int32_t> ids;
        for (const std::string & c : req.choices) {
            const std::vector<detail::Token> t = backend.tokenize_plain(c);
            ids.push_back(t.empty() ? -1 : t[0]);
        }
        im.decide_probe->begin(ids);
    }
    DecideResult r = detail::run_decide(backend, im.decide_cache.get(), req);
    if (im.decide_probe) {
        im.decide_probe->end();
        if (r.ok)
            im.decide_probe->write(im.decide_seq, r.n_tokens, r.prefill.seconds, req.choices, r.choice_logp, r.best);
        ++im.decide_seq;
    }
    // The abort callback turns a cancel into a failed decode; that is a request honoured, not a
    // context gone bad, and run_decide has already emptied the sequence.
    if (!r.ok && r.fatal && im.cancel_requested.load(std::memory_order_relaxed)) {
        r.fatal = false;
        r.cancelled = true;
        r.error = "cancelled";
    }
    return r;
}

} // namespace meitte
