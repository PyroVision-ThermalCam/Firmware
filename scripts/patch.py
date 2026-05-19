"""
patch.py

Copyright (C) Daniel Kampert, 2026
Website: www.kampis-elektroecke.de
File info: PlatformIO pre-build script that applies all *.patch files found in
           the project's patches/ directory to the ESP-IDF framework package
           used by the current build environment.

           Patches are applied idempotently: a SHA-256 stamp file stored in
           .pio/patches.applied tracks which patches have already been applied
           so that repeated builds do not re-apply or error on existing patches.
           As a secondary check, `git apply --check --reverse` is used to
           confirm whether a patch is already present even when the stamp file
           has been deleted or the patches/ directory has changed.

           Patches must be unified diff format (git format-patch output is
           ideal).  File paths inside the patch must be relative to the root
           of the ESP-IDF framework directory.

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

import sys
import hashlib
import subprocess

from pathlib import Path

def _sha256(path: Path) -> str:
    """Return the SHA-256 hex digest of a file's content."""

    h = hashlib.sha256()
    h.update(path.read_bytes())

    return h.hexdigest()

def _git_available() -> bool:
    """Return True if git is on PATH."""

    try:
        subprocess.run(["git", "--version"], capture_output=True, check=True)
        return True
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False

def _is_already_applied(patch_path: Path, idf_dir: Path) -> bool:
    """Return True when the patch's reverse applies cleanly (i.e. patch is present)."""

    result = subprocess.run(
        ["git", "apply", "--ignore-whitespace", "--check", "--reverse", str(patch_path)],
        cwd = str(idf_dir),
        capture_output = True,
    )

    return result.returncode == 0

def _apply_patch(patch_path: Path, idf_dir: Path) -> None:
    """Apply a single patch file to idf_dir; raise on failure."""

    result = subprocess.run(
        ["git", "apply", "--ignore-whitespace", "--whitespace=nowarn", str(patch_path)],
        cwd = str(idf_dir),
        capture_output = True,
        text = True,
    )

    if(result.returncode != 0):
        print("    stdout: {}".format(result.stdout.strip()))
        print("    stderr: {}".format(result.stderr.strip()))
        raise RuntimeError("git apply failed for '{}'".format(patch_path.name))

def _load_stamp(stamp_path: Path) -> set:
    """Load the set of already-applied patch SHA-256 hashes from the stamp file."""

    if(stamp_path.exists()):
        return set(stamp_path.read_text(encoding="utf-8").splitlines())

    return set()

def _save_stamp(stamp_path: Path, applied: set) -> None:
    """Persist the set of applied patch hashes to the stamp file."""

    stamp_path.parent.mkdir(parents = True, exist_ok = True)
    stamp_path.write_text("\n".join(sorted(applied)), encoding = "utf-8")

def apply_patches(source, target, env):
    project_dir = Path(env.get("PROJECT_DIR"))
    patches_dir = project_dir / "patches"
    stamp_file = project_dir / ".pio" / "patches.applied"

    # Resolve the ESP-IDF framework package directory
    try:
        idf_dir = Path(env.PioPlatform().get_package_dir("framework-espidf"))
    except Exception as exc:
        print("patch.py: Cannot determine ESP-IDF path: {}".format(exc))
        return

    if(not(idf_dir.exists())):
        print("patch.py: ESP-IDF directory not found: {}".format(idf_dir))
        return

    # Collect patches sorted by name so numbered prefixes control order
    patch_files = sorted(patches_dir.glob("*.patch"))

    if(not(patch_files)):
        # Nothing to do – not an error
        return

    if(not(_git_available())):
        print("patch.py: 'git' not found on PATH – cannot apply patches")
        sys.exit(1)

    applied_hashes = _load_stamp(stamp_file)
    newly_applied = 0
    skipped = 0
    errors = 0

    print("patch.py: Applying {} patch(es) to ESP-IDF at".format(len(patch_files)))
    print("          {}".format(idf_dir))

    for patch in patch_files:
        digest = _sha256(patch)

        # Stamp file says we applied this patch – verify with git in case
        # the ESP-IDF package was reinstalled and the changes were wiped.
        if digest in applied_hashes:
            if(_is_already_applied(patch, idf_dir)):
                print("  [skip]  {} (stamp)".format(patch.name))
                skipped += 1
                continue

            # Stamp is stale: the patch content is no longer present.
            print("  [re-apply] {} (stamp stale, ESP-IDF may have been reinstalled)".format(patch.name))
            applied_hashes.discard(digest)

        # Slower but reliable: ask git whether the patch is already present
        if(_is_already_applied(patch, idf_dir)):
            print("  [skip]  {} (already applied)".format(patch.name))
            applied_hashes.add(digest)
            skipped += 1
            continue

        # Apply the patch
        print("  [apply] {}".format(patch.name))
        try:
            _apply_patch(patch, idf_dir)
            applied_hashes.add(digest)
            newly_applied += 1
        except RuntimeError as exc:
            print("  [ERROR] {}".format(exc))
            errors += 1

    # Persist updated stamp regardless of errors so successful patches are not re-run
    _save_stamp(stamp_file, applied_hashes)

    if(errors > 0):
        print("patch.py: {} patch(es) failed – stopping build".format(errors))
        sys.exit(1)

    if(newly_applied > 0):
        print("patch.py: Applied {}, skipped {}".format(newly_applied, skipped))
    else:
        print("patch.py: All patches already applied ({} skipped)".format(skipped))

# Run immediately during the pre-build phase (script is loaded with "pre:" prefix
# in extra_scripts, so module-level code executes before any compilation starts).
apply_patches(None, None, env)
