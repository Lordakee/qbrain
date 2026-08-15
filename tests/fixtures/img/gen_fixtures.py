#!/usr/bin/env python3
"""N33-A D5: generate tests/fixtures/img fixtures + ground-truth JSONs.

Provenance script for the binary fixtures committed next to it. Run from the
repo root (or this directory) with Python 3.8+:

    python tests/fixtures/img/gen_fixtures.py

All fixtures are constructed programmatically (minimal valid headers; no
embedded third-party files). The oversized-bound fixture uses a declared PNG
chunk length of 0x03000000 (48 MiB > 32 MiB) instead of a large real file, so
the real bounds logic is exercised cheaply (per N33-A dispatch).
"""

import json
import pathlib
import struct
import zlib

HERE = pathlib.Path(__file__).resolve().parent

PNG_SIG = b"\x89PNG\r\n\x1a\n"
SOI = b"\xff\xd8"
EOI = b"\xff\xd9"


def png_chunk(ctype: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)


def build_png(width: int, height: int, depth: int, color: int, raw: bytes) -> bytes:
    ihdr = struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0)
    return (
        PNG_SIG
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )


def jpeg_seg(code: int, payload: bytes) -> bytes:
    return b"\xff" + bytes([code]) + struct.pack(">H", len(payload) + 2) + payload


