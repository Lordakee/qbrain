#include "qbrain/files/image_meta.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace qbrain::files {
namespace {

constexpr size_t kNoteMax = 80;

ImageMeta unknown(std::string note) {
  if (note.size() > kNoteMax) note.resize(kNoteMax);
  ImageMeta m;
  m.format = "unknown";
  m.ok = false;
  m.note = std::move(note);
  return m;
}

const unsigned char* udata(std::string_view b) {
  return reinterpret_cast<const unsigned char*>(b.data());
}

uint16_t be16(const unsigned char* p) {
  return static_cast<uint16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
}

uint32_t be32(const unsigned char* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool has_png_signature(std::string_view b) {
  static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  return b.size() >= 8 && std::memcmp(udata(b), kSig, 8) == 0;
}

bool has_jpeg_signature(std::string_view b) {
  const unsigned char* p = udata(b);
  return b.size() >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF;
}

// ---- PNG --------------------------------------------------------------
// Walks chunks (length:4BE type:4 data:CRC:4) looking for IHDR. Never
// allocates based on declared chunk length; a chunk whose declared extent
// exceeds the buffer terminates the scan (covers oversized declared lengths).
ImageMeta parse_png(std::string_view b) {
  const unsigned char* p = udata(b);
  const size_t n = b.size();
  size_t pos = 8;  // past signature
  int chunks = 0;
  while (pos + 8 <= n) {
    if (pos > kImageMetaMaxScanBytes) return unknown("IHDR not found within bounds");
    if (++chunks > kPngMaxChunks) return unknown("chunk limit exceeded");
    const uint32_t len = be32(p + pos);
    const uint64_t total = uint64_t(len) + 12;  // len + type(4) + crc(4)
    if (total > n - pos) return unknown("chunk exceeds buffer");
    const unsigned char* type = p + pos + 4;
    if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
      if (len < 13) return unknown("IHDR too short");
      const unsigned char* d = p + pos + 8;  // chunk data
      const uint32_t w = be32(d);
      const uint32_t h = be32(d + 4);
      const unsigned depth = d[8], color = d[9], compression = d[10], filter = d[11],
                     interlace = d[12];
      if (w == 0 || h == 0) return unknown("invalid PNG dimensions");
      if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)
        return unknown("unsupported PNG bit depth");
      int comps = 0;
      switch (color) {
        case 0: comps = 1; break;  // grayscale
        case 2: comps = 3; break;  // RGB
        case 3: comps = 1; break;  // palette index (one sample per pixel)
        case 4: comps = 2; break;  // grayscale + alpha
        case 6: comps = 4; break;  // RGBA
        default: return unknown("unsupported PNG color type");
      }
      if (compression != 0 || filter != 0)
        return unknown("unsupported PNG compression or filter method");
      if (interlace > 1) return unknown("invalid PNG interlace method");
      ImageMeta m;
      m.format = "png";
      m.width = w;
      m.height = h;
      m.bit_depth = static_cast<int>(depth);
      m.components = comps;
      m.ok = true;
      return m;
    }
    pos += static_cast<size_t>(total);  // skip chunk; total <= n - pos proved above
  }
  return unknown("IHDR not found within bounds");
}

// ---- JPEG -------------------------------------------------------------
// SOI + marker walk. Standalone markers (RSTn, TEM, SOI) carry no length;
// everything else is length-prefixed and skipped; SOF0/SOF2 carry the frame
// header (precision, height, width, component count). SOS/EOI before SOF or
// running out of bounds yields unknown.
ImageMeta parse_jpeg(std::string_view b) {
  const unsigned char* p = udata(b);
  const size_t n = b.size();
  size_t pos = 2;  // past SOI
  int markers = 1;  // SOI counts toward the bound
  while (true) {
    if (pos >= n) return unknown("SOF not found within bounds");
    if (pos > kImageMetaMaxScanBytes) return unknown("SOF not found within bounds");
    if (p[pos] != 0xFF) return unknown("marker desync");
    while (pos < n && p[pos] == 0xFF) ++pos;  // fill bytes
    if (pos >= n) return unknown("truncated marker");
    const unsigned code = p[pos++];
    if (++markers > kJpegMaxMarkers) return unknown("marker limit exceeded");
    if (code == 0x01 || (code >= 0xD0 && code <= 0xD8)) continue;  // TEM/RSTn/SOI: standalone
    if (code == 0xD9 || code == 0xDA)
      return unknown("SOF not found within bounds");  // EOI/SOS before SOF
    if (pos + 2 > n) return unknown("truncated marker length");
    const uint16_t len = be16(p + pos);
    if (len < 2) return unknown("invalid marker length");
    const size_t payload = pos + 2;
    const size_t end = pos + len;  // segment end, exclusive
    if (end > n) return unknown("marker exceeds buffer");
    if (code == 0xC0 || code == 0xC2) {  // SOF0 (baseline) / SOF2 (progressive)
      if (len - 2 < 6) return unknown("SOF payload too short");
      const unsigned precision = p[payload];
      const uint16_t h = be16(p + payload + 1);
      const uint16_t w = be16(p + payload + 3);
      const unsigned ncomp = p[payload + 5];
      if (w == 0 || h == 0) return unknown("invalid JPEG dimensions");
      if (ncomp < 1 || ncomp > 4) return unknown("invalid JPEG component count");
      ImageMeta m;
      m.format = "jpeg";
      m.width = w;
      m.height = h;
      m.bit_depth = static_cast<int>(precision);
      m.components = static_cast<int>(ncomp);
      m.ok = true;
      return m;
    }
    pos = end;  // skip APPn/DQT/DHT/COM/... segment
  }
}

