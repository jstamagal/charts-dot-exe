// sources.hpp -- turn command-line arguments (files, dirs, "-") into data paths.
#pragma once

#include <string>
#include <vector>

namespace ch {

// Directories expand to the *.csv/*.tsv/*.json files inside them, sorted.
// "-" is kept as-is (stdin).
std::vector<std::string> expand_sources(const std::vector<std::string> &args);

// mtime in nanoseconds, 0 when the file cannot be stat'ed.
long long file_mtime(const std::string &path);

bool is_dir(const std::string &path);

} // namespace ch
