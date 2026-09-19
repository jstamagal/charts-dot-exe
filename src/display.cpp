#include "display.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include "term.hpp"
#include "ttyguard.hpp"
#include "util.hpp"

namespace ch {

namespace {

void write_all(int fd, const char *p, std::size_t n) {
  while (n > 0) {
    ssize_t w = ::write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      return;
    }
    p += w;
    n -= static_cast<std::size_t>(w);
  }
}
void write_all(int fd, const std::string &s) { write_all(fd, s.data(), s.size()); }

int auto_scale(int px_w) { return std::max(1, static_cast<int>((px_w + 480) / 960)); }

std::string env(const char *name) {
  const char *v = std::getenv(name);
  return v ? v : "";
}

// ---- cells -------------------------------------------------------------------

class CellDisplay : public Display {
public:
  CellDisplay(bool ascii, bool color) : ascii_(ascii), color_(color) {
    bright_bg_ = env("TERM") != "linux";
  }
  std::string name() const override { return ascii_ ? "ascii" : "cells"; }
  Mode mode() const override {
    Mode m;
    m.ascii = ascii_;
    m.color = color_;
    return m;
  }
  void grid(int &cols, int &rows) override {
    TermSize ts = term_size();
    cols = ts.w;
    rows = ts.h;
  }
  void show(const Scene &sc) override {
    // Synchronized output where the terminal knows it; ignored where not.
    std::string out = "\x1b[?2026h\x1b[H";
    out += sc.to_cells().dump(color_, true, bright_bg_);
    // the dump ends with a newline that would scroll the last row away
    if (out.size() >= 2 && out.compare(out.size() - 2, 2, "\r\n") == 0) out.resize(out.size() - 2);
    out += "\x1b[?2026l";
    write_all(STDOUT_FILENO, out);
  }

private:
  bool ascii_, color_, bright_bg_ = true;
};

// ---- framebuffer -------------------------------------------------------------

// The VT is put in graphics mode so the kernel's console stops painting over
// us, and in process-controlled switching so Alt-Fn still works: we let go of
// the screen when asked and repaint when it comes back.
int g_vt_fd = -1;
volatile sig_atomic_t g_vt_active = 1;
volatile sig_atomic_t g_vt_redraw = 0;
bool g_vt_graphics = false;

void vt_release(int) {
  g_vt_active = 0;
  if (g_vt_fd >= 0) ::ioctl(g_vt_fd, VT_RELDISP, 1);
}
void vt_acquire(int) {
  if (g_vt_fd >= 0) ::ioctl(g_vt_fd, VT_RELDISP, VT_ACKACQ);
  g_vt_active = 1;
  g_vt_redraw = 1;
}
// Graphics mode and process-controlled switching: taking the VT, and taking it
// back after a stop.  ioctl() only, so a signal handler may call it.
void vt_take() {
  if (g_vt_fd < 0) return;
  struct vt_mode vm;
  std::memset(&vm, 0, sizeof vm);
  vm.mode = VT_PROCESS;
  vm.relsig = SIGUSR1;
  vm.acqsig = SIGUSR2;
  ::ioctl(g_vt_fd, VT_SETMODE, &vm);
  if (::ioctl(g_vt_fd, KDSETMODE, KD_GRAPHICS) == 0) g_vt_graphics = true;
  g_vt_active = 1;
  g_vt_redraw = 1;
}
void vt_restore() {
  if (g_vt_fd < 0) return;
  struct vt_mode vm;
  std::memset(&vm, 0, sizeof vm);
  vm.mode = VT_AUTO;
  ::ioctl(g_vt_fd, VT_SETMODE, &vm);
  if (g_vt_graphics) ::ioctl(g_vt_fd, KDSETMODE, KD_TEXT);
  g_vt_graphics = false;
}

class FbDisplay : public Display {
public:
  ~FbDisplay() override { close(); }

