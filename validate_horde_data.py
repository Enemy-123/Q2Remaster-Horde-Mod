#!/usr/bin/env python3
"""Validate Horde Lua config keys and deployed entity classnames."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def collect_monster_names() -> set[str]:
    text = read("horde/horde_ids.cpp")
    classnames = re.findall(r'"(monster_[^"]+|misc_[^"]+)"\s*,\s*MonsterTypeID::', text)
    return {name.removeprefix("monster_") for name in classnames}


def collect_weapon_names() -> set[str]:
    text = read("horde/weapon_id.cpp")
    return set(re.findall(r'"([a-z0-9_]+)"\s*,\s*WeaponID::', text))


def collect_spawn_classnames() -> set[str]:
    text = read("g_spawn.cpp")
    return set(re.findall(r'\{\s*"([^"]+)"\s*,\s*SP_', text))


def validate_monsters_lua(monster_names: set[str], weapon_names: set[str]) -> list[str]:
    config = read("deploy/config/monsters.lua")
    keys = re.findall(r"^\s*([A-Z0-9_]+)\s*=", config, re.MULTILINE)
    errors: list[str] = []

    monster_fields = [
        "POWER_ARMOR_POWER",
        "POWER_ARMOR_TYPE",
        "POWER_ARMOR_SCALE",
        "HEALTH_SCALE",
        "DAMAGE_SCALE",
        "SPEED_SCALE",
        "ARMOR_SCALE",
        "POWER_ARMOR",
        "ARMOR_POWER",
        "ARMOR_TYPE",
        "HEALTH",
    ]
    level_fields = [
        "INITIAL_POWER_ARMOR",
        "ADDON_POWER_ARMOR",
        "INITIAL_HEALTH",
        "ADDON_HEALTH",
        "INITIAL_ARMOR",
        "ADDON_ARMOR",
    ]
    weapon_infixes = ["_ADDON_DAMAGE_", "_DAMAGE_MAX_", "_DAMAGE_", "_SPEED_"]

    def check_global_weapon(key: str, prefix: str) -> bool:
        if not key.startswith(prefix):
            return False
        weapon = key[len(prefix) :].lower()
        if weapon not in weapon_names:
            errors.append(f"deploy/config/monsters.lua: unknown global weapon key {key}")
        return True

    for key in keys:
        if (
            check_global_weapon(key, "GLOBAL_DAMAGE_")
            or check_global_weapon(key, "GLOBAL_SPEED_")
            or check_global_weapon(key, "GLOBAL_RADIUS_")
        ):
            continue

        if key.startswith("MONSTER_LEVEL_"):
            body = key[len("MONSTER_LEVEL_") :]
            for field in level_fields:
                suffix = "_" + field
                if body.endswith(suffix):
                    monster = body[: -len(suffix)].lower()
                    if monster not in monster_names:
                        errors.append(f"deploy/config/monsters.lua: unknown level-scaling monster {key}")
                    break
            else:
                errors.append(f"deploy/config/monsters.lua: unparsed level-scaling key {key}")
            continue

        if key.startswith("MONSTER_"):
            body = key[len("MONSTER_") :]
            parsed = False
            for field in monster_fields:
                suffix = "_" + field
                if body.endswith(suffix):
                    parsed = True
                    monster = body[: -len(suffix)].lower()
                    if monster not in monster_names:
                        errors.append(f"deploy/config/monsters.lua: unknown monster key {key}")
                    break

            if parsed:
                continue

            for infix in weapon_infixes:
                if infix in body:
                    parsed = True
                    monster, weapon = body.split(infix, 1)
                    monster = monster.lower()
                    weapon = weapon.lower()
                    if monster not in monster_names:
                        errors.append(f"deploy/config/monsters.lua: unknown monster key {key}")
                    if weapon not in weapon_names:
                        errors.append(f"deploy/config/monsters.lua: unknown weapon key {key}")
                    break

            if not parsed:
                errors.append(f"deploy/config/monsters.lua: unparsed monster key {key}")

    return errors


def validate_ent_classnames(spawn_classnames: set[str]) -> list[str]:
    errors: list[str] = []
    for path in sorted((ROOT / "deploy/ents").glob("*.ent")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in re.finditer(r'"classname"\s+"([^"]+)"', text):
            classname = match.group(1)
            if not classname.startswith("monster_"):
                continue
            if classname in spawn_classnames:
                continue
            line = text.count("\n", 0, match.start()) + 1
            rel = path.relative_to(ROOT).as_posix()
            errors.append(f"{rel}:{line}: unknown monster classname {classname}")
    return errors


def main() -> int:
    monster_names = collect_monster_names()
    weapon_names = collect_weapon_names()
    spawn_classnames = collect_spawn_classnames()

    errors = []
    errors.extend(validate_monsters_lua(monster_names, weapon_names))
    errors.extend(validate_ent_classnames(spawn_classnames))

    if errors:
        print("Horde data validation failed:")
        for error in errors:
            print(f"  {error}")
        return 1

    print("Horde data validation passed.")
    print(f"  monster config names: {len(monster_names)}")
    print(f"  weapon names: {len(weapon_names)}")
    print(f"  spawn classnames: {len(spawn_classnames)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
