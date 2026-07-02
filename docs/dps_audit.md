# DPS Balance Audit — Player Weapons & Monster Attacks

> **Status:** Analysis + recommendations. No code was changed to produce this document.
> **Build basis:** values read from `g_config.h`, `p_weapon.cpp`, `xatrix/p_xatrix_weapon.cpp`,
> `rogue/p_rogue_weapon.cpp`, and the live `m_*.cpp` monster files.
> `fire_results.txt` is **stale and was NOT used** — every monster number here was re-derived
> from the current source.

---

## 1. Method & timing model

**Frame clock.** Player weapons and monster animations both advance at **10 Hz → 1 frame = 100 ms**.
- Player weapons: `Weapon_AnimationTime()` pins `ps.gunrate = 10` (`p_weapon.cpp:477`). Haste and
  QuadFire each multiply the *rate* ×2 (i.e. halve the cycle time).
- A `Weapon_Generic` weapon's **refire interval = (FRAME_FIRE_LAST − FRAME_FIRE_FIRST + 1) frames**,
  where `FRAME_FIRE_FIRST = FRAME_ACTIVATE_LAST + 1`. After the fire frame the animation walks to
  `FRAME_FIRE_LAST` and loops back to `FRAME_FIRE_FIRST` while attack is held (`p_weapon.cpp:958`).
- A `Weapon_Repeating` weapon (`p_weapon.cpp:1032`) calls its fire fn once per 100 ms think; the fire
  fn itself decides how many projectiles per think (machinegun = 1, chaingun = up to 3, hyperblaster/
  heatbeam fire every think in their loop).

**DPS formulas.**
- Player: `DPS = avg(dmg/hit) × hits-per-shot × shots/sec`. `DPS/ammo = DPS ÷ ammo/shot` (efficiency).
- Monster: `per-attack DPS = avg(dmg) × hits-per-animation ÷ animation-seconds`, where
  animation-seconds = (last frame − first frame + 1) × 100 ms from the attack's `MMOVE_T`.

**What DPS does *not* capture** (read every number as a ceiling, not a field average): travel time
and dodgeability of projectiles, hit-rate/spread, range falloff, splash overlap, refire probability,
target-switching, and AI decision gaps between animations. Use the numbers for *relative* comparison.

---

## 2. Scaling caveats (important — corrects common assumptions)

1. **There is no wave-based damage scaling right now.** `g_config.h:659` sets
   `use_sigmoid_scaling = false` and `g_horde_scaling.cpp:106` hard-disables the sigmoid system
   ("using unscaled Lua monster damage caps"). So a monster's bite at wave 40 hits for the same as at
   wave 5. **Base DPS below is the canonical value.**
2. **The only runtime damage multiplier is the powerup bonus.** `M_DamageModifier()`
   (`shared.cpp:491`) returns **1.0** normally, **2.0** under Double, **3.0** under Quad
   (sentry guns: 1.5 / 2.0). Champions/elites that roll a Quad/Double bonus flag therefore hit
   ×2–×3, but ordinary monsters do not.
3. **Difficulty progression is structural, not per-hit:** more monsters, tougher *types*, champions,
   and the anti-domination cadence/health system — not bigger numbers per bite.

**Where monster damage comes from** (`GetMonsterWeaponDamage`, `g_config.cpp:1605`), in priority order:
boss override → per-monster override → `GlobalWeaponDamage` default × per-monster `damage_scale` →
clamp to `weapon_damage_max`. Rows tagged **`cfg`** use this path (shown at the in-code global
default; a loaded monster data file can override per-monster). Rows tagged **`inline`** hardcode the
damage at the call site and bypass the config entirely.

`GlobalWeaponDamage` defaults (`g_config.h:384`): MELEE 10 · BLASTER 15 · BLASTER2 20 · BLASTER_BOLT 18 ·
SHOTGUN 4 · MACHINEGUN 8 · GRENADE 50 · ROCKET 100 · HEAT 15 · RAILGUN 150 · BFG 500 · IONRIPPER 50 ·
HYPERBLASTER 15 · BOLT 20 · TRACKER 30 · PLASMA 40 · DABEAM 30 · HEATBEAM 30 · SLAM 25 · LIGHTNING 12 ·
FLECHETTE 12 · FIREBALL 40 · PROBOSCIS 20.

