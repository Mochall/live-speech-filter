"""Check that a plugin DLL can load in a given OBS install: every function the plugin
imports from a DLL shipped in <obs>/bin/64bit must be exported by that DLL.

usage: python check-plugin-imports.py <plugin.dll> <obs root> [<obs root> ...]

A missing symbol makes Windows refuse the whole plugin (OBS logs "Module ... not
loaded"). DLLs not shipped with OBS (system, MSVC runtime, the plugin's own
onnxruntime/sherpa DLLs) are listed but not checked.
"""
import glob
import os
import re
import subprocess
import sys


def dumpbin():
    vs = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC"
    found = sorted(glob.glob(os.path.join(vs, "*", "bin", "Hostx64", "x64", "dumpbin.exe")))
    if not found:
        raise SystemExit("dumpbin.exe not found under " + vs)
    return found[-1]


def run(args):
    return subprocess.run([dumpbin()] + args, capture_output=True, text=True,
                          encoding="utf-8", errors="ignore").stdout


def imports(dll):
    result, cur = {}, None
    for line in run(["/IMPORTS", dll]).splitlines():
        m = re.match(r"\s{4}(\S+\.dll)$", line, re.I)
        if m:
            cur = m.group(1)
            result.setdefault(cur, [])
            continue
        m = re.match(r"\s+[0-9A-F]+\s+(\S+)$", line)
        if m and cur:
            result[cur].append(m.group(1))
    return result


def exports(dll):
    names = set()
    for line in run(["/EXPORTS", dll]).splitlines():
        m = re.match(r"\s+\d+\s+[0-9A-F]+\s+(?:[0-9A-F]{8}\s+)?(\S+)", line)
        if m:
            names.add(m.group(1))
    return names


def main():
    plugin, roots = sys.argv[1], sys.argv[2:]
    imp = imports(plugin)
    worst = 0
    for root in roots:
        bindir = os.path.join(root, "bin", "64bit")
        shipped = {f.lower(): os.path.join(bindir, f) for f in os.listdir(bindir) if f.lower().endswith(".dll")}
        missing, checked, unchecked = [], 0, []
        for dll, syms in imp.items():
            path = shipped.get(dll.lower())
            if not path:
                unchecked.append(dll)
                continue
            ex = exports(path)
            for s in syms:
                checked += 1
                if s not in ex:
                    missing.append((dll, s))
        print(f"== {root}: checked {checked} imports, missing {len(missing)}")
        for dll, s in missing:
            print(f"   MISSING {dll}: {s}")
        print("   not shipped with OBS (not checked): " + ", ".join(sorted(unchecked)))
        worst = max(worst, 1 if missing else 0)
    sys.exit(worst)


if __name__ == "__main__":
    main()
