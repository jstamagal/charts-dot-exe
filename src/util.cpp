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
  if (n == 1) return u32_to_utf8(utf8_decode(s).front()); // a character, never half of one
  // keep it glyph-safe: walk bytes until we have n-1 codepoints, add '.'
  auto cps = utf8_decode(s);
  std::string o;
  for (std::size_t i = 0; i + 1 < n && i < cps.size(); i++) o += u32_to_utf8(cps[i]);
  return o + ".";
}

// ---- numbers ---------------------------------------------------------------

// A number the way people write one in a table: -1,234.5  $12  12%  +3  1_000
// 1e6.  Grouping marks only between digits of the whole part.  Anything else
// is not a number, so it is text rather than a wrong value: "1 2" is not 12,
// "0x1A" is not 26, and inf, nan and 1.2.3 are not numbers at all.
bool parse_num(const std::string &raw, double &out) {
  std::string s = trim(raw);
  if (!s.empty() && s.back() == '%') s.pop_back();
  std::string t;
  std::size_t i = 0;
  auto sign = [&] {
    if (t.empty() && i < s.size() && (s[i] == '-' || s[i] == '+')) t += s[i++];
  };
  sign();
  if (i < s.size() && s[i] == '$') i++;
  sign();
  auto digit = [&](std::size_t k) { return k < s.size() && std::isdigit(static_cast<unsigned char>(s[k])) != 0; };
  bool digits = false, dot = false, exp = false;
  for (; i < s.size(); i++) {
    const char c = s[i];
    if (digit(i)) { t += c; digits = true; }
    else if ((c == ',' || c == '_') && !dot && !exp && i > 0 && digit(i - 1) && digit(i + 1)) continue;
    else if (c == '.' && !dot && !exp) { t += c; dot = true; }
    else if ((c == 'e' || c == 'E') && digits && !exp && (digit(i + 1) || ((i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) && digit(i + 2)))) {
      t += c;
      exp = true;
      if (!digit(i + 1)) t += s[++i];
    } else return false;
  }
  if (!digits) return false;
  char *end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (!end || *end || !std::isfinite(v)) return false;
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

bool is_wide(char32_t c) {
  return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0x303E) || (c >= 0x3041 && c <= 0x33FF) ||
         (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xA000 && c <= 0xA4CF) ||
         (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) ||
         (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) ||
         (c >= 0x1F680 && c <= 0x1F6FF) || (c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x1FA70 && c <= 0x1FAFF) ||
         (c >= 0x20000 && c <= 0x3FFFD);
}

char32_t cell_of(char32_t c) {
  if (c < 0x20 || (c >= 0x7F && c < 0xA0)) return U' ';
  if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
      (c >= 0x2060 && c <= 0x2069) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF)
    return 0;
  return is_wide(c) ? U'?' : c;
}

bool has_wide(const std::string &s) {
  for (char32_t c : utf8_decode(s))
    if (is_wide(c)) return true;
  return false;
}

std::size_t cp_len(const std::string &s) {
  std::size_t n = 0;
  for (char32_t c : utf8_decode(s))
    if (cell_of(c)) n++;
  return n;
}

// ---- wrapping ----------------------------------------------------------------

// Words onto lines of at most width codepoints; a word longer than that is
// cut.  Newlines start a new line.
std::vector<std::string> wrap_words(const std::string &s, std::size_t width) {
  std::vector<std::string> out;
  for (const std::string &para : split(s, '\n')) {
    std::string line;
    for (const std::string &word : split_any(para, " \t")) {
      if (word.empty()) continue;
      if (!line.empty() && cp_len(line) + 1 + cp_len(word) > width) {
        out.push_back(line);
        line.clear();
      }
      line += (line.empty() ? "" : " ") + word;
      // trunc_to ends a cut line with '.', which takes the place of one
      // character; at width 1 there is no room for the dot and nothing is
      // replaced, so the rest must start one further on or never shrink.
      const std::size_t cut = width > 1 ? width - 1 : 1;
      while (cp_len(line) > width) {
        out.push_back(trunc_to(line, width));
        auto cps = utf8_decode(line);
        std::string rest;
        for (std::size_t k = cut; k < cps.size(); k++) rest += u32_to_utf8(cps[k]);
        line = rest;
      }
    }
    out.push_back(line);
  }
  while (out.size() > 1 && out.back().empty()) out.pop_back();
  return out;
}

} // namespace ch