---

## 3. Player weapons

Base values, no Quad/Haste. Damage from `g_config.h`; cadence from the frame tables cited in §1.
"avg" = midpoint of the min–max roll.

| Weapon | dmg/hit (avg) | hits/shot | cycle | shots/s | ammo/shot | **raw DPS** | DPS/ammo | type / notes |
|---|---|---|---|---|---|---|---|---|
| Blaster | 16–18 (17) | 1 | FIRE 5–8 = 4f | 2.5 | 0 (∞) | **42.5** | ∞ | proj 1300 u/s |
| Shotgun | 3–5 (4) | 20 pellets | FIRE 8–18 = 11f | 0.91 | 1 shell | **72.7** | 72.7 | hitscan spread |
| Super Shotgun | 7–10 (8.5) | 20 pellets | FIRE 7–17 = 11f | 0.91 | 2 shells | **154.5** | 77.3 | hitscan spread |
| Machinegun | 7–10 (8.5) | 1 | repeating | 10 | 1 bullet | **85** | 85 | hitscan; +40 tracer/500 ms |
| Chaingun | 7–11 (9) | up to 3/think | repeating | 30 @ full spin | 1 bullet | **~270** (spins up 1→2→3) | 9 | hitscan; +20 tracer/300 ms |
| HyperBlaster | 16–18 (17) | 1/think | repeating | ~10 | 1 cell | **~170** | 17 | proj 1700 u/s |
| Hand Grenade | 125 + splash | 1 | thrown | — | 1 | **~125/throw** | — | situational |
| Grenade Launcher | 115 + r155 | 1 | FIRE 6–16 = 11f | 0.91 | 1 | **~104** direct | 104 | proj 900 u/s; Napalm = 95, 2 ammo |
| Rocket Launcher | 100–120 (110) + r140 | 1 | FIRE 5–12 = 8f | 1.25 | 1 | **137.5** direct | 137.5 | proj 1230 u/s |
| Railgun | **225** (horde; 150 base) | 1 | FIRE 4–18 = 15f | 0.67 | 1 slug | **~150** | 150 | hitscan, pierces |
| 20mm Cannon | 35 | 1 | FIRE 4–4 = **1f** | 10 | 1 | **~350** ⚠ | 350 | hitscan pierce, range 650 — see §5 |
| BFG10K | 700 + r1000 + beams | 1 | FIRE 9–32 = 24f | 0.42 | 50 cells | **~290** direct (+AoE) | ~6 | proj 650 u/s; Slide = 25 cells |
| Ionripper | 50 | 1 | FIRE 6–7 = 2f | 5 | 2 cells | **250** | 125 | proj 900 u/s, bounces |
| Phalanx | 80–95 (87.5) + splash | 2 (frames 7,8) | FIRE 6–20 = 15f | 2 / 1.5 s | 1 magslug ea | **~117** | ~58 | fires 2 projectiles |
| ETF Rifle | 9–13 (11) | 1/think | repeating | ~10 | 1 flechette | **~110** | 110 | proj 1450 u/s |
| **Plasma Beam** (Heatbeam) | 145 (135 SP) | 1/think | repeating loop | ~10 | ~2 cells | **~1450** ⚠ single-target | ~72 | hitscan beam, pierces — top outlier |
| Prox Launcher | 95 + r220 | 1 | FIRE 6–16 = 11f | 0.91 | 1 | **~86** (delayed) | 86 | mines; situational |
| Tesla (thrown) | 4 contact / ~300 blast | — | thrown | — | 1 | burst | — | area-denial |
| ChainFist | melee, per-think | 1/think | repeating | ~10 | 0 | high but point-blank | ∞ | melee |
| Disintegrator | 140 | 1 | FIRE 17–23 = 7f | 1.43 | 1 | **~200** | 200 | tracking; see §5 (two defs) |

**Damaging deployables** (`g_config.h`; scale with player upgrade level and the powerup ×-modifier):
sentry gun bullet 10 (+1/lvl) / rocket 50 (+15) / plasma 50 (+15) / grenade 50 (+15), heatbeam 10,
flechette 10 — cost 50 PC; laser 1 (+2/lvl), ×0.5 vs non-monsters — cost 25 PC; tesla mine 4 + ~×50
blast; prox mine 95 / r220; exploding barrel 100 (+40/lvl); food-cube trap blast 300 / r100.

