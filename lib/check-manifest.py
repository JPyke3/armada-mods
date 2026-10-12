#!/usr/bin/env python3
# Checks the listing fields of a staged package: check-manifest.py <package folder>
import json
import pathlib
import re
import sys

stage = pathlib.Path(sys.argv[1])
meta = json.loads((stage / "mod.json").read_text())
errors = []
for key in ("name", "summary", "description"):
    if key in meta and not (isinstance(meta[key], str) and meta[key].strip()):
        errors.append(f"{key} must be a non-empty string")
tags = meta.get("tags", [])
if not isinstance(tags, list) or not all(isinstance(t, str) and re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)*", t) for t in tags):
    errors.append("tags must be a list of lowercase words with dashes")
icon = meta.get("icon")
if icon is not None:
    path = (stage / icon).resolve() if isinstance(icon, str) else None
    if not path or stage.resolve() not in path.parents or not path.is_file():
        errors.append("icon must be a file inside the mod")
    elif path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        errors.append("icon must be a PNG")
if errors:
    sys.exit(f"build: {stage.name}: " + "; ".join(errors))
