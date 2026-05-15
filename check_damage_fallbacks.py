#!/usr/bin/env python3
"""Print the monster weapon damage clamps generated for the Lua config."""

from pathlib import Path

report_path = Path("monster_damage_clamp_report.md")
if not report_path.exists():
    raise SystemExit("monster_damage_clamp_report.md not found")

rows = [
    line for line in report_path.read_text(encoding="ascii").splitlines()
    if line.startswith("| `")
]

print("=== Monster Damage Caps ===")
for row in rows:
    parts = [part.strip().strip("`") for part in row.strip("|").split("|")]
    if len(parts) == 3:
        print(f"{parts[0]}.{parts[1]} <= {parts[2]}")

print(f"\nTotal clamped weapon entries: {len(rows)}")