---

## 4. Monster attacks

Per-attack, **single-target, base values, no powerup**. `cfg` = resolved via `GetMonsterWeaponDamage`
(shown at global default; per-monster/boss data file may override). `inline` = hardcoded at the call
site. Animation seconds from the attack's `MMOVE_T` frame span. Many attacks loop/refire, so the
"effective" rate while engaged can be higher than one full animation — noted where it matters.

### Common troops

| Monster | Attack (anim) | dmg | src | anim | ≈DPS | notes |
|---|---|---|---|---|---|---|
| Soldier (light) | blaster bolt (attak1, 12f) | ~BLASTER_BOLT 18 | cfg | 1.2 s | ~15 | trash; threat by numbers |
| Soldier (SS) | shotgun burst (12f) | 4 × pellets | cfg | 1.2 s | ~low-mid | hitscan spread |
| Soldier (MG) | bullet burst (12f) | MACHINEGUN 8 ×n | cfg | 1.2 s | ~low-mid | |
| Infantry | machinegun (attak1, 15f) | 3 (default) ×n | inline | 1.5 s | ~low | weak per hit |
| Infantry | grenade toss (attak5, 23f) | ~40 + splash | inline | 2.3 s | ~17 | arcing |
| Gunner | chaingun (attak chain 7f → fire loop 8f) | 6 (default)/bullet | inline | ~0.8 s burst | **standout** | sustained bullet stream |
| Gunner | grenade (attak grenade, 16f) | 40 + splash | inline | 1.6 s | ~25 | |
| Guncmdr | flechette chain | FLECHETTE 12 | cfg | loop | mid–high | + grenade/mortar, kick 15 |
| Berserk | spike (att_c1–8, 8f) | `damage` ×mod | cfg | 0.8 s | ~mid | melee, range-gated |
| Berserk | club (att_c9–20, 12f) | `damage` ×mod | cfg | 1.2 s | ~mid | melee |
| Berserk | ground strike (slam, 23f) | SLAM 25 + radius | cfg | 2.3 s | ~burst AoE | leap slam |
| Berserk (KL) | spike / club | irandom 21–28 / 15–22 ×mod | inline | 0.8 / 1.2 s | **high melee** | hardest melee troop |
| Gladiator | sword melee (melee3–16, 14f) | irandom 30–35 (32.5) | inline | 1.4 s | **~23** | strong melee |
| Gladiator | railgun (attack1–9, 9f) | `damage` | cfg | 0.9 s | hitscan | leads target |
| Chick | rocket (attak1, 14f) | ROCKET 100 | cfg | 1.4 s | ~71 | proj |
| Chick | railgun variant (14f) | RAILGUN 150 | cfg | 1.4 s | ~107 | hitscan |
| Chick | melee | irandom 10–16 | inline | — | low | |
| Parasite | drain proboscis (drain01–18, 18f) | PROBOSCIS 20 + drain | cfg | 1.8 s | ~11 | latches & drains |
| Flyer | blaster (attak2, 17f) | BLASTER_BOLT 18 ×n | cfg | 1.7 s | ~low | rapid weak bolts |
| Flyer | melee | 3 | inline | — | trivial | |
| Mutant | claw (attack09–15, 7f) | `damage` ×mod | cfg | 0.7 s | ~mid–high | fast melee, leaps |
| Brain | tentacle (attak1, 18f) | `damage` melee | inline | 1.8 s | low | + tongue pull/steal |
| Brain | tongue grab (attak3, 11f) | pull + steal | inline | 1.1 s | situational | |
| Medic | blaster2 (12f) | BLASTER2 20 | cfg | 1.2 s | ~low | support; heals allies |
| Floater | blaster (attak1, 14f) | ~10 ×n | cfg | 1.4 s | mid | + plasma ball (attak2, 25f) |
| Hover | blaster/rocket | 12 / ROCKET | mixed | — | mid | |

### Heavies & bosses

