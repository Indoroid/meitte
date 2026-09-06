#include "bmoe/meitte.h"
#include "../core/src/multimodal/audio_convert.h"
#include "../core/src/multimodal/media_io.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char * name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    auto cfg = meitte_default_config();
    check(cfg.context_grow == 0 && cfg.kv_unified == 0 && cfg.video_fps == 1.0f && cfg.video_max_frames == 32,
          "C defaults keep recovery and unified KV off and bound video sampling");
    check(meitte_abi_version() == MEITTE_ABI_VERSION && meitte_version()[0], "C API reports its ABI and build version");
    meitte::RunConfig run_cfg;
    run_cfg.kv_unified = true;
    check(meitte::session_config_from(run_cfg).kv_unified, "RunConfig forwards unified KV to SessionConfig");
    check(meitte::media_kind_from_name("CLIP.MP4") == meitte::MediaKind::Video,
          "media name detects video case-insensitively");
    char error[128];
    check(!meitte_open(&cfg, error, sizeof(error)) && error[0], "missing model returns an error");
    cfg = meitte_default_config();
    cfg.struct_size = offsetof(meitte_config, model_path) + sizeof(cfg.model_path);
    check(!meitte_open(&cfg, error, sizeof(error)) && std::strstr(error, "model path"),
          "C API accepts a config prefix and defaults missing fields");
    cfg.abi_version++;
    check(!meitte_open(&cfg, error, sizeof(error)) && std::strstr(error, "ABI"), "ABI mismatch is rejected");
    auto req = meitte_default_request();
    auto * result = meitte_generate(nullptr, &req, nullptr, nullptr);
    check(result && !meitte_result_ok(result) && meitte_result_error(result)[0], "null session is rejected");
    meitte_result_free(result);
    req = meitte_default_request();
    req.prompt = "test";
    req.struct_size = offsetof(meitte_request, prompt) + sizeof(req.prompt);
    result = meitte_generate(nullptr, &req, nullptr, nullptr);
    check(result && !meitte_result_ok(result) && !std::strstr(meitte_result_error(result), "size"),
          "C API accepts a request prefix and defaults missing fields");
    meitte_result_free(result);
    meitte_cancel(nullptr);
    meitte_close(nullptr);
#ifndef _WIN32
    // A short mono PCM WAV is independent of any model or projector.
    std::vector<uint8_t> wav(44 + 160 * 2, 0);
    auto word = [&](size_t offset, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            wav[offset + i] = uint8_t(value >> (8 * i));
    };
    std::memcpy(wav.data(), "RIFF", 4);
    word(4, (uint32_t) wav.size() - 8, 4);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    word(16, 16, 4);
    word(20, 1, 2);
    word(22, 1, 2);
    word(24, 16000, 4);
    word(28, 32000, 4);
    word(32, 2, 2);
    word(34, 16, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    word(40, 320, 4);
    std::vector<float> samples;
    std::string detail;
#ifdef BMOE_TEST_FFMPEG
    check(meitte::decode_audio_ffmpeg(wav, "", 16000, 1024, {}, samples, detail) && samples.size() == 160,
          "FFmpeg decodes the PCM fixture");
    check(!meitte::decode_audio_ffmpeg(wav, "", 16000, 4, {}, samples, detail), "audio byte limit is enforced");
    check(!meitte::decode_audio_ffmpeg(
              wav, "", 16000, 1024, [] { return true; }, samples, detail),
          "audio cancellation stops the child process");
#endif
    check(!meitte::decode_audio_ffmpeg(wav, "/missing-meitte-ffmpeg", 16000, 1024, {}, samples, detail),
          "missing FFmpeg is reported");
#endif
    return failures ? 1 : 0;
}
