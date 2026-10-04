#!/usr/bin/env python3
"""Stage the current C sources and short-name Open Watcom DOS build files."""
from pathlib import Path
import re
import shutil
import sys


def source_list(makefile, variable):
    match = re.search(r"^" + variable + r"\s*=\s*(.*?)(?=\n\w)",
                      makefile, re.M | re.S)
    if not match:
        raise SystemExit("Makefile is missing " + variable)
    return match.group(1).replace("\\\n", " ").split()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: legacy-stage.py REPOSITORY SHARE-DIRECTORY")
    root, share = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    fpflag = "-" + __import__("os").environ.get("LEGACY_FPFLAG", "fpc")
    if fpflag not in ("-fpi", "-fpc"):
        raise SystemExit("LEGACY_FPFLAG must be fpi or fpc")
    charts = share / "CHARTS"
    for name in ("APP", "LIB", "INCLUDE", "OBJ", "TESTS", "EXAMPLES"):
        directory = charts / name
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)

    makefile = (root / "Makefile").read_text()
    sources = source_list(makefile, "LIBSRC") + source_list(makefile, "APPSRC")
    for source in sources:
        path = Path(source)
        if path.suffix.lower() != ".c" or len(path.stem) > 8:
            raise SystemExit("DOS 8.3 build requires short C source names: " + source)
        shutil.copy2(root / path, charts / path.parent.name.upper() /
                     (path.stem.upper() + ".C"))

    for directory in ("lib", "app", "include"):
        for source in (root / directory).iterdir():
            if source.is_file() and source.suffix.lower() in (".h", ".inc"):
                if len(source.stem) > 8:
                    if source.name.lower() == "libcharts.h":
                        continue  # compatibility wrapper; charts.h is canonical
                    raise SystemExit("DOS 8.3 build requires short include names: " + str(source))
                target_dir = {"lib": "LIB", "app": "APP", "include": "INCLUDE"}[directory]
                shutil.copy2(source, charts / target_dir /
                             (source.stem.upper() + source.suffix.upper()))
    shutil.copy2(root / "tests" / "dossave.c", charts / "TESTS" / "DOSSAVE.C")
    shutil.copy2(root / "tests" / "fpcprobe.c", charts / "TESTS" / "FPCPROBE.C")
    # file inputs for TEST.BAT; deck.json names revenue.csv, which is already 8.3
    for source, target in (("deck.json", "DECK.JSN"), ("revenue.csv", "REVENUE.CSV"),
                           ("quarterly.csv", "QUARTER.CSV"), ("traffic.tsv", "TRAFFIC.TSV"),
                           ("demo/deck.json", "DEMO.JSN"), ("demo/latency.csv", "LATENCY.CSV"),
                           ("demo/regions.csv", "REGIONS.CSV"), ("demo/signups.csv", "SIGNUPS.CSV")):
        shutil.copy2(root / "examples" / source, charts / "EXAMPLES" / target)

    commands = ["@echo off", "set WATCOM=C:\\DEVEL\\WATCOMC",
                "set PATH=C:\\DEVEL\\WATCOMC\\BINW;%PATH%",
                "set INCLUDE=C:\\DEVEL\\WATCOMC\\H", "C:", "cd \\CHARTS",
                "set NO87=",
                "if exist C:\\BUILD.TXT del C:\\BUILD.TXT"]
    for source in sources:
        path = Path(source)
        stem, folder = path.stem.upper(), path.parent.name.upper()
        commands.append("wcl386 -q -bt=dos -3 " + fpflag + " -za -ox -c -i=INCLUDE -i=APP -i=LIB -fo=OBJ\\" +
                        stem + ".OBJ " + folder + "\\" + stem + ".C")
        commands.append("if errorlevel 1 goto failed")
    lib_sources = [source for source in sources if Path(source).parent.name == "lib"]
    app_sources = [source for source in sources if Path(source).parent.name == "app"]
    (charts / "LIB.CMD").write_text("\n".join(
        "+OBJ\\" + Path(source).stem.upper() + ".OBJ" for source in lib_sources) + "\n")
    linker = ["system dos4g", "option map", "option stack=256k", "name CHARTS.EXE"]
    linker.extend("file OBJ\\" + Path(source).stem.upper() + ".OBJ" for source in app_sources)
    linker.append("file CHARTS.LIB")
    (charts / "LINK.RSP").write_text("\n".join(linker) + "\n")
    commands.extend(["if exist CHARTS.EXE del CHARTS.EXE",
                     "if exist CHARTS.LIB del CHARTS.LIB"])
    for index, source in enumerate(lib_sources):
        mode = " -n" if index == 0 else ""
        commands.append("wlib -q" + mode + " CHARTS.LIB +OBJ\\" +
                        Path(source).stem.upper() + ".OBJ")
        commands.append("if errorlevel 1 goto failed")
    commands.extend(["if not exist CHARTS.LIB goto failed",
                     "wlink @LINK.RSP",
                     "if errorlevel 1 goto failed",
                     "if not exist CHARTS.EXE goto failed",
                     "echo WATCOM_19_WCL386_DOS32_BUILD_PASS> C:\\BUILD.TXT",
                     "goto done", ":failed",
                     "echo WATCOM_19_WCL386_DOS32_BUILD_FAIL> C:\\BUILD.TXT",
                     ":done"])
    (charts / "BUILD.BAT").write_text("\r\n".join(commands) + "\r\n")
    test = ["@echo off", "set WATCOM=C:\\DEVEL\\WATCOMC",
            "set PATH=C:\\DEVEL\\WATCOMC\\BINW;%PATH%",
            "set INCLUDE=C:\\DEVEL\\WATCOMC\\H", "C:", "cd \\CHARTS",
            "set NO87=",
            "CHARTS.EXE --demo --check > CHECK.TXT",
            "if errorlevel 1 goto failed",
            "CHARTS.EXE --demo --png-dir HARD --width 80 --height 24",
            "if errorlevel 1 goto failed",
            "CHARTS.EXE EXAMPLES\\DEMO.JSN --check >> CHECK.TXT",
            "if errorlevel 1 goto failed",
            "CHARTS.EXE EXAMPLES\\DECK.JSN --check >> CHECK.TXT",
            "if errorlevel 1 goto failed",
            "CHARTS.EXE EXAMPLES\\QUARTER.CSV --check >> CHECK.TXT",
            "if errorlevel 1 goto failed",
            "CHARTS.EXE EXAMPLES\\TRAFFIC.TSV --check >> CHECK.TXT",
            "if errorlevel 1 goto failed",
            "CHARTS.EXE EXAMPLES\\DECK.JSN --png-dir FILE --width 80 --height 24",
            "if errorlevel 1 goto failed",
            "set NO87=1",
            "CHARTS.EXE --demo --png-dir SOFT --width 80 --height 24",
            "if errorlevel 1 goto failed", "set NO87=",
            "wcl386 -q -bt=dos -3 " + fpflag + " -za -ox -c -i=INCLUDE -i=APP -i=LIB -fo=OBJ\\DOSSAVE.OBJ TESTS\\DOSSAVE.C",
            "if errorlevel 1 goto failed",
            "wlink @DOSSAVE.RSP",
            "if errorlevel 1 goto failed",
            "DOSSAVE.EXE",
            "if errorlevel 1 goto failed",
            "echo WATCOM_19_DOS32_DEMO_FILES_HARD_SOFT_SAVE_PASS> C:\\TEST.TXT",
            "goto done", ":failed",
            "set NO87=", "echo WATCOM_19_DOS32_DEMO_FILES_HARD_SOFT_SAVE_FAIL> C:\\TEST.TXT", ":done"]
    (charts / "TEST.BAT").write_text("\r\n".join(test) + "\r\n")
    dossave_link = ["system dos4g", "option map", "option stack=256k", "name DOSSAVE.EXE",
                    "file OBJ\\DOSSAVE.OBJ", "file OBJ\\MODEL.OBJ",
                    "file OBJ\\PLATFORM.OBJ", "file CHARTS.LIB"]
    (charts / "DOSSAVE.RSP").write_text("\n".join(dossave_link) + "\n")
    relink = ["@echo off", "set WATCOM=C:\\DEVEL\\WATCOMC",
              "set PATH=C:\\DEVEL\\WATCOMC\\BINW;%PATH%",
              "set INCLUDE=C:\\DEVEL\\WATCOMC\\H", "C:", "cd \\CHARTS",
              "set NO87=",
              "wcl386 -q -bt=dos -3 " + fpflag + " -za -ox -c -i=INCLUDE -i=APP -i=LIB -fo=OBJ\\PLATFORM.OBJ APP\\PLATFORM.C",
              "if errorlevel 1 goto failed", "if exist CHARTS.EXE del CHARTS.EXE",
              "wlink @LINK.RSP", "if errorlevel 1 goto failed",
              "if not exist CHARTS.EXE goto failed",
              "echo WATCOM_19_PLATFORM_RELINK_PASS> C:\\RELINK.TXT", "goto done",
              ":failed", "echo WATCOM_19_PLATFORM_RELINK_FAIL> C:\\RELINK.TXT", ":done"]
    (charts / "RELINK.BAT").write_text("\r\n".join(relink) + "\r\n")
    (charts / "FPCPROBE.BAT").write_text("\r\n".join([
        "@echo off", "set WATCOM=C:\\DEVEL\\WATCOMC",
        "set PATH=C:\\DEVEL\\WATCOMC\\BINW;%PATH%",
        "set INCLUDE=C:\\DEVEL\\WATCOMC\\H", "C:", "cd \\CHARTS", "set NO87=",
        "wcl386 -q -bt=dos -3 -fpc -za -ox -fe=FPCPROBE.EXE TESTS\\FPCPROBE.C",
        "if errorlevel 1 goto failed", "set NO87=1", "FPCPROBE.EXE",
        "if errorlevel 1 goto failed", "set NO87=",
        "echo WATCOM_19_FPC_NO87_PASS> C:\\FPC.TXT", "goto done",
        ":failed", "set NO87=", "echo WATCOM_19_FPC_NO87_FAIL> C:\\FPC.TXT", ":done"
    ]) + "\r\n")
    print("Staged", len(sources), "C translation units in", charts)


if __name__ == "__main__":
    main()
