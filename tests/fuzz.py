#!/usr/bin/env python3
"""tests/fuzz.py -- random and mutated decks and data files, thrown at charts.

Every run must exit 0, 1 or 2: never a signal, never a sanitizer report, and
--check --json must stay valid JSON whatever the deck said.  smoke.sh runs it
with a fixed seed; run it alone to dig:

  python3 tests/fuzz.py ./charts-asan /tmp/fuzz examples
  CHARTS_FUZZ_N=5000 CHARTS_FUZZ_SEED=7 CHARTS_FUZZ_KEEP=/tmp/bad python3 tests/fuzz.py ...
"""
import json, os, random, subprocess, sys

# A sanitizer report must not pass for an ordinary "bad input" exit 1.  Yours
# come after, so they still win.
os.environ["ASAN_OPTIONS"] = "exitcode=99:abort_on_error=0:" + os.environ.get("ASAN_OPTIONS", "")
os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:exitcode=99:print_stacktrace=1:" + os.environ.get("UBSAN_OPTIONS", "")

# absolute: every run happens inside outdir
binary, outdir, examples = (os.path.abspath(a) for a in sys.argv[1:4])
N = int(os.environ.get("CHARTS_FUZZ_N", "220"))
rng = random.Random(int(os.environ.get("CHARTS_FUZZ_SEED", "20260917")))
os.makedirs(outdir, exist_ok=True)
for f in ("revenue.csv", "quarterly.csv"):
    open(os.path.join(outdir, f), "wb").write(open(os.path.join(examples, f), "rb").read())

TYPES = ["bar", "stacked", "hbar", "dumbbell", "line", "area", "pie", "pie3d", "donut", "scatter", "hist", "table",
         "column", "xy", "", "barr", "PIE", 7, None]
WORDS = ["", "a", "Apr", "revenue", "日本語", "🎉🎉", "x" * 300, "\x1b[2J", "nan", "-", "0", "‮", "a\nb", "\t", "é" * 40]
NUMS = [0, 1, -1, 0.5, 1e308, -1e308, 1e-300, 2**53, -2**63, 2**64, 12345678901234567890, 1e400, -0.0, 3, 42, 100, 99999]

def word(): return rng.choice(WORDS)
def num():
    r = rng.random()
    if r < 0.6: return rng.choice(NUMS)
    if r < 0.8: return rng.uniform(-1e6, 1e6)
    return rng.choice(["nan", "NaN", "inf", "-inf", "1e999", "", "12abc", None, True, [], {}])

def junk(depth=0):
    r = rng.random()
    if depth > 3 or r < 0.35: return rng.choice([None, True, False, num(), word(), -1, 1e400])
    if r < 0.65: return [junk(depth + 1) for _ in range(rng.randrange(0, 4))]
    return {word() or "k": junk(depth + 1) for _ in range(rng.randrange(0, 4))}

def inline():
    n = rng.choice([0, 1, 2, 3, 12, 60])
    shape = rng.randrange(7)
    if shape == 0: return {"labels": [word() for _ in range(n)], "series": [{"name": word(), "values": [num() for _ in range(rng.choice([n, n, 0, n + 3]))]} for _ in range(rng.randrange(0, 4))]}
    if shape == 1: return [{"label": word(), "a": num(), "b": num()} for _ in range(n)]
    if shape == 2: return {(word() or str(i)) + str(i): num() for i in range(n)}
    if shape == 3: return {"x": [num() for _ in range(n)], "y": [num() for _ in range(rng.choice([n, n + 1, 0]))]}
    if shape == 4: return [["", "p", "q"]] + [[word(), num(), num()] for _ in range(n)]
    if shape == 5: return rng.choice(["revenue.csv", "quarterly.csv", "missing.csv", "", ".", "/", "../" * 9 + "etc/passwd", "/dev/null"])
    return junk()

def annotations():
    out = []
    for _ in range(rng.randrange(0, 5)):
        k = rng.randrange(5)
        if k == 0: out.append({"at": rng.choice(["Apr", "Q1", 0, -1, 10**9, 2.5, word()]), "series": word(), "text": word()})
        elif k == 1: out.append({"y": num(), "text": word()})
        elif k == 2: out.append({"x": rng.choice(["Apr", 3, -7, word()]), "text": word()})
        elif k == 3: out.append({"note": [num(), num()][:rng.randrange(0, 3)], "text": word(), "color": rng.choice(["red", 99, -1, "nope"])})
        else: out.append(junk())
    return out

