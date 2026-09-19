#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ch {

Glyphs G;

void use_unicode_glyphs() {
  G.blk[0] = U'\u2588'; // █
  G.blk[1] = U'\u2593'; // ▓
  G.blk[2] = U'\u2592'; // ▒
  G.blk[3] = U'\u2591'; // ░
  G.h = U'\u2500';      // ─
  G.v = U'\u2502';      // │
  G.tl = U'\u250C';     // ┌
  G.tr = U'\u2510';
  G.bl = U'\u2514';
  G.br = U'\u2518';
  G.lt = U'\u251C'; // ├
  G.rt = U'\u2524';
  G.tt = U'\u252C'; // ┬
  G.bt = U'\u2534'; // ┴
  G.cross = U'\u253C';
  G.dh = U'\u2550'; // ═
  G.dv = U'\u2551';
  G.dtl = U'\u2554';
  G.dtr = U'\u2557';
  G.dbl = U'\u255A';
  G.dbr = U'\u255D';
  G.hh = U'\u2501'; // ━
  G.hv = U'\u2503'; // ┃
  G.htl = U'\u250F';
  G.htr = U'\u2513';
  G.hbl = U'\u2517';
  G.hbr = U'\u251B';
  G.dot = U'\u00B7';    // ·
  G.bullet = U'\u25A0'; // ■
  G.up = U'\u25B2';     // ▲
  G.down = U'\u25BC';   // ▼
  G.check = U'\u221A';  // √
}

void use_ascii_glyphs() {
  G.blk[0] = U'#';
  G.blk[1] = U'%';
  G.blk[2] = U':';
  G.blk[3] = U'.';
  G.h = U'-';
  G.v = U'|';
  G.tl = G.tr = G.bl = G.br = U'+';
  G.lt = G.rt = G.tt = G.bt = G.cross = U'+';
  G.dh = U'=';
  G.dv = U'|';
  G.dtl = G.dtr = G.dbl = G.dbr = U'+';
  G.hh = U'=';
  G.hv = U'|';
  G.htl = G.htr = G.hbl = G.hbr = U'+';
  G.dot = U'.';
  G.bullet = U'*';
  G.up = U'^';
  G.down = U'v';
  G.check = U'*';
}

// ---- strings ---------------------------------------------------------------