  bool open(int scale, std::string &err) {
    std::string dev = env("CHARTS_FB");
    if (dev.empty()) dev = env("FRAMEBUFFER");
    if (dev.empty()) dev = "/dev/fb0";
    fd_ = ::open(dev.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
      err = dev + ": " + std::strerror(errno) + (errno == EACCES ? " (add yourself to the 'video' group)" : "");
      return false;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    std::memset(&var, 0, sizeof var);
    std::memset(&fix, 0, sizeof fix);
    std::string geom = env("CHARTS_FB_GEOM"); // WxHxBPP: a plain file stands in for the device (tests)
    if (!geom.empty()) {
      int w = 0, h = 0, bpp = 32;
      if (std::sscanf(geom.c_str(), "%dx%dx%d", &w, &h, &bpp) < 2 || w < 64 || h < 64) {
        err = "bad CHARTS_FB_GEOM";
        return false;
      }
      var.xres = var.xres_virtual = static_cast<unsigned>(w);
      var.yres = var.yres_virtual = static_cast<unsigned>(h);
      var.bits_per_pixel = static_cast<unsigned>(bpp);
      if (bpp == 16) {
        var.red.offset = 11; var.red.length = 5;
        var.green.offset = 5; var.green.length = 6;
        var.blue.offset = 0; var.blue.length = 5;
      } else {
        var.red.offset = 16; var.green.offset = 8; var.blue.offset = 0;
        var.red.length = var.green.length = var.blue.length = 8;
      }
      fix.line_length = static_cast<unsigned>(w * (bpp / 8));
      fake_ = true;
    } else if (::ioctl(fd_, FBIOGET_VSCREENINFO, &var) != 0 || ::ioctl(fd_, FBIOGET_FSCREENINFO, &fix) != 0) {
      err = dev + ": not a framebuffer";
      return false;
    }
    if (var.bits_per_pixel != 32 && var.bits_per_pixel != 24 && var.bits_per_pixel != 16) {
      err = dev + ": " + std::to_string(var.bits_per_pixel) + " bits per pixel is not supported";
      return false;
    }
    xres_ = static_cast<int>(var.xres);
    yres_ = static_cast<int>(var.yres);
    bytes_ = static_cast<int>(var.bits_per_pixel / 8);
    stride_ = static_cast<int>(fix.line_length);
    if (stride_ < xres_ * bytes_) stride_ = xres_ * bytes_;
    xoff_ = static_cast<int>(var.xoffset);
    yoff_ = static_cast<int>(var.yoffset);
    for (int i = 0; i < 16; i++) {
      auto chan = [&](uint8_t v, const fb_bitfield &f) -> uint32_t {
        return f.length == 0 ? 0 : (static_cast<uint32_t>(v) >> (8 - std::min<uint32_t>(8, f.length))) << f.offset;
      };
      lut_[i] = chan(VGA_RGB[i][0], var.red) | chan(VGA_RGB[i][1], var.green) | chan(VGA_RGB[i][2], var.blue);
      if (var.transp.length) lut_[i] |= ((1u << var.transp.length) - 1) << var.transp.offset;
    }
    scale_ = scale > 0 ? scale : auto_scale(xres_);
    while (scale_ > 1 && (xres_ / (8 * scale_) < 60 || yres_ / (16 * scale_) < 16)) scale_--;

    if (!fake_) take_vt();
    return true;
  }

  std::string name() const override { return "fb"; }
  Mode mode() const override {
    Mode m;
    m.pixel = true;
    return m;
  }
  void grid(int &cols, int &rows) override {
    cols = xres_ / (8 * scale_);
    rows = yres_ / (16 * scale_);
  }
  bool needs_redraw() override {
    if (!g_vt_redraw) return false;
    g_vt_redraw = 0;
    return true;
  }

  void show(const Scene &sc) override {
    if (!g_vt_active) return; // someone else has the screen
    Image im = sc.to_image();
    const int k = scale_;
    const int iw = im.w * k, ih = im.h * k;
    const int ox = std::max(0, (xres_ - iw) / 2), oy = std::max(0, (yres_ - ih) / 2);
    const uint8_t border = S.slide_bg == BG_NONE ? 0 : S.slide_bg;
    frame_.assign(static_cast<std::size_t>(stride_) * yres_, 0);
    std::vector<uint8_t> line(static_cast<std::size_t>(stride_));
    for (int y = 0; y < yres_; y++) {
      int sy = (y - oy) / k;
      bool in_y = y >= oy && sy < im.h;
      // rows repeat k times: build each source row once
      if (in_y && y > oy && (y - oy) % k != 0) {
        std::memcpy(&frame_[static_cast<std::size_t>(y) * stride_], &frame_[static_cast<std::size_t>(y - 1) * stride_],
                    static_cast<std::size_t>(stride_));
        continue;
      }
      for (int x = 0; x < xres_; x++) {
        int sx = (x - ox) / k;
        uint8_t c = (in_y && x >= ox && sx < im.w) ? im.get(sx, sy) : border;
        put_px(&line[static_cast<std::size_t>(x) * bytes_], lut_[c & 15]);
      }
      std::memcpy(&frame_[static_cast<std::size_t>(y) * stride_], line.data(), static_cast<std::size_t>(stride_));
    }
    // pwrite rather than mmap: it works on every fbdev driver, including the
    // DRM emulation with deferred I/O, and one call is one damage event.
    off_t base = static_cast<off_t>(yoff_) * stride_ + static_cast<off_t>(xoff_) * bytes_;
    std::size_t total = frame_.size(), done = 0;
    while (done < total) {
      ssize_t w = ::pwrite(fd_, frame_.data() + done, total - done, base + static_cast<off_t>(done));
      if (w <= 0) {
        if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        break;
      }
      done += static_cast<std::size_t>(w);
    }
  }

  void close() override {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    if (took_vt_) {
      tty_guard_set_cleanup(nullptr);
      tty_guard_set_resume(nullptr);
      vt_restore();
      g_vt_fd = -1;
      took_vt_ = false;
      // the console repaints its own text once it is back in KD_TEXT
    }
  }

private:
  void put_px(uint8_t *p, uint32_t v) const {
    if (bytes_ == 4) std::memcpy(p, &v, 4);
    else if (bytes_ == 2) { uint16_t s = static_cast<uint16_t>(v); std::memcpy(p, &s, 2); }
    else { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); p[2] = static_cast<uint8_t>(v >> 16); }
  }

