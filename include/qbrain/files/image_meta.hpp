#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace qbrain::files {

// N33 D1: header-level image metadata (PNG/JPEG only, no pixel decoding,
// no external dependencies, no filesystem access in the parser itself).

// Hard bounds (N33-PLAN D1 / security notes). All bounds are enforced inside
// parse_image_meta; callers additionally enforce the input bound before
// attempting metadata extraction (P1-1 enforcement point is the handler).
inline constexpr uint64_t kImageMetaMaxInputBytes = 32ull * 1024 * 1024;  // 32 MiB
inline constexpr uint64_t kImageMetaMaxScanBytes = 64ull * 1024;          // 64 KiB header scan
inline constexpr int kPngMaxChunks = 1000;   // PNG chunks scanned while looking for IHDR
inline constexpr int kJpegMaxMarkers = 100;  // JPEG markers scanned (incl. SOI) while looking for SOF

// Parsed image metadata. `ok` is true only when the format was identified and
// width/height were extracted; malformed/truncated/over-limit input yields
// format "unknown", ok=false, and a bounded reason note. Parsing never
// throws and never fails ingestion.
struct ImageMeta {
  std::string format;    // "png", "jpeg", or "unknown"
  int64_t width = 0;     // pixels, 0 when unknown
  int64_t height = 0;    // pixels, 0 when unknown
  int bit_depth = 0;     // PNG: IHDR bit depth; JPEG: SOF sample precision
  int components = 0;    // PNG: samples/pixel by color type; JPEG: SOF component count
  bool ok = false;       // true when format + dimensions were extracted
  std::string note;      // bounded (<= 80 chars) reason annotation when !ok
};

// N33 D2: content-based MIME detection over the first 16 bytes with
// declared-extension cross-check. When content magic identifies the type,
// `mime` follows the content (never the extension) and `ext_mismatch` is set
// when a recognized extension hint contradicts the content-derived type.
struct MimeResult {
  std::string mime;             // final MIME type, never empty
  bool content_based = false;   // true when derived from magic bytes
  bool ext_mismatch = false;    // declared extension contradicts content type
};

// Parse PNG (magic + IHDR: width/height/bit depth/color type) or JPEG
// (SOI + APPn skip + SOF0/SOF2 frame: height/width/precision/components).
// Untrusted input: bounded scan, no allocation proportional to declared
// chunk/marker lengths, no pixel decoding. ext_hint is ignored here.
ImageMeta parse_image_meta(std::string_view bytes);

// Content MIME sniff. ext_hint accepts a bare extension ("png"), a dotted
// extension (".png"), or a filename ("photo.PNG"); matching is
// case-insensitive. Unknown extension hints contribute nothing.
MimeResult sniff_mime(std::string_view bytes, std::string_view ext_hint);

}  // namespace qbrain::files
