#!/usr/bin/env python3
"""Copy ProxySQL and its transitive CLANG64 runtime DLLs into one directory."""

import argparse
import re
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--dll-dir", action="append", required=True, type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    available = {}
    for directory in args.dll_dir:
        for dll in directory.glob("*.dll"):
            available.setdefault(dll.name.lower(), dll)

    pending = [args.executable]
    files = []
    imported = set()
    while pending:
        binary = pending.pop()
        files.append(binary)
        headers = subprocess.check_output([args.objdump, "-p", binary], text=True)
        for dll in re.findall(r"DLL Name: ([^\s]+)", headers):
            name = dll.lower()
            if name in imported:
                continue
            imported.add(name)
            if name in available:
                pending.append(available[name])
            elif name.startswith("lib"):
                raise SystemExit(f"Missing runtime DLL: {dll}")

    args.output.mkdir(parents=True, exist_ok=False)
    for binary in files:
        shutil.copy2(binary, args.output)


if __name__ == "__main__":
    main()
