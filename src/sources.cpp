#include "sources.hpp"

#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

#include "util.hpp"

namespace ch {

bool is_dir(const std::string &path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode) != 0;
}

std::vector<std::string> expand_sources(const std::vector<std::string> &args) {
  std::vector<std::string> out;
  for (const auto &a : args) {
    if (a == "-") {
      out.push_back(a);
      continue;
    }
    if (!is_dir(a)) {
      out.push_back(a);
      continue;
    }
    DIR *d = ::opendir(a.c_str());
    if (!d) {
      out.push_back(a);
      continue;
    }
    std::vector<std::string> here;
    while (struct dirent *e = ::readdir(d)) {
      std::string n = e->d_name;
      if (n.empty() || n[0] == '.') continue;
      std::string l = lower(n);
      if (l.size() > 4 && (l.compare(l.size() - 4, 4, ".csv") == 0 ||
                           l.compare(l.size() - 4, 4, ".tsv") == 0 ||
                           l.compare(l.size() - 5, 5, ".json") == 0))
        here.push_back(n);
    }
    ::closedir(d);
    std::sort(here.begin(), here.end());
    std::string base = a;
    if (!base.empty() && base.back() != '/') base += '/';
    for (const auto &n : here) out.push_back(base + n);
  }
  return out;
}

long long file_mtime(const std::string &path) {
  if (path == "-") return 0;
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return 0;
  return static_cast<long long>(st.st_mtime) * 1000000000LL + st.st_mtim.tv_nsec;
}

} // namespace ch
