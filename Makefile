# charts-dot-exe -- ANSI chart renderer for the Linux console (TTY / DRM fbcon)
#
#   make            build ./charts
#   make asan       build ./charts-asan (ASan + UBSan) for testing
#   make test       build + run tests/smoke.sh
#   make install    install to $(PREFIX)/bin
#   make static     try a fully static binary (needs libstdc++.a)
#   make clean

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?=
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin
MANDIR   ?= $(PREFIX)/share/man/man1

SRC   := $(wildcard src/*.cpp)
OBJ   := $(SRC:src/%.cpp=build/%.o)
DEP   := $(OBJ:.o=.d)
BIN   := charts

.PHONY: all asan test install install-skill uninstall static clean run skill gallery

all: $(BIN)

build:
	@mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# Instrumented build for the test run. Sanitizer runtimes move around: gcc needs
# libasan from the distro, clang carries its own in compiler-rt. Use whichever
# compiler can actually link a sanitized binary.
SAN_CXX ?= $(shell printf 'int main(){return 0;}' > /tmp/.san_probe.c 2>/dev/null; \
  for c in clang++ g++; do \
    if command -v $$c >/dev/null 2>&1 && \
       $$c -fsanitize=address -x c++ /tmp/.san_probe.c -o /tmp/.san_probe.bin >/dev/null 2>&1; then \
      echo $$c; break; \
    fi; \
  done; rm -f /tmp/.san_probe.c /tmp/.san_probe.bin)

asan: | build
	@test -n "$(SAN_CXX)" || { echo "no compiler here can link -fsanitize=address (install libasan, or clang)"; exit 1; }
	@echo "sanitizer build with $(SAN_CXX)"
	$(SAN_CXX) -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o charts-asan $(SRC)

# The pty tests need python3; without it the CLI tests still run.
test: all
	./tests/smoke.sh ./$(BIN)
	@if command -v python3 >/dev/null 2>&1; then \
		python3 ./tests/tui.py "$(CURDIR)/$(BIN)" "$(CURDIR)/examples"; \
	else echo "(no python3: skipping the interactive pty tests)"; fi

test-asan: asan
	./tests/smoke.sh ./charts-asan
	@if command -v python3 >/dev/null 2>&1; then \
		ASAN_OPTIONS=detect_leaks=1 python3 ./tests/tui.py "$(CURDIR)/charts-asan" \
			"$(CURDIR)/examples"; \
	else echo "(no python3: skipping the interactive pty tests)"; fi

install: all
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 docs/charts.1 $(DESTDIR)$(MANDIR)/charts.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN) $(DESTDIR)$(MANDIR)/charts.1

static: | build
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -static \
		-o charts-static $(SRC) $(LDFLAGS)

run: all
	./$(BIN) examples/revenue.csv -t pie3d

clean:
	rm -rf build $(BIN) charts-asan charts-static

-include $(DEP)

# The agent skill: ~/.agents/skills/charts (override SKILLDIR for another agent)
SKILLDIR ?= $(HOME)/.agents/skills
install-skill: skill
	mkdir -p $(SKILLDIR)/charts
	cp -r skill/charts/. $(SKILLDIR)/charts/

# The gallery's pictures are rendered, never committed: they cannot go stale.
gallery: all
	@./$(BIN) skill/charts/assets/gallery/deck.json --check
	@./$(BIN) skill/charts/assets/gallery/deck.json --png-dir skill/charts/assets/gallery/png > /dev/null
	@echo "skill/charts/assets/gallery/png rendered"

# SKILL.md is `charts -h` with a front page: one source of truth.
skill: all gallery
	@{ cat skill/charts/frontmatter.md; echo '```text'; ./$(BIN) -h; echo '```'; } > skill/charts/SKILL.md
	@cp docs/DECK.md skill/charts/references/deck-format.md
	@echo "skill/charts: SKILL.md from charts -h, references/deck-format.md from docs/DECK.md"