APP0_JFIF = jpeg_seg(0xE0, b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00")  # 16-byte segment
DQT = jpeg_seg(0xDB, b"\x00" + bytes(range(64)))  # 8-bit table 0
DHT = jpeg_seg(0xC4, b"\x00" + bytes([1] + [0] * 15) + b"\x01")  # DC table 0, one 1-bit code
SOS = jpeg_seg(0xDA, b"\x01\x01\x00\x02\x00\x03\x00" + b"\x00\x3F\x00")


def sof(code: int, height: int, width: int, ncomp: int) -> bytes:
    payload = struct.pack(">BHHB", 8, height, width, ncomp)
    for i in range(1, ncomp + 1):
        payload += bytes([i, 0x11, 0x00])
    return jpeg_seg(code, payload)


# ---- valid -------------------------------------------------------------

png_gray = build_png(1, 1, 8, 0, b"\x00\x80")  # 1x1 8-bit grayscale
raw_rgba = b"".join(b"\x00" + bytes([(x * 30 + y * 40 + c * 60) & 0xFF
                                     for x in range(7) for c in range(4)])
                    for y in range(5))
png_rgba = build_png(7, 5, 8, 6, raw_rgba)  # 7x5 8-bit RGBA

jpg_sof0 = (
    SOI + APP0_JFIF + DQT + sof(0xC0, 240, 320, 3) + DHT + SOS + b"\x00" * 8 + EOI
)  # baseline 320x240 RGB
jpg_sof2 = (
    SOI
    + jpeg_seg(0xE1, b"Exif\x00\x00" + b"\x00" * 8)  # APP1 placeholder
    + sof(0xC2, 48, 64, 1)  # progressive 64x48 grayscale
    + EOI
)

# ---- malformed / bound-exercising --------------------------------------

trunc_png = PNG_SIG + struct.pack(">I", 13) + b"IHDR" + b"\x00\x00\x00\x01\x00"
trunc_jpg = SOI + APP0_JFIF + b"\xff\xc0" + struct.pack(">H", 17) + b"\x08\x30"
png_sig_only = PNG_SIG
jpg_desync = SOI + APP0_JFIF + b"hello desync - no marker here"
png_1005_chunks = (
    PNG_SIG + png_chunk(b"unKn", b"") * 1005 + build_png(1, 1, 8, 0, b"\x00\x80")[8:]
)
jpg_105_markers = SOI + APP0_JFIF * 105 + sof(0xC0, 240, 320, 3) + EOI
png_huge_chunk_len = PNG_SIG + struct.pack(">I", 0x03000000) + b"IDAT" + b"\x00" * 8


def gt_meta(fmt, w, h, d, c, ok, note):
    return {"format": fmt, "width": w, "height": h, "bit_depth": d,
            "components": c, "ok": ok, "note": note}


def gt_mime(mime, cb, mm):
    return {"mime": mime, "content_based": cb, "ext_mismatch": mm}


FIXTURES = [
    # file, bytes, ext_hint, description, meta, mime
    ("valid_png_gray_1x1.png", png_gray, ".png",
     "minimal valid 1x1 8-bit grayscale PNG (real zlib IDAT)",
     gt_meta("png", 1, 1, 8, 1, True, ""), gt_mime("image/png", True, False)),
    ("valid_png_rgba_7x5.png", png_rgba, ".png",
     "minimal valid 7x5 8-bit RGBA PNG (real zlib IDAT)",
     gt_meta("png", 7, 5, 8, 4, True, ""), gt_mime("image/png", True, False)),
    ("valid_jpg_sof0_320x240.jpg", jpg_sof0, ".jpg",
     "structurally complete baseline JPEG: SOI APP0 DQT SOF0 DHT SOS EOI",
     gt_meta("jpeg", 320, 240, 8, 3, True, ""), gt_mime("image/jpeg", True, False)),
    ("valid_jpg_sof2_64x48.jpg", jpg_sof2, ".jpg",
     "progressive JPEG with APP1 skip: SOI APP1 SOF2 EOI",
     gt_meta("jpeg", 64, 48, 8, 1, True, ""), gt_mime("image/jpeg", True, False)),
    ("spoof_png_as.jpg", png_gray, ".jpg",
     "spoofed extension: PNG bytes declared as .jpg - content wins",
     gt_meta("png", 1, 1, 8, 1, True, ""), gt_mime("image/png", True, True)),
    ("spoof_jpg_as.png", jpg_sof0, ".png",
     "spoofed extension: JPEG bytes declared as .png - content wins",
     gt_meta("jpeg", 320, 240, 8, 3, True, ""), gt_mime("image/jpeg", True, True)),
    ("spoof_text_as.png", b"qbrain n33 text fixture - not an image\n", ".png",
     "spoofed extension: plain text declared as .png - text/plain + mismatch",
     gt_meta("unknown", 0, 0, 0, 0, False, "no PNG or JPEG signature"),
     gt_mime("text/plain", True, True)),
    ("trunc_png_ihdr.png", trunc_png, ".png",
     "truncated PNG: IHDR declares 13 data bytes, only 5 present",
     gt_meta("unknown", 0, 0, 0, 0, False, "chunk exceeds buffer"),
     gt_mime("image/png", True, False)),
    ("trunc_jpg_sof.jpg", trunc_jpg, ".jpg",
     "truncated JPEG: SOF0 declares a 17-byte segment, only 2 payload bytes",
     gt_meta("unknown", 0, 0, 0, 0, False, "marker exceeds buffer"),
     gt_mime("image/jpeg", True, False)),
    ("malformed_png_sig_only.png", png_sig_only, ".png",
     "malformed PNG: bare signature, no chunks",
     gt_meta("unknown", 0, 0, 0, 0, False, "IHDR not found within bounds"),
     gt_mime("image/png", True, False)),
    ("malformed_jpg_desync.jpg", jpg_desync, ".jpg",
     "malformed JPEG: after APP0 the stream desyncs (no 0xFF marker byte)",
     gt_meta("unknown", 0, 0, 0, 0, False, "marker desync"),
     gt_mime("image/jpeg", True, False)),
    ("bounds_png_chunk_1005.png", png_1005_chunks, ".png",
     "bound: 1005 empty chunks before IHDR - PNG chunk limit is 1000",
     gt_meta("unknown", 0, 0, 0, 0, False, "chunk limit exceeded"),
     gt_mime("image/png", True, False)),
    ("bounds_jpg_marker_105.jpg", jpg_105_markers, ".jpg",
     "bound: 105 APP0 markers before SOF0 - JPEG marker limit is 100",
     gt_meta("unknown", 0, 0, 0, 0, False, "marker limit exceeded"),
     gt_mime("image/jpeg", True, False)),
    ("bounds_png_huge_chunk_len.png", png_huge_chunk_len, ".png",
     "oversized bound: first chunk declares 0x03000000 bytes (48 MiB > 32 MiB)",
     gt_meta("unknown", 0, 0, 0, 0, False, "chunk exceeds buffer"),
     gt_mime("image/png", True, False)),
]


def main() -> None:
    for name, data, ext_hint, desc, meta, mime in FIXTURES:
        HERE.joinpath(name).write_bytes(data)
        doc = {
            "file": name,
            "ext_hint": ext_hint,
            "description": desc,
            "expect": {"meta": meta, "mime": mime},
        }
        HERE.joinpath(name + ".json").write_text(
            json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n"
        )
        print(f"wrote {name} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
