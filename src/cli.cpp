#include "cli.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "chart.hpp"
#include "util.hpp"

namespace ch {

static const char *kVersion = "1.0.0";

void print_version(FILE *f) { std::fprintf(f, "charts %s\n", kVersion); }

static std::vector<std::pair<std::string, std::string>> type_help() {
  return {
      {"bar", "vertical bars; several series sit side by side, 3-D tops"},
      {"grouped", "same as bar (alias)"},
      {"stacked", "vertical bars stacked into a total per category"},
      {"hbar", "horizontal bars, values at the bar end"},
      {"line", "connected markers: * o + x # @ ^ ~"},
      {"area", "line with the space under it shaded"},
      {"pie", "flat pie, percentages inside fat slices"},
      {"pie3d", "ellipsoid pie with a swept side wall (the 1990 one)"},
      {"donut", "3-D ring, hole in the middle"},
      {"scatter", "XY points; --xy takes the first column as X"},
      {"hist", "histogram of the first series, --bins N buckets"},
      {"table", "the raw numbers as a grid"},
  };
}

void print_types(FILE *f) {
  for (const auto &t : type_help()) std::fprintf(f, "%-9s %s\n", t.first.c_str(), t.second.c_str());
}

void print_palettes(FILE *f) {
  std::fprintf(f, "dos      bright primary colours (default)\n");
  std::fprintf(f, "ega      normal-intensity EGA order\n");
  std::fprintf(f, "cga      cyan/magenta/yellow/white, four-colour CGA card\n");
  std::fprintf(f, "ice      cyan/blue/white\n");
  std::fprintf(f, "fire     red/orange/yellow\n");
  std::fprintf(f, "green    phosphor greens\n");
  std::fprintf(f, "amber    amber terminal\n");
  std::fprintf(f, "mono     white/grey only, colour-free\n");
}

void print_example(FILE *f, const std::string &what) {
  if (what == "json" || what == "j") {
    std::fprintf(f,
                 "[\n"
                 "  {\"label\": \"Q1\", \"north\": 120, \"south\": 88},\n"
                 "  {\"label\": \"Q2\", \"north\": 145, \"south\": 91},\n"
                 "  {\"label\": \"Q3\", \"north\": 132, \"south\": 110}\n"
                 "]\n");
    return;
  }
  std::fprintf(f,
               "month,revenue,costs\n"
               "Jan,120,80\n"
               "Feb,145,88\n"
               "Mar,132,91\n"
               "Apr,178,104\n");
}

void print_help(FILE *f) {
  std::fprintf(f,
"charts %s -- ANSI charts for the Linux console (fbcon/DRM tty, no X, no Wayland)\n"
"\n"
"usage: charts [FILE|-] [options]\n"
"       charts a.csv b.json c.tsv --tile        many charts, one screen\n"
"       charts some/dir                          interactive over *.csv/*.json\n"
"       charts -i data.csv                        interactive, flip with keys\n"
"       cat data.csv | charts -t pie3d            from a pipe\n"
"\n"
"Draws one chart that fits the screen and prints it. 16 colours, half-block\n"
"shading, DOS-era boxes. No dependencies, no runtime, one binary.\n"
"\n"
"INPUT (see docs/INPUT.md)\n"
"  CSV/TSV  first row is a header unless it is all numbers. A non-numeric first\n"
"           column becomes the category labels; every other numeric column is a\n"
"           series. Delimiter , ; TAB | is sniffed. '#' starts a comment line.\n"
"  JSON     array of records   [{\"label\":\"Jan\",\"cpu\":12,\"mem\":40}, ...]\n"
"           named series       {\"labels\":[..],\"series\":[{\"name\":\"cpu\",\"values\":[..]}]}\n"
"           object of numbers  {\"Mon\":10,\"Tue\":20}   array of numbers [1,2,3]\n"
"           rows with header   [[null,\"cpu\",\"mem\"],[\"Jan\",12,40], ...]\n"
"           x/y form           {\"x\":[1,2,3],\"y\":[4,5,6]}\n"
"  -        reads stdin.\n"
"\n\n", kVersion);
  std::fprintf(f, "CHART TYPES  (-t NAME[,NAME...], '--list-types')\n");
  print_types(f);
  std::fprintf(f,
"\n"
"OUTPUT\n"
"  -t, --type NAME      chart type (default bar; comma list + --tile tiles them)\n"
"  -T, --title TEXT     title, drawn into the top border\n"
"      --subtitle TEXT  second line, centred\n"
"      --xlabel TEXT    label under the x axis\n"
"      --ylabel TEXT    label up the left side, rotated one char per row\n"
"  -p, --palette NAME   colour set (default dos; '--list-palettes')\n"
"  -w, --width N        force width in cells (default: the terminal)\n"
"  -H, --height N       force height in rows\n"
"      --frame STYLE    double|single|heavy|ascii|none (default double)\n"
"      --no-shadow      no drop shadow behind the frame\n"
"      --tile           lay several charts out on one screen (grid)\n"
"      --legend/--no-legend, --values/--no-values, --grid/--no-grid\n"
"      --color/--no-color   ANSI colour off when stdout is not a tty\n"
"      --ascii          pure ASCII glyphs, no box drawing or blocks\n"
"\n"
"DATA MASSAGE\n"
"      --xy             first numeric column is the X axis (scatter)\n"
"      --transpose      swap rows and columns first\n"
"      --delim C        force the delimiter (C may be 'tab')\n"
"      --no-header      treat the first row as data\n"
"      --labels-col N   use column N (1-based) for labels\n"
"      --series-col N   keep only numeric column N as a series\n"
"      --label-key K    JSON: record field to use as the label\n"
"      --min V --max V  pin the value axis\n"
"      --explode [N]    pop slice N out of the pie (bare = the biggest)\n"
"      --depth N        rows of 3-D extrusion (default 2)\n"
"      --bins N         histogram buckets (default 10)\n"
"      --prec N         fixed decimals in value labels\n"
"\n"
"RUNNING\n"
"  -i, --interactive    full-screen TUI\n"
"      --watch          redraw when the file changes (agents: write csv, look)\n"
"      --describe       print what charts parsed out of the file, then exit\n"
"      --example [csv|json]  print a sample input file\n"
"      --list-types | --list-palettes | --version | -h\n"
"  -o, --out FILE       write to FILE instead of stdout\n"
"\n"
"KEYS (interactive)\n"
"  c / C     next / previous chart type        arrows  same\n"
"  p / P     next / previous palette           1-9     jump to type\n"
"  t         table view                        e       explode next slice\n"
"  v         value labels                      g       grid on/off\n"
"  d         deeper 3-D                        D       flatter\n"
"  n / N     next / previous file              r       reload now\n"
"  w         toggle auto-reload on file change\n"
"  ?         keys                              q       quit\n"
"\n"
"EXAMPLES\n"
"  charts examples/revenue.csv -t pie3d -T 'Revenue mix'\n"
"  charts examples/quarterly.csv -t stacked --values --ylabel 'units'\n"
"  charts examples/disk.json -t hbar --palette ice\n"
"  charts a.csv b.csv c.csv d.csv --tile -t bar,line,pie3d,stacked\n"
"  charts -i examples/           # browse a directory of data\n"
);
}

Args parse_args(int argc, char **argv) {
  Args a;
  const char *env = std::getenv("NO_COLOR");
  if (env && *env) a.color = false;

  auto need = [&](int &i, const char *what) -> std::string {
    if (i + 1 >= argc) throw std::runtime_error(std::string("option ") + what + " needs a value");
    return argv[++i];
  };
  auto num = [&](const std::string &s, const char *what) -> double {
    double d;
    if (!parse_num(s, d)) throw std::runtime_error(std::string("bad number for ") + what + ": " + s);
    return d;
  };

  std::vector<std::string> positional;
  bool end_opts = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (end_opts || arg.empty() || arg[0] != '-' || arg == "-") {
      positional.push_back(arg);
      continue;
    }
    if (arg == "--") { end_opts = true; continue; }

    std::string name = arg, val;
    bool has_val = false;
    std::size_t eq = arg.find('=');
    if (starts_with(arg, "--") && eq != std::string::npos) {
      name = arg.substr(0, eq);
      val = arg.substr(eq + 1);
      has_val = true;
    }
    auto value = [&]() -> std::string { return has_val ? val : need(i, name.c_str()); };

    if (name == "-h" || name == "--help") a.help = true;
    else if (name == "-V" || name == "--version") a.version = true;
    else if (name == "-t" || name == "--type") {
      for (auto &t : split(value(), ',')) {
        std::string tt = lower(trim(t));
        if (!tt.empty()) a.types.push_back(tt);
      }
    } else if (name == "-T" || name == "--title") a.title = value();
    else if (name == "--subtitle") a.subtitle = value();
    else if (name == "--xlabel") a.xlabel = value();
    else if (name == "--ylabel") a.ylabel = value();
    else if (name == "-p" || name == "--palette") a.palette = value();
    else if (name == "-w" || name == "--width") a.width = static_cast<int>(num(value(), "width"));
    else if (name == "-H" || name == "--height") a.height = static_cast<int>(num(value(), "height"));
    else if (name == "--frame") a.frame = lower(value());
    else if (name == "-o" || name == "--out") a.out = value();
    else if (name == "-i" || name == "--interactive" || name == "--tui") a.interactive = true;
    else if (name == "--watch") { a.watch = true; }
    else if (name == "--watch-ms") a.watch_ms = static_cast<int>(num(value(), "watch-ms"));
    else if (name == "--tile" || name == "--grid-layout") a.tile = true;
    else if (name == "--legend") a.legend = true;
    else if (name == "--no-legend") a.legend = false;
    else if (name == "--values") a.values = true;
    else if (name == "--no-values") a.values = false;
    else if (name == "--grid") a.grid = true;
    else if (name == "--no-grid") a.grid = false;
    else if (name == "--shadow") a.shadow = true;
    else if (name == "--no-shadow") a.shadow = false;
    else if (name == "--color" || name == "--colour") { a.color = true; a.color_forced = true; }
    else if (name == "--no-color" || name == "--no-colour") { a.color = false; a.color_forced = true; }
    else if (name == "--ascii") a.ascii = true;
    else if (name == "--xy") a.xy = true;
    else if (name == "--transpose") a.transpose = true;
    else if (name == "--no-header") a.no_header = true;
    else if (name == "--delim" || name == "--delimiter") {
      std::string d = value();
      if (ieq(d, "tab") || d == "\\t") a.delim = '\t';
      else if (ieq(d, "comma")) a.delim = ',';
      else if (ieq(d, "semi") || ieq(d, "semicolon")) a.delim = ';';
      else if (ieq(d, "pipe")) a.delim = '|';
      else if (d.size() == 1) a.delim = d[0];
      else throw std::runtime_error("bad --delim: " + d);
    } else if (name == "--labels-col") a.label_col = static_cast<int>(num(value(), "labels-col")) - 1;
    else if (name == "--series-col") a.series_col = static_cast<int>(num(value(), "series-col")) - 1;
    else if (name == "--label-key") a.label_key = value();
    else if (name == "--min") { a.lo = num(value(), "min"); a.has_lo = true; }
    else if (name == "--max") { a.hi = num(value(), "max"); a.has_hi = true; }
    else if (name == "--depth") a.depth = static_cast<int>(num(value(), "depth"));
    else if (name == "--bins") a.bins = static_cast<int>(num(value(), "bins"));
    else if (name == "--prec" || name == "--decimals") a.prec = static_cast<int>(num(value(), "prec"));
    else if (name == "--explode") {
      a.explode = -1;
      if (!has_val && i + 1 < argc) {
        double d;
        if (parse_num(argv[i + 1], d)) { i++; a.explode = static_cast<int>(d); }
      } else if (has_val && !val.empty()) {
        double d;
        if (!parse_num(val, d)) throw std::runtime_error("bad --explode value: " + val);
        a.explode = static_cast<int>(d);
      }
    } else if (name == "--describe" || name == "--dump") a.describe = true;
    else if (name == "--example") {
      a.example = "csv";
      if (!has_val && i + 1 < argc && argv[i + 1][0] != '-') a.example = argv[++i];
      else if (has_val && !val.empty()) a.example = val;
    } else if (name == "--list-types" || name == "--types") a.list_types = true;
    else if (name == "--list-palettes" || name == "--palettes") a.list_palettes = true;
    else if (name == "-q" || name == "--quiet") { /* accepted, nothing to say */ }
    else throw std::runtime_error("unknown option: " + arg + "\nrun 'charts -h' for the list");
  }

  a.files = positional;

  if (a.width < 0 || a.height < 0) throw std::runtime_error("width/height must be positive");
  if (a.depth < 0) a.depth = 0;
  if (a.depth > 6) a.depth = 6;
  if (a.bins < 1) a.bins = 1;
  if (a.bins > 60) a.bins = 60;
  for (const auto &t : a.types) {
    std::string tt = t;
    if (tt == "column") continue;
    if (tt == "xy") continue;
    if (!type_valid(tt)) throw std::runtime_error("unknown chart type: " + tt + " (see --list-types)");
  }
  if (!a.frame.empty()) {
    if (a.frame != "double" && a.frame != "single" && a.frame != "heavy" && a.frame != "ascii" &&
        a.frame != "none")
      throw std::runtime_error("bad --frame: " + a.frame);
  }
  return a;
}

} // namespace ch
