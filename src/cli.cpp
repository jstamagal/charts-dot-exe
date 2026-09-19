#include "cli.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "chart.hpp"
#include "display.hpp"
#include "json.hpp"
#include "util.hpp"

namespace ch {

static const char *kVersion = "2.0.0";

void print_version(FILE *f) { std::fprintf(f, "charts %s\n", kVersion); }

// One line per type, in the order type_names() gives; a type without a line
// here still gets listed.
void print_types(FILE *f) {
  static const char *rows[][2] = {
      {"bar", "vertical bars, side by side per series; extruded unless --depth 0"},
      {"stacked", "bars stacked into a total per category"},
      {"hbar", "horizontal bars, names down the left: best for long labels and rankings"},
      {"dumbbell", "before -> after: a dot per series on each row, an arrow to the last"},
      {"line", "one line per series, a different marker each"},
      {"area", "line with a dithered fill underneath"},
      {"pie", "flat pie: one series -> a slice per row; several -> a slice per series"},
      {"pie3d", "the extruded one; --explode N pops slice N out"},
      {"donut", "ring; --depth 0 for flat"},
      {"scatter", "XY points; --xy takes the first numeric column as X"},
      {"hist", "histogram of the first series, --bins N buckets"},
      {"table", "the numbers as a grid"},
  };
  for (const auto &t : type_names()) {
    const char *what = "";
    for (const auto &r : rows)
      if (t == r[0]) what = r[1];
    std::fprintf(f, "%-9s %s\n", t.c_str(), what);
  }
}

void print_palettes(FILE *f) {
  std::fprintf(f, "dos      yellow, cyan, green, red ...: the bright eight first (default)\n");
  std::fprintf(f, "ega      red, green, yellow, blue ...: EGA order\n");
  std::fprintf(f, "cga      cyan, magenta, white, yellow: the four-colour card\n");
  std::fprintf(f, "ice      cyans, blues, white\n");
  std::fprintf(f, "fire     yellow, red, brown\n");
  std::fprintf(f, "green    phosphor greens\n");
  std::fprintf(f, "amber    amber terminal\n");
  std::fprintf(f, "mono     white and grey; series are told apart by dither\n");
}

void print_themes(FILE *f) {
  std::fprintf(f, "dos      blue desktop, black chart windows, grey status bar (default for decks)\n");
  std::fprintf(f, "black    no background at all (default when printing into a shell)\n");
  std::fprintf(f, "light    grey desktop, white chart windows, dark ink\n");
}

void print_example(FILE *f, const std::string &what) {
  if (what == "deck" || what == "d") {
    std::fputs(R"({
  "title": "Q3 review",
  "theme": "dos",
  "footer": "ACME  -  Q3 review",
  "slides": [
    {"title": "Q3 review", "subtitle": "Revenue, costs and what comes next"},
    {
      "title": "Revenue grew every month but June",
      "type": "bar", "values": true, "ylabel": "USD k",
      "data": "revenue.csv",
      "annotations": [
        {"at": "Apr", "series": "revenue", "text": "spring launch"},
        {"y": 150, "text": "target"}
      ],
      "notes": "June dipped because of the warehouse move."
    },
    {
      "title": "Where the money went",
      "layout": "cols",
      "blocks": [
        {"type": "pie3d", "explode": 0, "weight": 2,
         "data": {"labels": ["payroll", "cloud", "rent", "other"], "series": [{"name": "USD k", "values": [620, 310, 140, 95]}]}},
        {"rows": [
          {"stat": "1,165", "label": "total spend, USD k", "delta": "+4% vs Q2"},
          {"bullets": ["Payroll is **53%** of spend", "Cloud doubled since Q1", "Rent is fixed until 2027"]}
        ]}
      ]
    }
  ]
}
)", f);
    return;
  }
  if (what == "json" || what == "j") {
    std::fputs(R"({
  "chart": {"type": "line", "title": "Load average", "ylabel": "load"},
  "rows": [
    {"label": "09:00", "web": 1.2, "db": 0.8},
    {"label": "10:00", "web": 1.9, "db": 1.1},
    {"label": "11:00", "web": 2.4, "db": 1.0}
  ]
}
)", f);
    return;
  }
  std::fputs("#chart: type=bar, title=\"Revenue vs costs\", values\n"
             "month,revenue,costs\n"
             "Jan,120,80\n"
             "Feb,145,88\n"
             "Mar,132,91\n"
             "Apr,178,104\n",
             f);
}

