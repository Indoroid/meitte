#include "bmoe/decode_trace.h"
#include "bmoe/route_trace.h"
#include "bmoe/session.h"
#include "media_io.h"

#include <cstdio>

namespace {
class ComputeSink final : public meitte::IComputeTraceSink {
public:
    explicit ComputeSink(uint8_t media_kind) : media_kind_(media_kind) {}
    void on_static(const meitte::DecodeTraceStatic &) override {}
    void on_rows(const meitte::ComputeTraceRow * rows, size_t n) override {
        for (size_t i = 0; i < n; ++i)
            if (rows[i].phase == 0 && rows[i].step >= 0) {
                ++prefill_rows;
                if (rows[i].media_kind == media_kind_) ++media_rows;
            }
    }
    size_t prefill_rows = 0;
    size_t media_rows = 0;

private:
    uint8_t media_kind_;
};

class RouteSink final : public meitte::IRouteTraceSink {
public:
    explicit RouteSink(uint8_t media_kind) : media_kind_(media_kind) {}
    void on_static(const meitte::RouteTraceStatic &) override {}
    void on_rows(const meitte::RouteTraceRow * rows, size_t n) override {
        for (size_t i = 0; i < n; ++i)
            if (rows[i].phase == 0 && rows[i].step >= 0 && rows[i].media_kind == media_kind_) ++media_rows;
    }
    size_t media_rows = 0;

private:
    uint8_t media_kind_;
};

class IoSink final : public meitte::IIoTraceSink {
public:
    explicit IoSink(uint8_t media_kind) : media_kind_(media_kind) {}
    void on_static(const meitte::DecodeTraceStatic &) override {}
    void on_rows(const meitte::IoTraceRow * rows, size_t n) override {
        for (size_t i = 0; i < n; ++i)
            if (rows[i].phase == 0 && rows[i].step >= 0 && rows[i].media_kind == media_kind_) ++media_rows;
    }
    size_t media_rows = 0;

private:
    uint8_t media_kind_;
};
} // namespace

int main(int argc, char ** argv) {
    if (argc != 5) return 2;
    meitte::MediaInput media;
    std::string error;
    if (!meitte::load_media_file(argv[3], 64ull * 1024 * 1024, media, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    meitte::RunConfig config;
    config.model_path = argv[1];
    config.multimodal.mmproj_path = argv[2];
    config.moe.enabled = std::string(argv[4]) == "ON";
    config.chatml = true;
    config.n_ctx = 4096;
    config.n_batch = 128;
    config.compute_trace_layers = true;
    const uint8_t media_kind = static_cast<uint8_t>(media.kind);
    ComputeSink compute(media_kind);
    RouteSink route(media_kind);
    IoSink io(media_kind);
    auto session = meitte::Session::open(meitte::session_config_from(config), error, &route, &compute, &io);
    if (!session) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    meitte::GenerateRequest request;
    request.prompt = "Describe the media briefly.";
    request.media.push_back(std::move(media));
    request.n_predict = 1;
    const auto result = session->generate(request);
    if (!result.ok || compute.prefill_rows == 0 || compute.media_rows == 0 ||
        result.summary.media_projector_seconds <= 0) {
        std::fprintf(stderr, "%s\n", result.error.c_str());
        return 1;
    }
    if (config.moe.enabled && (route.media_rows == 0 || io.media_rows == 0)) return 1;
    return 0;
}