std::string trim(const std::string &s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

std::string lower(std::string s) {
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool ieq(const std::string &a, const std::string &b) { return lower(a) == lower(b); }

bool starts_with(const std::string &s, const std::string &p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

std::vector<std::string> split_any(const std::string &s, const std::string &seps) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (seps.find(c) != std::string::npos) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

std::string join(const std::vector<std::string> &v, const std::string &sep) {
  std::string o;
  for (std::size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; }
  return o;
}

std::string rep(const std::string &s, int n) {
  std::string o;
  for (int i = 0; i < n; i++) o += s;
  return o;
}

std::string pad_left(std::string s, std::size_t n) {
  while (cp_len(s) < n) s.insert(s.begin(), ' ');
  return s;
}

std::string pad_right(std::string s, std::size_t n) {
  while (cp_len(s) < n) s += ' ';
  return s;
}

std::string pad_center(std::string s, std::size_t n) {
  std::size_t len = cp_len(s);
  if (len >= n) return s;
  std::size_t left = (n - len) / 2;
  return std::string(left, ' ') + s + std::string(n - len - left, ' ');
}

std::string trunc_to(std::string s, std::size_t n) {
  if (cp_len(s) <= n) return s;
  if (n == 0) return "";
  if (n == 1) return s.substr(0, 1);
  // keep it glyph-safe: walk bytes until we have n-1 codepoints, add '.'
  auto cps = utf8_decode(s);
  std::string o;
  for (std::size_t i = 0; i + 1 < n && i < cps.size(); i++) o += u32_to_utf8(cps[i]);
  return o + ".";
}

// ---- numbers ---------------------------------------------------------------

bool parse_num(const std::string &raw, double &out) {
  std::string s = trim(raw);
  if (s.empty()) return false;
  // strip thousands separators, currency and percent decoration
  std::string t;
  t.reserve(s.size());
  for (char c : s) {
    if (c == ',' || c == '_' || c == ' ' || c == '$') continue;
    t.push_back(c);
  }
  if (!t.empty() && t.back() == '%') t.pop_back();
  if (t.empty()) return false;
  const char *b = t.c_str();
  char *end = nullptr;
  double v = std::strtod(b, &end);
  if (end == b) return false;
  while (end && *end && std::isspace(static_cast<unsigned char>(*end))) end++;
  if (end && *end) return false;
  if (!std::isfinite(v)) return false;
  out = v;
  return true;
}

static std::string group_digits(const std::string &digits) {
  std::string o;
  for (std::size_t i = 0; i < digits.size(); i++) {
    if (i && (digits.size() - i) % 3 == 0) o += ',';
    o += digits[i];
  }
  return o;
}

static std::string commafy(double v, int prec) {
  char buf[128];
  std::snprintf(buf, sizeof buf, "%.*f", prec, v);
  std::string s(buf);
  bool neg = !s.empty() && s[0] == '-';
  if (neg) s.erase(s.begin());
  std::size_t dot = s.find('.');
  std::string ip = (dot == std::string::npos) ? s : s.substr(0, dot);
  std::string fp = (dot == std::string::npos) ? "" : s.substr(dot);
  return (neg ? "-" : "") + group_digits(ip) + fp;
}

std::string fmt_val(double v, int prec) {
  if (std::isnan(v)) return "n/a";
  if (prec >= 0) return commafy(v, prec);
  if (v == std::floor(v) && std::fabs(v) < 1e15) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f", v);
    std::string s(buf);
    bool neg = !s.empty() && s[0] == '-';
    if (neg) s.erase(s.begin());
    return (neg ? "-" : "") + group_digits(s);
  }
  // up to two decimals (three below 1), without a tail of zeros: 6.9, not 6.90
  std::string s = commafy(v, std::fabs(v) < 1 ? 3 : 2);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s.empty() || s == "-" ? "0" : s;
}

std::string fmt_raw(double v) {
  if (!std::isfinite(v)) return "";
  char b[40];
  if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%.0f", v);
  else std::snprintf(b, sizeof b, "%.12g", v);
  return b;
}

std::string fmt_axis(double v) {
  if (std::isnan(v)) return "n/a";
  double a = std::fabs(v);
  const char *suf = nullptr;
  double div = 1;
  if (a >= 1e9) { suf = "B"; div = 1e9; }
  else if (a >= 1e6) { suf = "M"; div = 1e6; }
  else if (a >= 1e3) { suf = "K"; div = 1e3; }
  if (!suf) return fmt_val(v);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.2f", v / div);
  std::string s(buf);
  // trim trailing zeros of the fraction
  std::size_t dot = s.find('.');
  if (dot != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  return s + suf;
}

std::string fmt_pct(double frac, int prec) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f%%", prec, frac * 100.0);
  return std::string(buf);
}

double nice_step(double raw) {
  if (!(raw > 0)) return 1;
  double mag = std::pow(10.0, std::floor(std::log10(raw)));
  double n = raw / mag;
  double m = n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10;
  return m * mag;
}

// ---- unicode ---------------------------------------------------------------

std::string u32_to_utf8(char32_t c) {
  std::string o;
  if (c < 0x80) {
    o += static_cast<char>(c);
  } else if (c < 0x800) {
    o += static_cast<char>(0xC0 | (c >> 6));
    o += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    o += static_cast<char>(0xE0 | (c >> 12));
    o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    o += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    o += static_cast<char>(0xF0 | (c >> 18));
    o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    o += static_cast<char>(0x80 | (c & 0x3F));
  }
  return o;
}

// Strict: a byte that does not belong to a well-formed sequence becomes
// U+FFFD, so broken input can never come back out as broken output.
std::vector<char32_t> utf8_decode(const std::string &s) {
  std::vector<char32_t> out;
  std::size_t i = 0;
  const std::size_t n = s.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { out.push_back(c); i++; continue; }
    int extra = c >= 0xF0 && c <= 0xF4 ? 3 : (c >= 0xE0 && c < 0xF0 ? 2 : (c >= 0xC2 && c < 0xE0 ? 1 : -1));
    char32_t cp = extra == 3 ? (c & 0x07) : (extra == 2 ? (c & 0x0F) : (c & 0x1F));
    bool good = extra > 0 && i + static_cast<std::size_t>(extra) < n;
    if (good)
      for (int k = 1; k <= extra; k++) {
        unsigned char t = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
        if ((t & 0xC0) != 0x80) { good = false; break; }
        cp = (cp << 6) | (t & 0x3F);
      }
    static const char32_t MIN[4] = {0, 0x80, 0x800, 0x10000};
    if (good && (cp < MIN[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) good = false;
    if (!good) { out.push_back(0xFFFD); i++; continue; }
    out.push_back(cp);
    i += static_cast<std::size_t>(extra) + 1;
  }
  return out;
}

std::string clean_utf8(const std::string &s) {
  std::string out;
  for (char32_t cp : utf8_decode(s)) out += u32_to_utf8(cp);
  return out;
}

std::size_t cp_len(const std::string &s) { return utf8_decode(s).size(); }

} // namespace ch