// ---- MIME -------------------------------------------------------------
// Lowercase ASCII extension from a hint that may be "png", ".png", or
// "photo.PNG". Empty result means no recognized extension.
std::string norm_ext(std::string_view hint) {
  if (hint.empty()) return {};
  size_t start = hint.find_last_of('.');
  if (start == std::string_view::npos) start = 0;
  else ++start;
  std::string ext;
  ext.reserve(hint.size() - start);
  for (size_t i = start; i < hint.size(); ++i) {
    char c = hint[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    ext.push_back(c);
  }
  return ext;
}

std::string mime_for_ext(const std::string& ext) {
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif") return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "bmp" || ext == "dib") return "image/bmp";
  if (ext == "webp") return "image/webp";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "ico") return "image/x-icon";
  if (ext == "tif" || ext == "tiff") return "image/tiff";
  if (ext == "txt" || ext == "text" || ext == "log") return "text/plain";
  if (ext == "md" || ext == "markdown") return "text/markdown";
  if (ext == "csv") return "text/csv";
  if (ext == "html" || ext == "htm") return "text/html";
  if (ext == "css") return "text/css";
  if (ext == "js" || ext == "mjs") return "text/javascript";
  if (ext == "json") return "application/json";
  if (ext == "xml") return "application/xml";
  if (ext == "yaml" || ext == "yml") return "application/yaml";
  if (ext == "pdf") return "application/pdf";
  if (ext == "zip") return "application/zip";
  if (ext == "gz") return "application/gzip";
  if (ext == "mp3") return "audio/mpeg";
  if (ext == "wav") return "audio/wav";
  if (ext == "mp4") return "video/mp4";
  if (ext == "mov") return "video/quicktime";
  if (ext == "avi") return "video/x-msvideo";
  return {};
}

bool is_printable_ascii(unsigned char c) {
  return (c >= 0x20 && c <= 0x7E) || c == 0x09 || c == 0x0A || c == 0x0D;
}

// Content classification over the first min(size, 16) bytes. Empty string
// means "no content classification" (fall back to extension hint).
std::string content_mime(std::string_view b) {
  const size_t n = std::min(b.size(), size_t(16));
  const unsigned char* p = udata(b);
  if (n >= 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return "image/png";
  if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return "image/jpeg";
  if (n >= 6 && (std::memcmp(p, "GIF87a", 6) == 0 || std::memcmp(p, "GIF89a", 6) == 0))
    return "image/gif";
  // BMP: "BM" + reserved fields zero at bytes 6..9 (guards plain text like "BMW ...").
  if (n >= 14 && p[0] == 'B' && p[1] == 'M' && p[6] == 0 && p[7] == 0 && p[8] == 0 && p[9] == 0)
    return "image/bmp";
  if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0)
    return "image/webp";
  if (n >= 5 && std::memcmp(p, "%PDF-", 5) == 0) return "application/pdf";
  if (n >= 1) {
    bool text = true;
    for (size_t i = 0; i < n; ++i) {
      if (!is_printable_ascii(p[i])) {
        text = false;
        break;
      }
    }
    if (text) return "text/plain";
  }
  return {};
}

}  // namespace

ImageMeta parse_image_meta(std::string_view bytes) {
  if (bytes.empty()) return unknown("empty input");
  if (bytes.size() > kImageMetaMaxInputBytes) return unknown("input exceeds 32 MiB bound");
  if (has_png_signature(bytes)) return parse_png(bytes);
  if (has_jpeg_signature(bytes)) return parse_jpeg(bytes);
  return unknown("no PNG or JPEG signature");
}

MimeResult sniff_mime(std::string_view bytes, std::string_view ext_hint) {
  MimeResult r;
  const std::string content = content_mime(bytes);
  const std::string ext = norm_ext(ext_hint);
  const std::string ext_type = mime_for_ext(ext);
  if (!content.empty()) {
    r.mime = content;
    r.content_based = true;
    r.ext_mismatch = !ext_type.empty() && ext_type != content;
  } else {
    r.mime = ext_type.empty() ? "application/octet-stream" : ext_type;
    r.content_based = false;
    r.ext_mismatch = false;
  }
  return r;
}

}  // namespace qbrain::files
