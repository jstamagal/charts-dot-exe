// png.cpp -- 4-bit indexed PNG with a small deflate, so there is no zlib to
// link.  Charts are flat colour and dither, which LZ77 with fixed Huffman codes
// squeezes to a few tens of kilobytes.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gfx.hpp"

namespace ch {

namespace {

struct BitWriter {
  std::string out;
  uint32_t acc = 0;
  int n = 0;
  void bits(uint32_t v, int count) { // LSB first
    acc |= v << n;
    n += count;
    while (n >= 8) {
      out += static_cast<char>(acc & 0xFF);
      acc >>= 8;
      n -= 8;
    }
  }
  void huff(uint32_t code, int len) { // Huffman codes go MSB first
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    bits(r, len);
  }
  void flush() {
    if (n > 0) { out += static_cast<char>(acc & 0xFF); acc = 0; n = 0; }
  }
};

void put_symbol(BitWriter &w, int sym) {
  if (sym < 144) w.huff(0x30 + sym, 8);
  else if (sym < 256) w.huff(0x190 + (sym - 144), 9);
  else if (sym < 280) w.huff(sym - 256, 7);
  else w.huff(0xC0 + (sym - 280), 8);
}

const int LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                          31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int DIST_BASE[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void put_match(BitWriter &w, int len, int dist) {
  int li = 28;
  while (li > 0 && LEN_BASE[li] > len) li--;
  put_symbol(w, 257 + li);
  if (LEN_EXTRA[li]) w.bits(static_cast<uint32_t>(len - LEN_BASE[li]), LEN_EXTRA[li]);
  int di = 29;
  while (di > 0 && DIST_BASE[di] > dist) di--;
  w.huff(static_cast<uint32_t>(di), 5);
  if (DIST_EXTRA[di]) w.bits(static_cast<uint32_t>(dist - DIST_BASE[di]), DIST_EXTRA[di]);
}

std::string deflate_fixed(const std::string &in) {
  BitWriter w;
  w.bits(1, 1); // final block
  w.bits(1, 2); // fixed Huffman
  const int N = static_cast<int>(in.size());
  const int HBITS = 15, WINDOW = 32768, MAXCHAIN = 24;
  std::vector<int> head(1u << HBITS, -1), prev(static_cast<std::size_t>(N > 0 ? N : 1), -1);
  const unsigned char *d = reinterpret_cast<const unsigned char *>(in.data());
  auto hash = [&](int i) {
    return ((d[i] << 10) ^ (d[i + 1] << 5) ^ d[i + 2]) & ((1 << HBITS) - 1);
  };
  int i = 0;
  while (i < N) {
    int best = 0, bdist = 0;
    if (i + 2 < N) {
      int cand = head[static_cast<std::size_t>(hash(i))];
      int limit = std::min(258, N - i);
      for (int chain = 0; cand >= 0 && i - cand <= WINDOW && chain < MAXCHAIN; chain++) {
        int l = 0;
        while (l < limit && d[cand + l] == d[i + l]) l++;
        if (l > best) { best = l; bdist = i - cand; if (l == limit) break; }
        cand = prev[static_cast<std::size_t>(cand)];
      }
    }
    int step = 1;
    if (best >= 3) { put_match(w, best, bdist); step = best; }
    else put_symbol(w, d[i]);
    for (int k = 0; k < step; k++, i++) {
      if (i + 2 < N) {
        int h = hash(i);
        prev[static_cast<std::size_t>(i)] = head[static_cast<std::size_t>(h)];
        head[static_cast<std::size_t>(h)] = i;
      }
    }
  }
  put_symbol(w, 256);
  w.flush();
  return w.out;
}

uint32_t crc32(const std::string &s) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

void be32(std::string &o, uint32_t v) {
  o += static_cast<char>(v >> 24);
  o += static_cast<char>(v >> 16);
  o += static_cast<char>(v >> 8);
  o += static_cast<char>(v);
}

void chunk(std::string &png, const char *type, const std::string &body) {
  be32(png, static_cast<uint32_t>(body.size()));
  std::string t(type, 4);
  t += body;
  png += t;
  be32(png, crc32(t));
}

} // namespace

std::string zlib_compress(const std::string &raw) {
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  std::string z = "\x78\x01";
  z += deflate_fixed(raw);
  be32(z, (b << 16) | a);
  return z;
}

std::string png_encode(const Image &im) {
  std::string raw;
  const int stride = (im.w + 1) / 2;
  raw.reserve(static_cast<std::size_t>(stride + 1) * im.h);
  for (int y = 0; y < im.h; y++) {
    raw += '\0'; // filter: none
    const uint8_t *row = &im.px[static_cast<std::size_t>(y) * im.w];
    for (int x = 0; x < im.w; x += 2) {
      uint8_t hi = row[x] & 15, lo = (x + 1 < im.w) ? (row[x + 1] & 15) : 0;
      raw += static_cast<char>((hi << 4) | lo);
    }
  }
  std::string z = zlib_compress(raw);

  std::string png("\x89PNG\r\n\x1a\n", 8);
  std::string ihdr;
  be32(ihdr, static_cast<uint32_t>(im.w));
  be32(ihdr, static_cast<uint32_t>(im.h));
  ihdr += '\x04'; // bit depth
  ihdr += '\x03'; // indexed colour
  ihdr += '\0';
  ihdr += '\0';
  ihdr += '\0';
  chunk(png, "IHDR", ihdr);
  std::string plte;
  for (int i = 0; i < 16; i++)
    for (int k = 0; k < 3; k++) plte += static_cast<char>(VGA_RGB[i][k]);
  chunk(png, "PLTE", plte);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", "");
  return png;
}

} // namespace ch