  void take_vt() {
    int mode = 0;
    if (::ioctl(STDIN_FILENO, KDGETMODE, &mode) != 0) return; // not a VT (ssh, a pty): just draw
    g_vt_fd = STDIN_FILENO;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = vt_release;
    ::sigaction(SIGUSR1, &sa, nullptr);
    sa.sa_handler = vt_acquire;
    ::sigaction(SIGUSR2, &sa, nullptr);
    vt_take();
    g_vt_redraw = 0;
    tty_guard_set_cleanup(vt_restore);
    tty_guard_set_resume(vt_take);
    took_vt_ = true;
  }

  int fd_ = -1;
  bool fake_ = false, took_vt_ = false;
  int xres_ = 0, yres_ = 0, bytes_ = 4, stride_ = 0, xoff_ = 0, yoff_ = 0, scale_ = 1;
  uint32_t lut_[16] = {};
  std::vector<uint8_t> frame_;
};

// ---- terminal graphics -------------------------------------------------------

struct TermPixels {
  int cols = 0, rows = 0, cw = 0, chh = 0;
  bool ok() const { return cw > 0 && chh > 0; }
};

TermPixels term_pixels() {
  TermPixels t;
  struct winsize ws;
  std::memset(&ws, 0, sizeof ws);
  for (int fd : {STDOUT_FILENO, STDIN_FILENO})
    if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) break;
  t.cols = ws.ws_col;
  t.rows = ws.ws_row;
  if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0 && t.cols > 0 && t.rows > 0) {
    t.cw = ws.ws_xpixel / t.cols;
    t.chh = ws.ws_ypixel / t.rows;
  }
  return t;
}