def chart():
    b = {"data": inline()}
    for k, v in (("type", rng.choice(TYPES)), ("title", word()), ("values", rng.choice([True, False, "yes", 3])),
                 ("depth", rng.choice([0, 3, 6, 7, -1, 1e9, "2"])), ("explode", rng.choice([0, True, -5, 10**6, "big"])),
                 ("min", num()), ("max", num()), ("prec", rng.choice([0, 2, 50, -3])), ("bins", rng.choice([1, 0, -2, 10**7, 8])),
                 ("palette", rng.choice(["cga", "mono", "nope", 3])), ("frame", rng.choice(["double", "none", "zig"])),
                 ("xy", rng.choice([True, False])), ("transpose", rng.choice([True, False])), ("annotations", annotations()),
                 ("errors", rng.choice(["b", "q", ["a", "b"], {"a": "b"}, {"p": ["q", "p"]}, {"x": ["y"]}, 3, [], {"": ""}])),
                 ("series_col", rng.choice([1, 2, 3, 9, 0, "x"])),
                 ("weight", rng.choice([1, 2, 0, -1, 1e9, "x"])), ("at", [rng.choice([0, 3, 6, 11, 12, 13, -1, 1e9, "a"]) for _ in range(rng.choice([4, 4, 3, 5]))])):
        if rng.random() < 0.35: b[k] = v
    return b

def shapes():
    out = []
    for _ in range(rng.randrange(0, 9)):
        k = rng.choice(["rect", "ellipse", "poly", "line", "arrow", "label", "nope"])
        if k in ("rect", "ellipse"): g = [rng.choice([0, 1, 5.5, 12, 13, -1, 1e9, 0.001, "a"]) for _ in range(rng.choice([4, 4, 3]))]
        elif k == "label": g = [num(), num()][:rng.choice([2, 2, 1])]
        else: g = [[rng.uniform(-2, 14), rng.uniform(-2, 14)] for _ in range(rng.choice([0, 1, 2, 3, 7, 60]))]
        sh = {k: g}
        for kk, v in (("text", word()), ("color", rng.choice(["red", 99, "nope", 3])), ("depth", rng.choice([0, 6, 9, "x"])),
                      ("dither", rng.choice([0, 3, 4])), ("shadow", rng.choice([True, 0])), ("width", rng.choice([1, 4, 99])),
                      ("dash", True), ("head", rng.choice(["both", "zz", True, "start"])), ("size", rng.choice([1, 3, 0])),
                      ("fill", rng.choice([False, "none", 1])), ("border", rng.choice(["white", "none", 77])), ("align", rng.choice(["right", "up"]))):
            if rng.random() < 0.4: sh[kk] = v
        out.append(sh if rng.random() < 0.9 else junk())
    return out

def flow():
    names = [word() or "n%d" % i for i in range(rng.choice([0, 1, 2, 5, 12, 30]))]
    b = {"flow": [n if rng.random() < 0.7 else {"id": n, "text": word(), "color": rng.choice(["red", 42])} for n in names]}
    if rng.random() < 0.6 and names:
        b["edges"] = [[rng.choice(names + ["ghost"]), rng.choice(names), word()][:rng.choice([2, 3, 3, 1])] for _ in range(rng.randrange(0, 20))]
    if rng.random() < 0.3 and names: b["labels"] = {rng.choice(names + ["x"]) + rng.choice([">", "->", ""]) + rng.choice(names + ["y"]): word()}
    for kk, v in (("dir", rng.choice(["down", "right", "up"])), ("box", True), ("title", word())):
        if rng.random() < 0.4: b[kk] = v
    return b

def block(depth=0):
    r = rng.random()
    if r < 0.06: return {"shapes": shapes(), "box": rng.choice([True, False])}
    if r < 0.12: return flow()
    if r < 0.15: return {"like": rng.choice([1, 2, 0, -1, 99, 1.5, "slides[0]", "slides[1].blocks[0]", "slides[0].blocks[9]", "nope", None, []])}
    if depth < 8 and r < 0.25: return {rng.choice(["rows", "cols"]): [block(depth + 1) for _ in range(rng.randrange(0, 4))]}
    if r < 0.55: return chart()
    if r < 0.70: return {"text": rng.choice([word(), [word() for _ in range(rng.randrange(0, 6))], "# h\n## s\n- b\n> a\n**e**", 5]), "size": rng.choice([1, 4, 0, 99]), "align": rng.choice(["left", "center", "right", "up"]), "box": True}
    if r < 0.80: return {"bullets": [word() for _ in range(rng.randrange(0, 5))]}
    if r < 0.90: return {"stat": rng.choice([word(), 18, None]), "label": word(), "delta": rng.choice(["+3", "-3", "", 4])}
    return junk()

def deep(n):
    b = {"text": "bottom"}
    for i in range(n): b = {("rows" if i % 2 else "cols"): [b, {"stat": str(i)}]}
    return b

def slide():
    r = rng.random()
    if r < 0.15: return {"title": word(), "subtitle": word()}
    if r < 0.30:
        s = chart(); s["title"] = word(); return s
    if r < 0.35: return {"title": "deep", "blocks": [deep(rng.choice([5, 40, 400]))]}
    if r < 0.38: return {"title": word(), "like": rng.choice([1, 2, 3, "slides[0]", "slides[2].blocks[1]", 0, "x"]), "type": rng.choice(TYPES)}
    if r < 0.40: return junk()
    return {"title": word(), "layout": rng.choice(["auto", "cols", "rows", "grid", "spiral"]), "notes": word(),
            "blocks": [block() for _ in range(rng.randrange(0, 7))]}

def deck():
    return {"title": word(), "theme": rng.choice(["dos", "black", "light", "neon", 4]), "palette": rng.choice(["ega", "zzz"]),
            "slides": [slide() for _ in range(rng.randrange(0, 5))]}

