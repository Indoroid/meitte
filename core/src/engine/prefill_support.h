#pragma once

// How the engine feeds a prompt to llama.cpp and measures what that cost, for every Session entry
// point that prefills (generate, perplexity, decide). Meitte keeps the two helpers in their own
// units: batch_fill in llama_glue.h and PrefillTally in run_tally.h. This header gathers them for the
// decide() backend, so all prefill paths fill batches and attribute prefill the same way.
// Internal header: it includes llama.h.

#include "llama_glue.h"
#include "run_tally.h"
