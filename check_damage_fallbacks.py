#!/usr/bin/env python3
import re
import json
import glob

# Load monsters.json
with open('deploy/config/monsters.json', 'r') as f:
    monsters_data = json.load(f)

# Find all .cpp files
cpp_files = glob.glob('*.cpp') + glob.glob('*/*.cpp')

# Pattern to find GetMonsterWeaponDamage with fallback
pattern = r'GetMonsterWeaponDamage\([^,]+,\s*"([^"]+)"\)[^;]*(?:<=\s*0.*?=\s*(\d+)|>\s*0\s*\?\s*\w+\s*:\s*(\d+))'

mismatches = []

for cpp_file in cpp_files:
    with open(cpp_file, 'r') as f:
        content = f.read()

    # Find all matches
    for match in re.finditer(pattern, content):
        weapon = match.group(1)
        fallback = match.group(2) or match.group(3)

        if fallback:
            print(f"{cpp_file}: weapon='{weapon}', fallback={fallback}")

print("\n=== Summary ===")
print(f"Found damage fallback values in code")
