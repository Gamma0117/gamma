# 데이터 파일 구조

게임 내용(블록·아이템·레시피·연구·각인·몬스터 등)은 모두 JSON 파일로 두고, 코드는 이를 읽기만 한다. 수치를 바꾸거나 모드를 만들 때 코드를 고칠 필요가 없다. 세부 구현은 Claude Code가 이 규칙에 맞춰 만든다.

## 폴더

```
game/
  data/aurora/            ← 기본 내용 (네임스페이스 aurora)
    blocks/ items/ materials/ recipes/ tech/ enchants/ spells/
    buildings/ jobs/ traits/ laws/ monsters/ bosses/ loot/
    factions/ events/ biomes/ structures/ vehicles/ parts/
  assets/aurora/
    textures/{block,item,entity,particle,ui}/
    models/ animations/ sounds/ shaders/ lang/
  mods/<모드이름>/data, assets   ← 같은 구조, 다른 네임스페이스
```

- **ID 형식**: `네임스페이스:이름` (예: `aurora:steel_ingot`). 영문 소문자·밑줄만.
- **번역**: 표시 이름은 `lang/ko_kr.json`의 키(`item.aurora.steel_ingot`)로. 영어는 `en_us.json`.
- **로딩 순서**: aurora → 모드(이름순). 같은 ID면 나중 것이 덮어쓴다.
- **검사**: 폴더마다 JSON 스키마를 두고, 시작 시 틀린 파일과 없는 ID 참조를 모두 로그로 보여준다. 개발 중에는 `/reload`로 게임을 끄지 않고 다시 읽는다.

## 예시

**재료 (materials/aurora\_alloy.json)** — 합금 순위·배율이 여기 모인다.

```json
{
  "id": "aurora:aurora_alloy",
  "rank": 1,
  "stats": { "attack": 14.2, "defense": 14.8, "durability": 13.5, "weight": 0.6, "mana_conductivity": 18.0 },
  "enchant_slots": 5,
  "purity": { "source": ["aurora:titanium", "aurora:tungsten"], "rule": "min" },
  "traits": ["special_enchant_power_25"],
  "color": "#6fb0c8"
}
```

**블록 (blocks/mana\_ore\_clear.json)**

```json
{
  "id": "aurora:mana_ore_clear",
  "hardness": 4.5,
  "tool": "pickaxe", "min_tool_rank": 6,
  "light": 7,
  "textures": { "all": "block/mana_ore_clear", "emissive": "block/mana_ore_clear_e" },
  "drops": "aurora:loot/mana_ore_clear",
  "generation": { "y_min": -30, "y_max": 0, "veins_per_chunk": 1.5, "vein_size": [2, 5], "biome_bonus": { "aurora:mana_crystal_plains": 3.0 } }
}
```

**아이템 (items/steel\_sword.json)**

```json
{
  "id": "aurora:steel_sword",
  "type": "melee", "weapon_class": "sword",
  "material": "aurora:steel",
  "base_damage": 8, "attack_speed": 1.6,
  "durability": 800,
  "model": "item/steel_sword", "animation_set": "aurora:sword"
}
```

**레시피 (recipes/steel\_ingot.json)**

```json
{
  "id": "aurora:steel_ingot_blast",
  "station": "aurora:blast_furnace",
  "inputs": [ { "item": "aurora:iron_ingot", "count": 1 }, { "item": "aurora:coal", "count": 2 } ],
  "output": { "item": "aurora:steel_ingot", "count": 1 },
  "time_ticks": 1500,
  "requires_tech": "aurora:ind_2",
  "worker_job": "aurora:smelter"
}
```

**연구 (tech/mag\_6.json)**

```json
{
  "id": "aurora:mag_6",
  "branch": "magic", "era": "industrial",
  "prerequisites": ["aurora:mag_5"],
  "cost": { "rp": 3500 },
  "unlocks": { "recipes": ["aurora:mana_battery"], "buildings": ["aurora:charging_station"], "stations": ["aurora:enchant_table_2"] }
}
```

**각인 (enchants/attack.json)**

```json
{
  "id": "aurora:attack",
  "targets": ["melee", "ranged"],
  "levels": [ { "value": 0.08, "dust": "aurora:mana_dust_murky", "dust_count": 1, "success": 0.95, "time_ticks": 200 } ],
  "stat": "damage_multiplier"
}
```

(levels 배열에 Lv1\~Lv5를 모두 적는다. 여기서는 Lv1만 예시.)

**몬스터 (monsters/ogre\_warrior.json)**

```json
{
  "id": "aurora:ogre_warrior",
  "tier": 4, "elite": false,
  "health_mult": 1.12, "attack_mult": 1.18,
  "speed": 0.8,
  "behaviors": ["melee_chase", "break_blocks", "target_buildings"],
  "model": "entity/ogre", "animation_set": "aurora:ogre",
  "loot": "aurora:loot/ogre",
  "spawn": { "biomes": ["aurora:mountains"], "light_max": 7, "group": [1, 2] }
}
```

체력·공격은 등급 기준값(T4 = 98 / 11)에 배수를 곱하는 방식이라, 등급 공식을 바꾸면 모든 몬스터가 함께 따라간다.

**건물 (buildings/smithy.json)**

```json
{
  "id": "aurora:smithy",
  "core_block": "aurora:anvil",
  "min_size": [7, 7], "requires_roof": true,
  "required_furniture": { "aurora:anvil": 1, "aurora:furnace": 1 },
  "job": "aurora:blacksmith", "workers_per_core": 1, "max_cores": 3,
  "default_blueprint": "aurora:blueprints/smithy_gothic",
  "unlock": { "tech": "aurora:ind_1" }
}
```

**세력 (factions/assyria.json)**

```json
{
  "id": "aurora:assyria",
  "start_rank": 9, "start_era": "industrial",
  "spawn_distance": [6000, 9000],
  "personality": { "aggression": 0.9, "trade": 0.3, "research_speed": 1.5 },
  "unique_units": ["aurora:lamassu_walker", "aurora:immortal_guard", "aurora:bronze_airship"],
  "victory_target": true
}
```

## 저장 파일

| 파일 | 내용 | 형식 |
| --- | --- | --- |
| `world.json` | 시드, 난이도, 게임 규칙, 버전 | JSON |
| `region/r.X.Z.bin` | 청크 32×32개씩 묶음 | 바이너리 + LZ4 |
| `kingdoms/<id>.bin` | 왕국·주민·연구·법령·외교 | 바이너리 + LZ4 |
| `players/<id>.bin` | 인벤토리·레벨·위치 | 바이너리 |

저장 파일에는 데이터 버전을 기록하고, 버전이 올라가면 변환기로 자동 업그레이드한다.
