// util.hpp -- small string / number helpers and the glyph set.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ch {

// ---- strings ---------------------------------------------------------------
std::string trim(const std::string &s);
std::string lower(std::string s);
bool ieq(const std::string &a, const std::string &b);
bool starts_with(const std::string &s, const std::string &p);
std::vector<std::string> split(const std::string &s, char sep);
std::vector<std::string> split_any(const std::string &s, const std::string &seps);
std::string join(const std::vector<std::string> &v, const std::string &sep);
std::string rep(const std::string &s, int n);
std::string pad_left(std::string s, std::size_t n);
std::string pad_right(std::string s, std::size_t n);
std::string pad_center(std::string s, std::size_t n);
std::string trunc_to(std::string s, std::size_t n);
std::vector<std::string> wrap_words(const std::string &s, std::size_t width);

// ---- numbers ---------------------------------------------------------------
bool parse_num(const std::string &s, double &out);
std::string fmt_val(double v, int prec = -1); // -1 = auto (commas / 2dp)
std::string fmt_raw(double v);                // every digit, no commas: what a cell holds
std::string fmt_axis(double v);               // compact: 1.5K, 2.4M, 3.1B
std::string fmt_pct(double frac, int prec = 1);
double nice_step(double raw);

// ---- unicode ---------------------------------------------------------------
std::string u32_to_utf8(char32_t c);
std::vector<char32_t> utf8_decode(const std::string &s);
std::string clean_utf8(const std::string &s); // ill-formed bytes -> U+FFFD
std::size_t cp_len(const std::string &s); // codepoint count (width-1 assumption)

// ---- glyphs ----------------------------------------------------------------
// blk[] runs dense -> sparse: [0]=full [1]=dark [2]=medium [3]=light
struct Glyphs {
  char32_t blk[4];
  char32_t h, v, tl, tr, bl, br, lt, rt, tt, bt, cross;   // single box
  char32_t dh, dv, dtl, dtr, dbl, dbr;                    // double box
  char32_t hh, hv, htl, htr, hbl, hbr;                    // heavy box
  char32_t dot, bullet, up, down, check;
};

extern Glyphs G;
void use_unicode_glyphs();
void use_ascii_glyphs();

} // namespace ch