std::string base64(const std::string &in) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) | static_cast<uint8_t>(in[i + 2]);
    out += T[v >> 18];
    out += T[(v >> 12) & 63];
    out += T[(v >> 6) & 63];
    out += T[v & 63];
  }
  if (i < in.size()) {
    uint32_t v = static_cast<uint8_t>(in[i]) << 16;
    if (i + 1 < in.size()) v |= static_cast<uint8_t>(in[i + 1]) << 8;
    out += T[v >> 18];
    out += T[(v >> 12) & 63];
    out += i + 1 < in.size() ? T[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

// Shared by kitty and sixel: an image the size of the window, in whole cells.
class TermGfx : public Display {
public:
  explicit TermGfx(int scale) : want_scale_(scale) {}
  Mode mode() const override {
    Mode m;
    m.pixel = true;
    return m;
  }
  void grid(int &cols, int &rows) override {
    tp_ = term_pixels();
    if (!tp_.ok()) { cols = 100; rows = 30; scale_ = 1; return; }
    int pw = tp_.cols * tp_.cw, ph = (tp_.rows - reserve_rows()) * tp_.chh;
    scale_ = want_scale_ > 0 ? want_scale_ : auto_scale(pw);
    while (scale_ > 1 && (pw / (8 * scale_) < 60 || ph / (16 * scale_) < 16)) scale_--;
    cols = std::max(20, pw / (8 * scale_));
    rows = std::max(6, ph / (16 * scale_));
  }
  void close() override {
    if (closed_) return;
    closed_ = true;
    write_all(STDOUT_FILENO, cleanup() + "\x1b[0m\x1b[2J\x1b[H");
  }
  ~TermGfx() override {}

protected:
  void invalidate() override { // the backdrop goes down again with the next frame
    last_w_ = last_h_ = 0;
    last_bg_ = 0xFE;
  }
  virtual int reserve_rows() const { return 0; }
  virtual std::string cleanup() const { return ""; }

  // Clear to the slide colour so the margin around the image matches it.
  std::string backdrop() const {
    const uint8_t *c = VGA_RGB[S.slide_bg == BG_NONE ? 0 : S.slide_bg];
    char b[64];
    std::snprintf(b, sizeof b, "\x1b[48;2;%d;%d;%dm\x1b[2J\x1b[0m", c[0], c[1], c[2]);
    return b;
  }
  // Cursor to where a w x h pixel image sits centred.
  std::string centre(int w, int h) const {
    int col = std::max(0, (tp_.cols * tp_.cw - w) / 2 / std::max(1, tp_.cw));
    int row = std::max(0, ((tp_.rows - reserve_rows()) * tp_.chh - h) / 2 / std::max(1, tp_.chh));
    char b[32];
    std::snprintf(b, sizeof b, "\x1b[%d;%dH", row + 1, col + 1);
    return b;
  }

  int want_scale_ = 0, scale_ = 1;
  TermPixels tp_;
  bool closed_ = false;
  int last_w_ = 0, last_h_ = 0;
  uint8_t last_bg_ = 0xFE;
};

class KittyDisplay : public TermGfx {
public:
  explicit KittyDisplay(int scale) : TermGfx(scale) { raw_ = !env("CHARTS_KITTY_RAW").empty(); }
  std::string name() const override { return "kitty"; }

  void show(const Scene &sc) override {
    Image im = sc.to_image().scaled(scale_);
    std::string out;
    if (im.w != last_w_ || im.h != last_h_ || S.slide_bg != last_bg_) {
      out += backdrop();
      last_w_ = im.w;
      last_h_ = im.h;
      last_bg_ = S.slide_bg;
    }
    out += centre(im.w, im.h);
    int id = 1 + (frame_++ & 1);
    std::string payload, head;
    char b[128];
    if (raw_) {
      std::string rgb;
      rgb.reserve(static_cast<std::size_t>(im.w) * im.h * 3);
      for (uint8_t c : im.px) rgb.append(reinterpret_cast<const char *>(VGA_RGB[c & 15]), 3);
      payload = base64(zlib_compress(rgb));
      std::snprintf(b, sizeof b, "a=T,f=24,o=z,s=%d,v=%d,i=%d,p=1,q=2,C=1", im.w, im.h, id);
    } else {
      payload = base64(png_encode(im));
      std::snprintf(b, sizeof b, "a=T,f=100,i=%d,p=1,q=2,C=1", id);
    }
    head = b;
    // chunks of at most 4096 base64 bytes; only the first carries the keys
    for (std::size_t off = 0; off < payload.size() || off == 0;) {
      std::size_t n = std::min<std::size_t>(4096, payload.size() - off);
      bool more = off + n < payload.size();
      out += "\x1b_G";
      if (off == 0) out += head + ",";
      out += more ? "m=1;" : "m=0;";
      out.append(payload, off, n);
      out += "\x1b\\";
      off += n;
      if (n == 0) break;
    }
    // the new frame is up: drop the one underneath
    std::snprintf(b, sizeof b, "\x1b_Ga=d,d=I,i=%d,q=2\x1b\\", 1 + (frame_ & 1));
    out += b;
    write_all(STDOUT_FILENO, out);
  }

protected:
  std::string cleanup() const override { return "\x1b_Ga=d,d=A,q=2\x1b\\"; }

private:
  bool raw_ = false;
  unsigned frame_ = 0;
};

class SixelDisplay : public TermGfx {
public:
  explicit SixelDisplay(int scale) : TermGfx(scale) {}
  std::string name() const override { return "sixel"; }

  void show(const Scene &sc) override {
    Image im = sc.to_image().scaled(scale_);
    std::string out = "\x1b[?2026h";
    if (im.w != last_w_ || im.h != last_h_ || S.slide_bg != last_bg_) {
      out += backdrop();
      last_w_ = im.w;
      last_h_ = im.h;
      last_bg_ = S.slide_bg;
    }
    out += centre(im.w, im.h);
    out += "\x1bP0;1;0q\"1;1;" + std::to_string(im.w) + ";" + std::to_string(im.h);
    for (int i = 0; i < 16; i++) {
      char b[48];
      std::snprintf(b, sizeof b, "#%d;2;%d;%d;%d", i, VGA_RGB[i][0] * 100 / 255, VGA_RGB[i][1] * 100 / 255,
                    VGA_RGB[i][2] * 100 / 255);
      out += b;
    }
    std::vector<uint8_t> six(static_cast<std::size_t>(im.w));
    for (int y0 = 0; y0 < im.h; y0 += 6) {
      unsigned present = 0;
      int rows = std::min(6, im.h - y0);
      for (int y = 0; y < rows; y++)
        for (int x = 0; x < im.w; x++) present |= 1u << im.get(x, y0 + y);
      bool first = true;
      for (int c = 0; c < 16; c++) {
        if (!(present & (1u << c))) continue;
        std::fill(six.begin(), six.end(), 0);
        for (int y = 0; y < rows; y++) {
          const uint8_t *row = &im.px[static_cast<std::size_t>(y0 + y) * im.w];
          for (int x = 0; x < im.w; x++)
            if (row[x] == c) six[static_cast<std::size_t>(x)] |= static_cast<uint8_t>(1 << y);
        }
        if (!first) out += '$';
        first = false;
        out += '#';
        out += std::to_string(c);
        int end = im.w;
        while (end > 0 && six[static_cast<std::size_t>(end - 1)] == 0) end--; // trailing blanks need not be sent
        for (int x = 0; x < end;) {
          int run = 1;
          while (x + run < end && six[static_cast<std::size_t>(x + run)] == six[static_cast<std::size_t>(x)]) run++;
          char ch = static_cast<char>(63 + six[static_cast<std::size_t>(x)]);
          if (run > 3) { out += '!'; out += std::to_string(run); out += ch; }
          else out.append(static_cast<std::size_t>(run), ch);
          x += run;
        }
      }
      out += '-';
    }
    out += "\x1b\\\x1b[?2026l";
    write_all(STDOUT_FILENO, out);
  }

protected:
  // A sixel image that reaches the last row scrolls the screen.
  int reserve_rows() const override { return 1; }
};

// ---- asking the terminal what it can do ---------------------------------------

// Send a query and collect the reply up to the DA1 answer ("...c"), which
// every terminal sends, so there is never a wait for a timeout on a terminal
// that simply does not know the other question.
std::string ask_terminal(const std::string &query, int timeout_ms) {
  if (!is_tty(STDIN_FILENO) || !is_tty(STDOUT_FILENO)) return "";
  RawMode raw(STDIN_FILENO);
  if (!raw.ok()) return "";
  write_all(STDOUT_FILENO, query);
  std::string reply;
  for (;;) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) break;
    char c;
    if (::read(STDIN_FILENO, &c, 1) != 1) break;
    reply += c;
    std::size_t da = reply.rfind("\x1b[?");
    if (c == 'c' && da != std::string::npos) break;
    if (reply.size() > 512) break;
  }
  return reply;
}

} // namespace

