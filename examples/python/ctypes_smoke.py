#!/usr/bin/env python3
"""Load libmeitte with ctypes and optionally generate from a local GGUF."""

import argparse
import ctypes as ct


class Config(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_size_t),
        ("abi_version", ct.c_uint32),
        ("model_path", ct.c_char_p),
        ("projector_path", ct.c_char_p),
        ("context_size", ct.c_int32),
        ("threads", ct.c_int32),
        ("streaming", ct.c_int32),
        ("cache_mb", ct.c_int32),
        ("chat", ct.c_int32),
        ("kv_unified", ct.c_int32),
        ("speculation", ct.c_int32),
        ("context_grow", ct.c_int32),
        ("context_summarize", ct.c_int32),
        ("context_trim", ct.c_int32),
        ("context_min", ct.c_int32),
        ("context_max", ct.c_int32),
        ("video_fps", ct.c_float),
        ("video_max_frames", ct.c_int32),
        ("ffmpeg_bin_dir", ct.c_char_p),
        ("media_max_bytes", ct.c_uint64),
    ]


class Media(ct.Structure):
    _fields_ = [("data", ct.POINTER(ct.c_uint8)), ("size", ct.c_size_t), ("name", ct.c_char_p), ("kind", ct.c_int32)]


class Request(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_size_t),
        ("abi_version", ct.c_uint32),
        ("prompt", ct.c_char_p),
        ("max_tokens", ct.c_int32),
        ("clear_kv", ct.c_int32),
        ("media", ct.POINTER(Media)),
        ("media_count", ct.c_size_t),
    ]


TOKEN_CALLBACK = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_size_t, ct.c_void_p)


def bind(library):
    library.meitte_default_config.restype = Config
    library.meitte_default_request.restype = Request
    library.meitte_open.argtypes = [ct.POINTER(Config), ct.c_char_p, ct.c_size_t]
    library.meitte_open.restype = ct.c_void_p
    library.meitte_generate.argtypes = [ct.c_void_p, ct.POINTER(Request), TOKEN_CALLBACK, ct.c_void_p]
    library.meitte_generate.restype = ct.c_void_p
    library.meitte_close.argtypes = [ct.c_void_p]
    library.meitte_result_free.argtypes = [ct.c_void_p]
    library.meitte_result_ok.argtypes = [ct.c_void_p]
    library.meitte_result_ok.restype = ct.c_int
    library.meitte_result_cancelled.argtypes = [ct.c_void_p]
    library.meitte_result_cancelled.restype = ct.c_int
    library.meitte_result_error.argtypes = [ct.c_void_p]
    library.meitte_result_error.restype = ct.c_char_p


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("library", help="path to libmeitte.so or meitte.dll")
    parser.add_argument("--model", help="local GGUF path; omit to test error ownership only")
    parser.add_argument("--prompt", default="Reply with one word: ready.")
    parser.add_argument("--max-tokens", type=int, default=8)
    parser.add_argument("--cancel-after", type=int, default=0, help="cancel from the callback after N pieces")
    args = parser.parse_args()

    library = ct.CDLL(args.library)
    bind(library)
    config = library.meitte_default_config()
    error = ct.create_string_buffer(512)

    if not args.model:
        if library.meitte_open(ct.byref(config), error, len(error)):
            raise RuntimeError("expected a missing-model error")
        print(error.value.decode())
        return

    config.model_path = args.model.encode()
    session = library.meitte_open(ct.byref(config), error, len(error))
    if not session:
        raise RuntimeError(error.value.decode())
    result = None
    try:
        request = library.meitte_default_request()
        request.prompt = args.prompt.encode()
        request.max_tokens = args.max_tokens
        pieces = []

        @TOKEN_CALLBACK
        def on_token(data, size, _user):
            pieces.append(ct.string_at(data, size))
            return int(args.cancel_after and len(pieces) >= args.cancel_after)

        result = library.meitte_generate(session, ct.byref(request), on_token, None)
        if not result:
            raise RuntimeError("result allocation failed")
        cancelled = bool(library.meitte_result_cancelled(result))
        if args.cancel_after and not cancelled:
            raise RuntimeError("callback cancellation was not reported")
        if not cancelled and not library.meitte_result_ok(result):
            raise RuntimeError(library.meitte_result_error(result).decode())
        print(b"".join(pieces).decode(errors="replace"))
    finally:
        if result:
            library.meitte_result_free(result)
        library.meitte_close(session)


if __name__ == "__main__":
    main()
