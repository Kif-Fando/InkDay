#!/usr/bin/env python3
"""Add InkDay with its icon to the PocketBook Verse Pro / Verse Pro Color menu.

Reads a copy of /mnt/ext1/system/config/desktop/view.json, adds the user
application entry "U_InkDay" (the format is documented in the file's own
"_comment") and lists it in the "@General" group. Running it again changes
nothing.

Usage: tools/desktop-entry.py <view.json from the reader> <output view.json>
"""
import json
import sys

KEY = "U_InkDay"
ENTRY = {
    "path": "/mnt/ext1/applications/InkDay.app",
    "title": "InkDay",
    "icon": {"path": "applications/icons/InkDay.bmp"},
    "focused_icon": {"path": "applications/icons/InkDay_f.bmp"},
}
GROUP = "@General"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        view = json.load(f)

    view.setdefault("applications", {})[KEY] = ENTRY
    groups = view.setdefault("view", {}).setdefault("groups", [])
    group = next((g for g in groups if g.get("title") == GROUP), None)
    if group is None:
        group = {"title": GROUP, "sort": "title", "apps": []}
        groups.insert(0, group)
    apps = group.setdefault("apps", [])
    if KEY not in apps:
        apps.append(KEY)

    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(view, f, ensure_ascii=False, indent=4)
        f.write("\n")


if __name__ == "__main__":
    main()