bool gfx_name_valid(const std::string &n) {
  return n == "auto" || n == "fb" || n == "kitty" || n == "sixel" || n == "cells" || n == "ascii";
}

std::unique_ptr<Display> open_display(const DisplayOpts &o, std::string &why) {
  std::string want = o.want;
  if (want == "auto" && !env("CHARTS_GFX").empty() && gfx_name_valid(env("CHARTS_GFX"))) want = env("CHARTS_GFX");
  const std::string term = env("TERM");

  auto cells = [&](bool ascii) { return std::unique_ptr<Display>(new CellDisplay(ascii, o.color)); };
  if (want == "ascii") { why = "ascii: asked for"; return cells(true); }
  if (want == "cells") { why = "cells: asked for"; return cells(false); }
  if (!o.color) { why = "cells: colour is off, and pixels are nothing but colour"; return cells(false); }

  if (want == "fb" || (want == "auto" && (term == "linux" || !env("CHARTS_FB").empty()))) {
    std::unique_ptr<FbDisplay> fb(new FbDisplay());
    std::string err;
    if (fb->open(o.scale, err)) { why = "fb: the console framebuffer"; return fb; }
    why = "cells: no framebuffer (" + err + ")";
    return cells(false);
  }
  if (want == "kitty") { why = "kitty: asked for"; return std::unique_ptr<Display>(new KittyDisplay(o.scale)); }
  if (want == "sixel") { why = "sixel: asked for"; return std::unique_ptr<Display>(new SixelDisplay(o.scale)); }

  // auto, inside some terminal emulator
  if (!env("TMUX").empty() || starts_with(term, "screen") || starts_with(term, "tmux")) {
    why = "cells: inside tmux/screen, which does not pass graphics through";
    return cells(false);
  }
  if (!term_pixels().ok()) { why = "cells: the terminal does not report its pixel size"; return cells(false); }
  std::string reply = ask_terminal("\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c", 250);
  if (reply.find("_Gi=31;OK") != std::string::npos) {
    why = "kitty: the terminal answered the graphics query";
    return std::unique_ptr<Display>(new KittyDisplay(o.scale));
  }
  std::size_t da = reply.rfind("\x1b[?");
  if (da != std::string::npos) {
    std::string attrs = ";" + reply.substr(da + 3);
    for (char &c : attrs)
      if (c == 'c') c = ';';
    if (attrs.find(";4;") != std::string::npos) {
      why = "sixel: the terminal lists sixel in its device attributes";
      return std::unique_ptr<Display>(new SixelDisplay(o.scale));
    }
  }
  why = "cells: the terminal offers neither kitty graphics nor sixel";
  return cells(false);
}

} // namespace ch
