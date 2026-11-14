"""Minimal float32/PCM WAV I/O shared by the analysis scripts."""
import struct

import numpy as np


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt, frames = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos : pos + 4], struct.unpack("<I", data[pos + 4 : pos + 8])[0]
        body = data[pos + 8 : pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            frames = body
        pos += 8 + size + (size & 1)
    tag, chans, rate, _, _, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(frames, dtype="<f4")
    elif tag == 1 and bits == 16:
        x = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    else:
        raise ValueError(f"{path}: unsupported format tag={tag} bits={bits}")
    return x.reshape(-1, chans).T.copy(), rate
