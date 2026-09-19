// json.hpp -- tiny dependency-free JSON reader good enough for chart data.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ch {

struct Json {
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
  Type t = NUL;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  bool is_null() const { return t == NUL; }
  bool is_bool() const { return t == BOOL; }
  bool is_num() const { return t == NUM; }
  bool is_str() const { return t == STR; }
  bool is_arr() const { return t == ARR; }
  bool is_obj() const { return t == OBJ; }
  std::size_t size() const { return a.size(); }
  const Json *get(const std::string &key) const;
  Json *find(const std::string &key);
  // Builders, for writing a file back out.  set() replaces or appends and
  // keeps the order keys were first seen in.
  static Json null() { return Json(); }
  static Json boolean(bool v) { Json j; j.t = BOOL; j.b = v; return j; }
  static Json number(double v) { Json j; j.t = NUM; j.n = v; return j; }
  static Json string(const std::string &v) { Json j; j.t = STR; j.s = v; return j; }
  static Json array() { Json j; j.t = ARR; return j; }
  static Json object() { Json j; j.t = OBJ; return j; }
  Json &set(const std::string &key, const Json &v);
  void erase(const std::string &key);
  double num_or(double d = 0) const { return is_num() ? n : d; }
  std::string str_or(const std::string &d = "") const { return is_str() ? s : d; }
};

// throws std::runtime_error with line/column on bad input
Json parse_json(const std::string &text);

// Pretty-printed, two-space indent.  Short arrays of scalars stay on one line
// so a column of numbers does not become a column of lines.
std::string json_write(const Json &j);

// An array whose every member is a number (or a null gap).
bool is_number_array(const Json &j);

} // namespace ch