| Monster | Attack | dmg | src | anim | ≈DPS | notes |
|---|---|---|---|---|---|---|
| Tank | blaster2 (attak blast, 16f) | `damage` BLASTER2 | cfg | 1.6 s | mid | + reattack loop |
| Tank | rocket (attak fire, 9f, refires) | ROCKET 100 | cfg | 0.9 s/refire | **~111+** | refire loop sustains it |
| Tank | chaingun (attak chain, 29f) | `damage` ×n | cfg | 2.9 s | burst | |
| Supertank | bullets (attak1 6f loop) | 6 ×3 spread | inline | 0.6 s loop | sustained | wide spread |
| Supertank | rockets (attak2, 27f) | 50 | inline | — | burst | salvo |
| Supertank | grenade (attak4, 6f) | `damage` | inline | 0.6 s | burst | |
| Shambler | swing L/R (swingl/r, 9f) | `damage` (high) | cfg | 0.9 s | **high melee** | + smash / fireball (12f) |
| Boss2 (Hornet) | rockets ×4 salvo | ROCKET 100 | cfg | — | **burst-heavy** | + dual machinegun 8 |
| Boss2 | machinegun (R/L) | MACHINEGUN 8 | cfg | loop | mid | suppression |
| Jorg (Boss31) | BFG | 50 | inline | 6f loop | burst | + plasma (725 u/s) |
| Makron (Boss32) | railgun (attak rail, 16f) | RAILGUN 150 (boss may override ↑) | cfg | 1.6 s | **~94** | hitscan nuke |
| Makron | blaster beam (attak4, 26f) | `damage` | cfg | 2.6 s | mid | tracking volley |
| Makron | BFG | `damage` | cfg | — | burst AoE | |
| Guardian | blaster loop | 5 (default) | inline | loop | low-mid | + dabeam |
| Guardian | melee | 85 | inline | — | **huge** | point-blank crusher |
| Guardian (PSX) | blaster / dabeam / melee | 5 / 15 / 85 | inline | — | as above | |
| Arachnid | railgun (rails, 9–11f) | RAILGUN (35–40 boss inline elsewhere) | mixed | ~1.0 s | hitscan | + plasma, melee 15–30 |
| Runnertank | railgun / rocket / plasma / chain | `damage` | cfg | refire | high | fast heavy |

---

## 5. Findings

**Player side**
- **Plasma Beam (Heatbeam) is the runaway outlier** at ~1450 DPS single-target — it applies full
  `plasmabeam.damage` (145) every 100 ms think, and it *pierces*. Even bounded by ~20 cells/s it
  trivializes single targets and lines of monsters. This is the strongest weapon in the game by a wide
  margin.
- **The 20mm Cannon's 1-frame fire cycle (`Weapon_Generic(ent, 3, 4, …)`, `p_weapon.cpp:2467`) yields
  ~10 shots/s of 35-damage piercing hitscan ≈ 350 DPS.** Verify this is intended; a 1-frame cycle is
  unusual and may be much hotter than designed.
- **Chaingun (≈270 at full spin) and HyperBlaster (≈170)** are the best conventional sustained DPS;
  Machinegun (85) is the honest baseline. Shotguns are middling raw but spike hard with the **Energy
  Shells** benefit (SSG 154→273, Shotgun 73→164) — that one benefit roughly doubles output.
- **Railgun's Horde buff (150→225, `g_config.h:121`)** is a large single-target boost (~150 DPS hitscan).
- **Two "Disintegrator/Tracker" definitions disagree:** `weapon_disint_fire` (`p_weapon.cpp:2608`,
  7-frame cycle, ~200 DPS) vs `weapon_tracker_fire` (`rogue/p_rogue_weapon.cpp:274`, 5-frame cycle,
  ~280 DPS). Confirm which the live Disintegrator item binds to so the number is real.
- **DPS/ammo** tells a different story than raw DPS: BFG (~6/cell) and Chaingun (9/bullet) are
  ammo-hungry, while Railgun (150/slug) and Machinegun (85/bullet) are efficient.

**Monster side**
- **Damage is flat across waves** (see §2) — the only thing separating a wave-3 Gunner from a wave-40
  Gunner is count, champion flags, and the anti-domination system. If late waves feel like "more of the
  same," this is why.
