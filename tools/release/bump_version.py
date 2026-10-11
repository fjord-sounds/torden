#!/usr/bin/env python3
"""Sets the version of Torden and prepares the changelog.

The single source of truth is `project(Torden VERSION x.y.z)` in CMakeLists.txt: the plugin version (VST3, AU,
LV2, About panel) and the test tool take it from there.

Usage:  python3 tools/release/bump_version.py 0.2.0

What it does: sets the version in CMakeLists.txt and turns "## [Unreleased]" in CHANGELOG.md into the new
release section (with today's date) with a fresh empty "Unreleased" above it. Then commit and tag:
    git commit -am "Release 0.2.0" && git tag -a v0.2.0 -m "Torden 0.2.0" && git push --follow-tags
The release workflow (.github/workflows/release.yml) builds the binaries when the tag arrives.
"""
import datetime
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

if len(sys.argv) != 2 or not re.fullmatch(r"\d+\.\d+\.\d+", sys.argv[1]):
    sys.exit(__doc__)
version = sys.argv[1]

cmake = ROOT / "CMakeLists.txt"
text = cmake.read_text()
text, n = re.subn(r"(project\(Torden VERSION )\d+\.\d+\.\d+", rf"\g<1>{version}", text, count=1)
assert n == 1, "project(Torden VERSION ...) not found in CMakeLists.txt"
cmake.write_text(text)

log = ROOT / "CHANGELOG.md"
text = log.read_text()
assert "## [Unreleased]" in text, "CHANGELOG.md has no [Unreleased] section"
assert f"## [{version}]" not in text, f"CHANGELOG.md already has {version}"
text = text.replace("## [Unreleased]", f"## [Unreleased]\n\n## [{version}] – {datetime.date.today().isoformat()}", 1)
log.write_text(text)
print(f"Version {version} set. Review the diff, then commit and tag v{version}.")
