"""
format.py

Copyright (C) Daniel Kampert, 2026
Website: www.kampis-elektroecke.de
File info: AStyle formatting script for PlatformIO.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
"""

Import("env")
import os
import subprocess

from pathlib import Path

PATTERNS = ["*.c", "*.cpp", "*.h"]

def run_astyle(source, target, env):
    project_dir = env.get("PROJECT_DIR")
    astyle_config = os.path.join(project_dir, "scripts/astyle.cfg")

    try:
        subprocess.run(["astyle", "--version"], capture_output = True, check = True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("AStyle not found!")
        return

    if(not(os.path.exists(astyle_config))):
        print("AStyle config not found: {}".format(astyle_config))
        return

    files_to_format = []
    main_dir = Path(project_dir) / "main"
    for pattern in PATTERNS:
        files_to_format.extend(main_dir.rglob(pattern))

    if(not(files_to_format)):
        print("No C/C++ files found to format")
        return

    print("🎨 Formatting {} files with AStyle...".format(len(files_to_format)))
    print("   Config: {}".format(astyle_config))

    cmd = ["astyle", "--options={}".format(astyle_config)] + [str(f) for f in files_to_format]

    try:
        result = subprocess.run(cmd, capture_output = True, text = True, check = True)

        if(result.stdout):
            print(result.stdout)

        formatted_count = result.stdout.count("Formatted")
        unchanged_count = result.stdout.count("Unchanged")

        print("✅ Done!")
        print("   Formatted: {}".format(formatted_count))
        print("   Unchanged: {}".format(unchanged_count))

    except subprocess.CalledProcessError as e:
        print("❌ AStyle failed")

        if(e.stdout):
            print(e.stdout)

        if(e.stderr):
            print(e.stderr)

        raise

env.AlwaysBuild(env.Alias("format", None, run_astyle))
