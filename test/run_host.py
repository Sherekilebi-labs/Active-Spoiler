#!/usr/bin/env python3
"""Compile and run hardware-independent production logic with sanitizers."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("Install a C++ compiler or set CXX")
with tempfile.TemporaryDirectory(prefix="tsintsadze-tests-") as directory:
    for name in ("host", "n1_commands", "n2_requests"):
        binary = str(Path(directory) / name)
        dependency = root / ".pio/libdeps/esp32_n2/ArduinoJson/src"
        if name == "n2_requests" and not dependency.is_dir():
            raise SystemExit("Build esp32_n2 first to install the ArduinoJson headers used by the HTTP tests")
        subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                        "-I" + str(root / "test/stubs"), "-I" + str(root / "include"),
                        "-I" + str(dependency),
                        str(root / ("test/" + name + ".cpp")), "-o", binary], check=True)
        subprocess.run([binary], check=True)
