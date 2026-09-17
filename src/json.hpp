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
  bool is_num() const { return t == NUM; }
  bool is_str() const { return t == STR; }
  bool is_arr() const { return t == ARR; }
  bool is_obj() const { return t == OBJ; }
  std::size_t size() const { return a.size(); }
  const Json *get(const std::string &key) const;
  double num_or(double d = 0) const { return is_num() ? n : d; }
  std::string str_or(const std::string &d = "") const { return is_str() ? s : d; }
};

// throws std::runtime_error with line/column on bad input
Json parse_json(const std::string &text);

} // namespace ch