namespace {

void set_spec(ChartSpec &spec, const std::string &key, const Json &v) {
  Json o = Json::object();
  o.set(key, v);
  try {
    spec_from_json(o, spec);
  } catch (const std::exception &e) {
    std::string m = e.what();
    if (starts_with(m, "chart spec: ")) m = m.substr(12);
    throw UsageError("--" + key + ": " + m);
  }
}

} // namespace

Args parse_args(int argc, char **argv) {
  Args a;
  const char *env = std::getenv("NO_COLOR");
  if (env && *env) a.color = false;

  auto need = [&](int &i, const std::string &what) -> std::string {
    if (i + 1 >= argc) throw UsageError("option " + what + " needs a value");
    return argv[++i];
  };
  auto integer = [&](const std::string &s, const std::string &what, int lo, int hi) -> int {
    double d;
    if (!parse_num(s, d) || d != static_cast<int>(d) || d < lo || d > hi)
      throw UsageError(what + " wants a whole number from " + std::to_string(lo) + " to " + std::to_string(hi) + ", not \"" + s + "\"");
    return static_cast<int>(d);
  };

  bool end_opts = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (end_opts || arg.empty() || arg[0] != '-' || arg == "-") {
      a.files.push_back(arg);
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
    auto value = [&]() -> std::string { return has_val ? val : need(i, name); };
    // "--values" / "--no-values" style switches that are chart settings
    auto toggle = [&](const char *key) -> bool {
      if (name == std::string("--") + key) { set_spec(a.cli, key, Json::boolean(true)); return true; }
      if (name == std::string("--no-") + key) { set_spec(a.cli, key, Json::boolean(false)); return true; }
      return false;
    };

    if (name == "-h" || name == "--help") a.help = true;
    else if (name == "-V" || name == "--version") a.version = true;
    else if (name == "-t" || name == "--type") {
      for (auto &t : split(value(), ',')) {
        std::string tt = type_canonical(t);
        if (tt.empty()) continue;
        if (!type_valid(tt)) throw UsageError("unknown chart type: " + tt + " (see --list-types)");
        a.types.push_back(tt);
      }
    } else if (name == "-T" || name == "--title") set_spec(a.cli, "title", Json::string(value()));
    else if (name == "--subtitle") set_spec(a.cli, "subtitle", Json::string(value()));
    else if (name == "--xlabel") set_spec(a.cli, "xlabel", Json::string(value()));
    else if (name == "--ylabel") set_spec(a.cli, "ylabel", Json::string(value()));
    else if (name == "-p" || name == "--palette") set_spec(a.cli, "palette", Json::string(value()));
    else if (name == "--frame") set_spec(a.cli, "frame", Json::string(value()));
    else if (name == "--theme") {
      a.theme = lower(value());
      bool ok = false;
      for (const auto &t : theme_names())
        if (t == a.theme) ok = true;
      if (!ok) throw UsageError("unknown theme: " + a.theme + " (see --list-themes)");
    } else if (name == "--gfx") {
      a.gfx = lower(value());
      if (!gfx_name_valid(a.gfx)) throw UsageError("--gfx wants auto, fb, kitty, sixel, cells or ascii");
    } else if (name == "-w" || name == "--width") a.width = integer(value(), "--width", 1, 1000);
    else if (name == "-H" || name == "--height") a.height = integer(value(), "--height", 1, 500);
    else if (name == "--size") {
      std::string s = lower(value());
      std::size_t x = s.find('x');
      if (x == std::string::npos) throw UsageError("--size wants COLSxROWS, e.g. 120x33");
      a.width = integer(s.substr(0, x), "--size", 20, 1000);
      a.height = integer(s.substr(x + 1), "--size", 6, 500);
    } else if (name == "--scale") a.scale = integer(value(), "--scale", 1, 8);
    else if (name == "--slide") a.slide = integer(value(), "--slide", 1, 100000);
    else if (name == "-o" || name == "--out") a.out = value();
    else if (name == "--png") a.png = value();
    else if (name == "--png-dir") a.png_dir = value();
    else if (name == "--launcher") a.launcher = value();
    else if (name == "-i" || name == "--show" || name == "--interactive" || name == "--tui" || name == "--watch") a.show = true;
    else if (name == "--print") a.print = true;
    else if (name == "--check" || name == "--validate") a.check = true;
    else if (name == "--json") a.json = true;
    else if (name == "--describe" || name == "--dump") a.describe = true;
    else if (name == "--schema") a.schema = true;
    else if (name == "--tile") a.tile = true;
    else if (name == "-v" || name == "--verbose") a.verbose = true;
    else if (toggle("legend") || toggle("values") || toggle("grid") || toggle("shadow")) { /* set */ }
    else if (name == "--color" || name == "--colour") { a.color = true; a.color_forced = true; }
    else if (name == "--no-color" || name == "--no-colour") { a.color = false; a.color_forced = true; }
    else if (name == "--ascii") a.ascii = true;
    else if (name == "--xy") { set_spec(a.cli, "xy", Json::boolean(true)); a.load.xy = a.load.has_xy = true; }
    else if (name == "--transpose") a.load.transpose = a.load.has_transpose = true;
    else if (name == "--no-header") a.load.no_header = a.load.has_no_header = true;
    else if (name == "--delim" || name == "--delimiter") {
      ChartSpec tmp;
      set_spec(tmp, "delim", Json::string(value()));
      a.load.delim = tmp.delim;
      a.load.has_delim = true;
    } else if (name == "--labels-col") { a.load.label_col = integer(value(), name, 1, 1000) - 1; a.load.has_label_col = true; }
    else if (name == "--series-col") { a.load.series_col = integer(value(), name, 1, 1000) - 1; a.load.has_series_col = true; }
    else if (name == "--label-key") { a.load.label_key = value(); a.load.has_label_key = true; }
    else if (name == "--min") set_spec(a.cli, "min", Json::string(value()));
    else if (name == "--max") set_spec(a.cli, "max", Json::string(value()));
    else if (name == "--depth") set_spec(a.cli, "depth", Json::string(value()));
    else if (name == "--bins") set_spec(a.cli, "bins", Json::string(value()));
    else if (name == "--prec" || name == "--decimals") set_spec(a.cli, "prec", Json::string(value()));
    else if (name == "--explode") {
      // the value is optional: a bare --explode pops the biggest slice
      double d;
      if (has_val) set_spec(a.cli, "explode", val.empty() ? Json::boolean(true) : Json::string(val));
      else if (i + 1 < argc && parse_num(argv[i + 1], d)) set_spec(a.cli, "explode", Json::number(std::atof(argv[++i])));
      else set_spec(a.cli, "explode", Json::boolean(true));
    } else if (name == "--example") {
      a.example = "csv";
      if (has_val) a.example = val;
      else if (i + 1 < argc && argv[i + 1][0] != '-') a.example = argv[++i];
      if (a.example != "csv" && a.example != "json" && a.example != "deck" && a.example != "j" && a.example != "d")
        throw UsageError("--example wants csv, json or deck");
    } else if (name == "--list-types" || name == "--types") a.list_types = true;
    else if (name == "--list-palettes" || name == "--palettes") a.list_palettes = true;
    else if (name == "--list-themes" || name == "--themes") a.list_themes = true;
    else if (name == "-q" || name == "--quiet") { /* accepted, nothing to say */ }
    else throw UsageError("unknown option: " + arg + "  (charts -h lists them)");
  }
  if (!a.png.empty() && !a.png_dir.empty()) throw UsageError("--png and --png-dir are one or the other");
  return a;
}

} // namespace ch