- **Hardest hitters per bite:** Guardian melee (85), Makron railgun (150 hitscan), Boss2 rocket salvos
  (4×100), Berserk-KL melee (21–28), Gladiator melee (30–35), Shambler swings. Bosses lean on **burst**,
  not sustained DPS.
- **Trash troops (Soldier light, Flyer, Medic, basic Infantry, Parasite) deal trivial per-hit damage**
  (2–18) — they're a *volume* threat, exactly as intended.
- **Data is split between config and inline literals.** Migration toward `M_*_DMG` macros is clearly
  underway (Chick, Arachnid, Boss2, Tank, Hover now read config), but Gunner (bullet 6, grenade 40),
  Supertank (rocket 50, bullet 6), Gladiator/Berserk-KL/Chick melee rolls, Flyer/Brain melee, Infantry
  defaults, and Guardian still hardcode damage. **You cannot tune these from `g_config.h`** — a balance
  pass has to hunt them down in the `.cpp` files.

---

## 6. Recommendations (prioritized; no changes applied)

Each lever lists the file/field, current value, and a suggested direction. Numbers are starting points
for playtesting, not final.

**P1 — Tame the Plasma Beam outlier.** `PlasmaBeamConfig.damage` (`g_config.h:169`, 145) and/or its
per-think cadence. Options: drop per-tick damage to ~60–90, or make `Weapon_Repeating` fire it every
other think. Target a sustained ~400–600 single-target DPS so it's elite but not auto-win.

**P2 — Audit the 20mm 1-frame cycle.** `p_weapon.cpp:2467` `Weapon_Generic(ent, 3, 4, …)`. If 350 DPS
piercing isn't intended, widen the fire cycle to 2–3 frames (→ ~117–175 DPS) or raise ammo cost.

**P3 — Resolve the Disintegrator double-definition.** Confirm the bound fire function; delete/retire the
unused one so the weapon has a single source of truth (`p_weapon.cpp:2608` vs
`rogue/p_rogue_weapon.cpp:274`).

**P4 — Re-weight benefits to match their real power.** Energy Shells (~2× shotgun DPS) currently shares
the same 0.2 selection weight as weaker mods in `BENEFITS_SRC` (`horde/g_horde_benefits.cpp:19`).
Consider lowering its weight or raising its `min_level`, and review `cluster_prox` / `piercing_plasma`
similarly.

**P5 — Structural: centralize the inline monster damage.** Move the remaining hardcoded literals into
`MonsterStatsConfig::weapon_damage_overrides` (`g_config.h:505`) so all monster damage tunes from one
place. Concrete targets: Gunner bullet `6` / grenade `40` (`m_gunner.cpp:444,599`), Supertank rocket
`50` / bullet `6` (`m_supertank.cpp:544,569`), Guardian melee `85` (`m_guardian.cpp:527`),
Gladiator melee `30–35` (`m_gladiator.cpp:121`), Berserk-KL `21–28`/`15–22` (`m_berserk.cpp:1354,1368`).

**P6 — Decide on wave scaling intent.** With `use_sigmoid_scaling=false`, late-wave threat is purely
count/composition. If per-hit escalation is wanted, re-enabling a gentle damage curve (e.g. the existing
`monster_damage` sigmoid, max ~1.3–1.5) is a one-flag change in `g_config.h:659`; if the flat model is
intended, document it so it isn't mistaken for a bug.

---

## 7. How to verify these numbers

- **Re-derive a sample by hand:** pick one weapon (e.g. Rocket Launcher: `Weapon_Generic(ent, 4, 12,…)`
  → 8-frame cycle → 1.25/s × 110 = 137.5) and one monster attack (e.g. Chick railgun: 14-frame anim →
  1.4 s, 150 dmg → ~107) and confirm against the cited lines.
- **Confirm flat scaling:** with `developer 1`, observe that monster bite damage does not grow between
  early and late waves (consistent with `M_DamageModifier` returning 1.0).
- **Spot-check config vs inline tags** against §4 by opening two or three of the cited `.cpp` lines.
- **Playtest the outliers:** Plasma Beam, 20mm, and Energy-Shells shotgun are where the model predicts
  the biggest gap between "raw DPS" and "intended" — start there.
