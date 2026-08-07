#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


tool = Path(sys.argv[1]).resolve()
root = Path(sys.argv[2]).resolve()
libporpoise = Path(sys.argv[3]).resolve()

with tempfile.TemporaryDirectory(prefix="porpoise-compat-") as temporary:
    output = Path(temporary) / "generated"
    subprocess.run([str(tool), str(root / "tests/fixtures/inputs/with_main"), "--output", str(output)], check=True)
    subproject = output / "subprojects" / "libPorpoise"
    subproject.parent.mkdir(parents=True)
    try:
        subproject.symlink_to(libporpoise, target_is_directory=True)
    except OSError:
        shutil.copytree(libporpoise, subproject, ignore=shutil.ignore_patterns("build", "build-*", ".git"))
    subprocess.run(["meson", "setup", "build", "--wrap-mode=forcefallback"], cwd=output, check=True)
    subprocess.run(["meson", "compile", "-C", "build"], cwd=output, check=True)
