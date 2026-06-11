#!/usr/bin/env python3
"""GN action entry point for assembling an Apple .framework.

GN's `.gn` pins `script_executable = "python3"` (Skia's global convention), so a GN
`action` must invoke a Python script. The actual framework assembly is done entirely
with first-party Apple command-line tools in assemble_apple_framework.sh; this file is
only the thin launcher GN requires - it forwards its arguments to that shell script.
"""
import os
import subprocess
import sys

_here = os.path.dirname(os.path.abspath(__file__))
_script = os.path.join(_here, "assemble_apple_framework.sh")
sys.exit(subprocess.call(["/bin/bash", _script] + sys.argv[1:]))