def csv():
    delim = rng.choice([",", ";", "\t", "|", ",", ","])
    rows = []
    if rng.random() < 0.3: rows.append("#chart: " + rng.choice(["type=pie3d, title=\"X\", values", "type=", "type=zzz,,,=", "title=\"unterminated", "depth=99999999999, bins=-1, explode=x", "\x00"]))
    ncol = rng.choice([1, 2, 3, 3, 8])
    if rng.random() < 0.8: rows.append(delim.join(word().replace("\n", " ") or "h" for _ in range(ncol)))
    for _ in range(rng.choice([0, 1, 3, 12, 80])):
        rows.append(delim.join(str(rng.choice([num(), word().replace("\n", " "), '"q,"', '"'])) for _ in range(rng.choice([ncol, ncol, ncol + 2, 1]))))
    return rng.choice(["\n", "\r\n", "\n", "\r"]).join(rows)

def mutate(data):
    b = bytearray(data)
    r = rng.random()
    if r < 0.3 and b: return bytes(b[:rng.randrange(len(b))])                       # truncated
    for _ in range(rng.choice([1, 1, 3, 12])):
        if not b: break
        i = rng.randrange(len(b)); k = rng.randrange(4)
        if k == 0: b[i] = rng.randrange(256)
        elif k == 1: del b[i:i + rng.randrange(1, 9)]
        elif k == 2: b[i:i] = rng.choice([b"[", b"{", b'"', b"\\", b"\x00", b"1e999", b"-", b",", b"]]]]", b"\xff\xfe", b"\\ud800"])
        else: b[i:i] = b[i:i + rng.randrange(1, 40)]
    return bytes(b)

def run(args):
    try:
        p = subprocess.run([binary] + args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, timeout=60, cwd=outdir)
    except subprocess.TimeoutExpired:
        return "timeout after 60 s"
    err = p.stderr.decode("utf-8", "replace")
    if p.returncode not in (0, 1, 2): return "exit status %d\n%s" % (p.returncode, err[-1500:])
    if "AddressSanitizer" in err or "runtime error:" in err or "LeakSanitizer" in err: return "sanitizer report\n" + err[-1500:]
    return None

seeds = [open(os.path.join(examples, "deck.json"), "rb").read()]
failures, runs = [], 0
for i in range(N):
    kind = i % 4
    if kind == 0:   name, data = "fuzz-%03d.json" % i, json.dumps(deck(), ensure_ascii=rng.random() < 0.5).encode()
    elif kind == 1: name, data = "fuzz-%03d.json" % i, mutate(rng.choice(seeds))
    elif kind == 2: name, data = "fuzz-%03d.csv" % i, csv().encode("utf-8", "surrogatepass")
    else:           name, data = "fuzz-%03d.json" % i, json.dumps(rng.choice([junk(), inline(), {"chart": chart(), "rows": inline()}])).replace("NaN", rng.choice(["NaN", "null", "1e999"])).replace("Infinity", "1e999").encode()
    if kind == 0 and len(seeds) < 40: seeds.append(data)
    open(os.path.join(outdir, name), "wb").write(data)
    png = os.path.join(outdir, "out.png")
    if name.endswith(".csv") or kind == 3:
        t = str(rng.choice(TYPES[:13]) or "bar")
        jobs = [[name, "-t", t, "-w", str(rng.choice([20, 80, 3, 250])), "-H", str(rng.choice([6, 24, 2, 70])), "--values"],
                [name, "--describe"], [name, "--png", png, "--size", rng.choice(["120x33", "20x6", "64x20"])]]
    else:
        jobs = [[name, "--check"], [name, "--check", "--json"], [name, "--describe"],
                [name, "--print", "-w", str(rng.choice([100, 40, 20, 200])), "-H", str(rng.choice([30, 10, 6, 50]))],
                [name, "--png", png, "--slide", str(rng.choice([1, 1, 2, 3]))]]
    for args in jobs:
        runs += 1
        why = run(args)
        if why:
            failures.append((name, args, why))
            break
    # --check --json must stay valid JSON whatever the deck said
    if not name.endswith(".csv") and kind != 3:
        p = subprocess.run([binary, name, "--check", "--json"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=outdir)
        if p.returncode in (0, 1) and p.stdout.strip():
            try:
                d = json.loads(p.stdout.decode("utf-8"))
                assert isinstance(d.get("issues"), list) and d.get("ok") == (p.returncode == 0)
            except Exception as e:
                failures.append((name, ["--check", "--json"], "output is not the promised JSON: %r" % e))

keep = os.environ.get("CHARTS_FUZZ_KEEP")
for name, args, why in failures[:8]:
    print("  fuzz case %s: charts %s\n    %s" % (name, " ".join(args), why.replace("\n", "\n    ")))
    if keep:
        os.makedirs(keep, exist_ok=True)
        open(os.path.join(keep, name), "wb").write(open(os.path.join(outdir, name), "rb").read())
print("%d files, %d runs, %d bad" % (N, runs, len(failures)))
sys.exit(1 if failures else 0)
