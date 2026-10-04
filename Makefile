# libcharts and charts: ISO C89, libc and libm. No configure or C++ toolchain.
# GNU make (including the versions shipped with GCC 2.95-era systems).
CC       = cc
AR       = ar
RANLIB   = ranlib
CFLAGS   = -O2 -ansi -pedantic -Wall -W
CPPFLAGS = -Iinclude -Iapp
LDFLAGS  =
LDLIBS   = -lm
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin
LIBDIR   = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include
MANDIR   = $(PREFIX)/share/man/man1

LIBSRC = lib/core.c lib/util.c lib/gfx.c lib/canvas.c lib/scene.c \
         lib/render.c lib/chart.c lib/diagram.c lib/slide.c lib/output.c
APPSRC = app/model.c app/json.c app/spec.c app/data.c app/deck.c \
         app/bridge.c app/platform.c app/cli.c app/guide.c app/examples.c \
         app/sources.c app/tui.c app/tui_draw.c app/main.c
LIBOBJ = $(LIBSRC:.c=.o)
APPOBJ = $(APPSRC:.c=.o)

all: libcharts.a charts

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBOBJ): include/charts.h lib/internal.h lib/render.h
lib/gfx.o: lib/font.inc
$(APPOBJ): include/charts.h app/model.h app/os.h app/bridge.h app/platform.h app/cli.h app/tui.h app/tui_priv.h
app/main.o app/sources.o: app/sources.h
app/tui.o app/tui_draw.o: lib/render.h lib/internal.h

libcharts.a: $(LIBOBJ)
	rm -f $@
	$(AR) rc $@ $(LIBOBJ)
	$(RANLIB) $@

charts: $(APPOBJ) libcharts.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(APPOBJ) libcharts.a $(LDLIBS)

test: all test-lib
	./tests/smoke.sh ./charts
	@if test "`uname -s`" = Linux; then $(MAKE) test-platform; fi
	@if command -v python3 >/dev/null 2>&1; then python3 tests/tui.py "$(CURDIR)/charts" "$(CURDIR)/examples"; else echo "python3 unavailable: skipped PTY tests"; fi

test-lib: libcharts.a
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o build/test-core tests/core.c libcharts.a $(LDLIBS)
	./build/test-core
	$(CC) $(CPPFLAGS) $(CFLAGS) -o build/test-output tests/output.c libcharts.a $(LDLIBS)
	./build/test-output

test-stress: libcharts.a
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o build/test-chart-alloc tests/chart_alloc.c libcharts.a $(LDLIBS)
	./build/test-chart-alloc
	$(CC) $(CPPFLAGS) $(CFLAGS) -o build/test-chart-extremes tests/chart_extremes.c libcharts.a $(LDLIBS)
	./build/test-chart-extremes

SAN_CC = clang
asan:
	$(SAN_CC) $(CPPFLAGS) -std=c89 -pedantic -O1 -g -Wall -Wextra \
	  -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -o charts-asan $(APPSRC) $(LIBSRC) $(LDLIBS)

test-asan: asan
	./tests/smoke.sh ./charts-asan
	ASAN_OPTIONS=detect_leaks=1 python3 tests/tui.py "$(CURDIR)/charts-asan" "$(CURDIR)/examples"

static:
	$(CC) $(CPPFLAGS) $(CFLAGS) -static $(LDFLAGS) -o charts-static $(APPSRC) $(LIBSRC) $(LDLIBS)

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(MANDIR)
	cp charts $(DESTDIR)$(BINDIR)/charts
	cp libcharts.a $(DESTDIR)$(LIBDIR)/libcharts.a
	cp include/charts.h include/libcharts.h $(DESTDIR)$(INCLUDEDIR)/
	cp docs/charts.1 $(DESTDIR)$(MANDIR)/charts.1
	chmod 755 $(DESTDIR)$(BINDIR)/charts

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/charts $(DESTDIR)$(LIBDIR)/libcharts.a $(DESTDIR)$(INCLUDEDIR)/charts.h $(DESTDIR)$(INCLUDEDIR)/libcharts.h $(DESTDIR)$(MANDIR)/charts.1

clean:
	rm -f $(LIBOBJ) $(APPOBJ) libcharts.a charts charts-asan charts-static build/test-core build/test-output build/test-platform build/test-save
	rm -f build/test-chart-alloc build/test-chart-extremes build/oracle-*.o build/oracle.a build/*-compare build/charts-oracle

run: all
	./charts examples/revenue.csv -t pie3d

# Historical implementation is retained only as a differential test oracle.
oracle:
	mkdir -p build
	$(CXX) -std=c++17 -O2 -o build/charts-oracle src/*.cpp

SKILLDIR = $(HOME)/.agents/skills
install-skill: skill
	mkdir -p $(SKILLDIR)/charts
	cp -r skill/charts/. $(SKILLDIR)/charts/

gallery: all
	./charts skill/charts/assets/gallery/deck.json --check
	./charts skill/charts/assets/gallery/deck.json --png-dir skill/charts/assets/gallery/png

skill: all gallery
	@{ cat skill/charts/frontmatter.md; echo '```text'; ./charts -h; echo '```'; } > skill/charts/SKILL.md
	cp docs/DECK.md skill/charts/references/deck-format.md
	cp examples/demo/* skill/charts/assets/example-deck/

.PHONY: all test test-lib test-stress asan test-asan static install uninstall clean run oracle install-skill gallery skill

# Optional migration oracle: C++ is used only for comparison with the frozen
# original source, never to build the library or application.
ORACLESRC = $(wildcard src/*.cpp)
ORACLEOBJ = $(ORACLESRC:src/%.cpp=build/oracle-%.o)
CXX = c++
CXXFLAGS = -std=c++17 -O2
build/oracle-%.o: src/%.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@
build/oracle.a: $(ORACLEOBJ)
	rm -f $@
	$(AR) rc $@ $(ORACLEOBJ)
	$(RANLIB) $@
build/%-compare: tests/%_compare.cpp libcharts.a build/oracle.a
	$(CXX) $(CXXFLAGS) -I. $< build/oracle.a libcharts.a $(LDLIBS) -o $@
build/charts-oracle: $(ORACLEOBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(ORACLEOBJ)
test-oracle: build/core-compare build/chart-compare build/diagram-compare build/slide-compare build/output-compare charts build/charts-oracle
	./tests/png_parity.sh ./charts ./build/charts-oracle
	./build/core-compare
	./build/chart-compare
	./build/diagram-compare
	./build/slide-compare
	./build/output-compare

test-platform: libcharts.a
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o build/test-platform tests/platform.c app/platform.c app/model.c libcharts.a $(LDLIBS)
	./build/test-platform
	$(CC) $(CPPFLAGS) $(CFLAGS) -o build/test-save tests/dossave.c app/platform.c app/model.c libcharts.a $(LDLIBS)
	cd build && ./test-save

.PHONY: test-oracle test-platform
