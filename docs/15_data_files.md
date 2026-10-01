# 데이터 파일 구조

게임 내용(블록·아이템·레시피·연구·각인·몬스터 등)은 모두 JSON 파일로 두고, 코드는 이를 읽기만 한다. 수치를 바꾸거나 모드를 만들 때 코드를 고칠 필요가 없다. 세부 구현은 Claude Code가 이 규칙에 맞춰 만든다.

## 폴더

```
game/
  data/aurora/            ← 기본 내용 (네임스페이스 aurora)
    blocks/ items/ materials/ recipes/ tech/ enchants/ spells/
    buildings/ jobs/ traits/ laws/ monsters/ bosses/ loot/
    factions/ events/ biomes/ structures/ vehicles/ parts/
    worldgen/                ← 월드 생성 설정 (P0-4: flat.json)
  assets/aurora/
    textures/{block,item,entity,particle,ui}/
    models/ animations/ sounds/ shaders/ lang/
  mods/<모드이름>/data, assets   ← 같은 구조, 다른 네임스페이스
```

- **ID 형식**: `네임스페이스:경로` (예: `aurora:steel_ingot`, `aurora:loot/ogre`). 네임스페이스와 경로의 각 구간은 영문 소문자·숫자·밑줄(`[a-z0-9_]`)만 쓰고, 경로는 `/`로 구간을 나눌 수 있다(빈 구간, 앞뒤 `/`, `.`은 안 됨). 블록 ID는 `/` 없는 한 구간이다.
- **번역**: 표시 이름은 `lang/ko_kr.json`의 키(`item.aurora.steel_ingot`)로. 영어는 `en_us.json`.
- **로딩 순서**: aurora → 모드(이름순). 다른 묶음이 같은 ID를 정의하면 나중 것이 덮어쓴다(로그에 알림). 같은 묶음 안에서 같은 ID가 두 번 나오면 오류다.
- **검사**: 규칙은 엔진의 데이터 로더 코드에 둔다. 폴더별 JSON Schema 파일은 미뤘고, 편집기 자동완성이 필요해질 때 규칙을 어떻게 공유할지 정한다. 시작할 때 모든 파일을 끝까지 검사해 틀린 파일·필드(JSON 포인터)·이유를 전부 로그로 보여주고, 오류가 하나라도 있으면 게임을 시작하지 않는다(경고만 있으면 시작). 개발 중에는 `/reload`로 게임을 끄지 않고 다시 읽는다(실패하면 기존 데이터를 그대로 쓴다).
- **게임 폴더**: `--game-dir <폴더>`로 지정하면 그 폴더만 쓴다(없거나 데이터가 틀려도 다른 폴더로 넘어가지 않음). 지정하지 않으면 실행 위치의 `./game`, 그다음 빌드한 소스 트리의 `game` 중 `data/aurora`가 있는 첫 폴더를 쓴다. 후보가 있는데 권한 등으로 확인할 수 없으면 다음 후보로 넘어가지 않고 오류다.
- **없는 폴더와 읽을 수 없는 폴더**: 없는 폴더는 규칙대로 허용한다(모드의 `blocks/` 등). 있는데 읽을 수 없는 폴더·파일·텍스처(권한, 순환하거나 대상이 없는 링크, 입출력 오류)는 오류다. 대상이 없는 링크는 경로 끝에 있든 중간 폴더에 있든 "없는 경로"로 보지 않는다. 폴더가 있어야 할 자리에 파일이 있는 경로(예: `textures/block`이 파일)도 오류다. 정상 링크는 따라간다. 데이터가 조용히 빠진 채 시작하지 않게 하기 위해서다.

## 블록 파일 (`blocks/*.json`, P0-3)

파일 하나에 블록 하나. 파일 이름은 ID와 달라도 된다.

| 필드 | 필수 | 형식 |
| --- | --- | --- |
| `id` | 예 | 블록 ID. `aurora:air`, `aurora:unknown`은 엔진 내장이라 쓸 수 없다 |
| `hardness` | `unbreakable`이 없으면 예 | 유한한 0 이상 수 |
| `unbreakable` | 아니오 | `true`만 쓸 수 있다(기반암처럼 부서지지 않음). `hardness`와 둘 중 정확히 하나만 적는다(`false`를 적거나 둘 다 적으면 오류) |
| `explosion_resistance` | 아니오 | 유한한 0 이상 수 |
| `light` | 아니오 | 정수 0~15 (기본 0) |
| `render` | 아니오 | `opaque`(기본) / `cutout` / `translucent` |
| `solid` | 아니오 | 충돌 여부 (기본 true) |
| `textures` | 예 | 아래 참고 |
| `states` | 아니오 | `{ "속성": ["값1", "값2", ...] }`. 이름·값은 `[a-z0-9_]`, 빈 배열·중복 값은 오류 |
| `default_state` | 아니오 | `{ "속성": "값" }`. 빠진 속성은 `states`에 적은 첫 값 |

