#!/usr/bin/env python3
"""Bridge a PulseAudio/PipeWire source to a sink (default: rig_rx -> default output).

Reads PCM frames from a capture source (the remote rig RX tunnel, `rig_rx`) and
writes them to a playback sink so you can monitor the rig on the local speakers.
Uses libpulse-simple via ctypes, so there are no pip dependencies and it talks to
pipewire-pulse exactly like pactl/parecord do.

  ./rig_monitor.py                       # rig_rx  -> default sink
  ./rig_monitor.py --source rig_rx_roc   # the ROC RX source instead
  ./rig_monitor.py --sink some_sink      # explicit output sink
  ./rig_monitor.py --rate 48000 --channels 2

The read frames pass through `process()` before playback -- insert any DSP there.
"""
import argparse
import ctypes as C
import signal
import sys

# --- libpulse-simple bindings (just the few symbols we need) ---------------
PA_STREAM_PLAYBACK = 1
PA_STREAM_RECORD = 2
PA_SAMPLE_S16LE = 3


UINT32_MAX = 0xFFFFFFFF  # (uint32_t)-1 == "let the server decide" for a buffer_attr field


class pa_sample_spec(C.Structure):
    _fields_ = [("format", C.c_int), ("rate", C.c_uint32), ("channels", C.c_uint8)]


class pa_buffer_attr(C.Structure):
    _fields_ = [("maxlength", C.c_uint32), ("tlength", C.c_uint32),
                ("prebuf", C.c_uint32), ("minreq", C.c_uint32), ("fragsize", C.c_uint32)]


def _load():
    pa = C.CDLL("libpulse-simple.so.0")
    pa.pa_simple_new.restype = C.c_void_p
    pa.pa_simple_new.argtypes = [
        C.c_char_p, C.c_char_p, C.c_int, C.c_char_p, C.c_char_p,
        C.POINTER(pa_sample_spec), C.c_void_p, C.POINTER(pa_buffer_attr), C.POINTER(C.c_int),
    ]
    pa.pa_simple_read.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_int)]
    pa.pa_simple_write.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_int)]
    pa.pa_simple_drain.argtypes = [C.c_void_p, C.POINTER(C.c_int)]
    pa.pa_simple_free.argtypes = [C.c_void_p]
    pa.pa_strerror.restype = C.c_char_p
    pa.pa_strerror.argtypes = [C.c_int]
    return pa


def _new(pa, direction, dev, ss, name, attr):
    err = C.c_int(0)
    dev_c = dev.encode() if dev else None
    s = pa.pa_simple_new(None, b"rig_monitor", direction, dev_c, name,
                         C.byref(ss), None, C.byref(attr), C.byref(err))
    if not s:
        raise RuntimeError("pa_simple_new(%s): %s" %
                           (dev or "default", pa.pa_strerror(err.value).decode()))
    return s


def process(buf: bytes) -> bytes:
    """Hook for optional DSP; identity passthrough by default."""
    return buf


def main():
    ap = argparse.ArgumentParser(description="Bridge a Pulse source to a sink.")
    ap.add_argument("--source", default="rig_rx", help="capture source (default: rig_rx)")
    ap.add_argument("--sink", default=None, help="playback sink (default: server default)")
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--channels", type=int, default=2)
    ap.add_argument("--frame-ms", type=int, default=20, help="read/write chunk size")
    ap.add_argument("--latency-ms", type=int, default=80,
                    help="target buffer latency; without this the Pulse default is ~2 s")
    args = ap.parse_args()

    # Belt-and-suspenders: also nudge libpulse's own latency target down, in case the
    # buffer_attr alone isn't honored without ADJUST_LATENCY on a given server.
    import os
    os.environ.setdefault("PULSE_LATENCY_MSEC", str(args.latency_ms))

    pa = _load()
    ss = pa_sample_spec(PA_SAMPLE_S16LE, args.rate, args.channels)
    bytes_per_frame = args.channels * 2  # S16 = 2 bytes/sample
    chunk = (args.rate * args.frame_ms // 1000) * bytes_per_frame  # read/write chunk (bytes)
    target = (args.rate * args.latency_ms // 1000) * bytes_per_frame  # buffer target (bytes)

    # Request a small buffer instead of the ~2 s pulse-compat default (node.latency
    # 88200/44100). fragsize bounds the record buffer; tlength the playback buffer.
    rec_attr = pa_buffer_attr(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, target)
    play_attr = pa_buffer_attr(UINT32_MAX, target, UINT32_MAX, UINT32_MAX, UINT32_MAX)

    rec = _new(pa, PA_STREAM_RECORD, args.source, ss, b"rig RX monitor", rec_attr)
    play = _new(pa, PA_STREAM_PLAYBACK, args.sink, ss, b"rig RX monitor", play_attr)

    running = [True]
    signal.signal(signal.SIGINT, lambda *_: running.__setitem__(0, False))
    signal.signal(signal.SIGTERM, lambda *_: running.__setitem__(0, False))

    print("monitoring %s -> %s  (%d Hz, %dch, %d ms frames). Ctrl-C to stop." %
          (args.source, args.sink or "default sink", args.rate, args.channels,
           args.frame_ms), file=sys.stderr)

    buf = (C.c_char * chunk)()
    err = C.c_int(0)
    try:
        while running[0]:
            if pa.pa_simple_read(rec, buf, chunk, C.byref(err)) < 0:
                raise RuntimeError("read: " + pa.pa_strerror(err.value).decode())
            out = process(bytes(buf))
            if pa.pa_simple_write(play, out, len(out), C.byref(err)) < 0:
                raise RuntimeError("write: " + pa.pa_strerror(err.value).decode())
    finally:
        pa.pa_simple_drain(play, C.byref(err))
        pa.pa_simple_free(play)
        pa.pa_simple_free(rec)
    return 0


if __name__ == "__main__":
    sys.exit(main())