- **텍스처**: 면마다 `north`/`south`/`east`/`west` → `side` → `all`, 위·아래는 `top`/`bottom` → `all` 순서로 찾고, 여섯 면이 모두 정해져야 한다. `emissive`는 선택. 값은 `[네임스페이스:]경로`이고, 네임스페이스를 빼면 **블록 파일이 들어 있는 `data/<ns>` 폴더의 ns**를 쓴다(블록 ID의 ns가 아님). 파일은 `assets/<ns>/textures/<경로>.png`를 나중에 읽는 묶음부터 찾으며, 없으면 오류.
- **텍스처 이미지** (P0-5): 블록 로더가 찾은 바로 그 파일을 창을 띄우기 전에 읽어 검사한다(묶음을 다시 찾지 않으므로, 나중 묶음의 파일이 깨졌으면 앞 묶음으로 넘어가지 않고 오류). PNG만 받는다(파일 첫 8바이트가 PNG 서명이어야 함). 블록 면 텍스처는 정확히 32×32이고, 아니면 파일과 실제 크기를 알리는 오류다. 오류가 있으면 블록 데이터처럼 종료 코드 1. `emissive` 이미지는 쉐이더 단계(P0-10)에서 읽는다. 면 텍스처가 없는 상태(내장 `aurora:unknown`)는 자홍·검정 체크무늬 "없음" 텍스처로 그린다.
- **쉐이더** (P0-5): `assets/aurora/shaders/chunk.vert`, `chunk.frag`(GLSL 4.50)를 기본 게임 묶음에서 읽는다. 컴파일 오류는 파일과 드라이버 로그를 남기고 종료 코드 1. 모드가 쉐이더를 바꾸는 규칙과 실행 중 다시 읽기는 P0-10에서 정한다.
- **나중 단계에서 읽는 필드**: `tool`, `min_tool_rank`, `drops`(P1), `generation`(P0-8)은 지금은 의미를 검사하지 않고 받아 둔다. JSON 문법이 틀리면 지금도 오류다. 그 밖의 모르는 필드는 경고(오타 확인용).
- **상태 ID**: 속성 값 조합마다 16비트 번호를 붙인다. 공기 0, unknown 1이 먼저이고, 나머지는 덮어쓰기까지 끝난 최종 블록을 ID 순으로 정렬해 번호를 매긴다. 전체 65,536개(데이터 블록 65,534개)를 넘으면 오류. 이 번호는 실행 중에만 쓰고 저장하지 않는다. 저장·전송에는 상태 문자열 `aurora:oak_log[axis=y]`(속성 이름순, 속성 없으면 `aurora:stone`)을 쓴다.

## 평지 프리셋 (`worldgen/flat.json`, P0-4)

평지 월드의 층 구성. 파일 하나를 통째로 쓰며, 나중에 읽는 묶음(모드)에 이 파일이 있으면 앞 묶음의 것을 대신한다. 어느 묶음에도 없으면 오류다. 나중 묶음의 자리에 파일 대신 폴더나 대상이 없는 링크가 있으면 앞 묶음으로 넘어가지 않고 오류다.

```json
{
  "layers": [
    { "block": "aurora:stone", "height": 124 },
    { "block": "aurora:dirt", "height": 3 },
    { "block": "aurora:grass_block", "height": 1 }
  ]
}
```

| 필드 | 필수 | 형식 |
| --- | --- | --- |
| `layers` | 예 | 비어 있지 않은 배열. 맨 아래(y −64)부터 위로 쌓고, 그 위는 공기 |
| `layers[].block` | 예 | 상태 문자열. `aurora:oak_log`처럼 ID만 쓰면 그 블록의 기본 상태(`axis=y`), `aurora:oak_log[axis=x]`처럼 속성을 줄 수도 있다. 없는 블록·속성·값은 오류다(`aurora:unknown`으로 바꾸지 않는다) |
| `layers[].height` | 예 | 1 이상의 정수. `0`, 음수, 소수(`2.0`, `1e3` 포함), 문자열, `true`는 오류 |

- 높이는 더하기 전에 남은 월드 높이와 비교한다. 합이 384(월드 전체 높이)까지는 되고, 넘으면 넘친 층의 `/layers/<n>/height`에 오류를 낸다.
- 기본 구성은 돌 y −64\~59, 흙 60\~62, 풀 63이다. 지표 위 첫 빈 칸은 64다. 기반암 블록은 아직 없어서 넣지 않았다.
- 문법 오류·중복 키·필드 누락은 오류, 모르는 필드는 경고다. 블록 파일과 같이 오류가 하나라도 있으면 창을 만들기 전에 종료 코드 1로 끝낸다.

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

블록은 숫자 상태 ID가 아니라 상태 문자열 팔레트로 저장한다(데이터가 바뀌어 번호가 달라져도 그대로 읽히게). 읽을 때 없는 블록은 `aurora:unknown`으로 대신한다. 원래 문자열을 보존하는 방법은 P0-12에서 정한다.

저장 파일에는 데이터 버전을 기록하고, 버전이 올라가면 변환기로 자동 업그레이드한다.
