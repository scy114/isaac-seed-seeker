import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "data" / "catalog" / "eden-start-catalog.j460.json"


def load_catalog() -> dict[str, object]:
    return json.loads(CATALOG_PATH.read_text(encoding="utf-8"))


def entry_map(catalog: dict[str, object]) -> dict[tuple[str, int], dict[str, object]]:
    return {
        (entry["kind"], entry["search_id"]): entry
        for entry in catalog["entries"]
    }


def test_catalog_shape_and_pinned_sources() -> None:
    catalog = load_catalog()

    assert catalog["schema_version"] == 1
    assert catalog["catalog_id"] == "huiji-j460-169621-169298"
    assert catalog["profile"]["game_build"] == "J460"
    assert catalog["counts"] == {
        "entries": 1078,
        "by_kind": {
            "active": 170,
            "passive": 551,
            "trinket": 188,
            "card": 97,
            "pill": 72,
        },
        "available_for_eden": {
            "active": 165,
            "passive": 542,
            "trinket": 187,
            "card": 59,
            "pill": 44,
        },
    }
    assert [(source["key"], source["revision"]) for source in catalog["sources"]] == [
        ("item", 169621),
        ("item-keywords", 169298),
    ]
    assert {source["license"]["spdx"] for source in catalog["sources"]} == {"CC0-1.0"}


def test_guppy_target_names_and_aliases() -> None:
    entries = entry_map(load_catalog())

    dead_cat = entries[("passive", 81)]
    assert dead_cat["name_zh"] == "嗝屁猫"
    assert dead_cat["name_en"] == "Dead Cat"
    assert dead_cat["quality"] == 3
    assert {"死猫", "9命猫", "九命猫"} <= set(dead_cat["aliases"])
    assert "gepimao" in dead_cat["pinyin"]
    assert dead_cat["available_for_eden"] is True

    guppys_head = entries[("active", 145)]
    assert guppys_head["name_zh"] == "嗝屁猫的头"
    assert guppys_head["name_en"] == "Guppy's Head"
    assert guppys_head["quality"] == 2
    assert "猫头" in guppys_head["aliases"]

    kids_drawing = entries[("trinket", 169)]
    assert kids_drawing["name_zh"] == "儿童涂鸦"
    assert kids_drawing["quality"] is None
    assert "猫片" in kids_drawing["aliases"]
    assert kids_drawing["available_for_eden"] is True


def test_profile_availability_and_pocket_id_normalization() -> None:
    entries = entry_map(load_catalog())

    assert entries[("trinket", 145)]["name_en"] == "Perfection"
    assert entries[("trinket", 145)]["available_for_eden"] is False

    normal = entries[("pill", 12)]
    horse = entries[("pill", 67)]
    assert normal["variant"] == "normal"
    assert horse["variant"] == "horse"
    assert horse["source_id"] == normal["source_id"] == 12
    assert horse["name_zh"] == f"大胶囊：{normal['name_zh']}"
    assert horse["available_for_eden"] is True

    possible_cards = {
        *range(1, 23),
        *range(42, 55),
        *range(56, 79),
        80,
    }
    available_cards = {
        entry["search_id"]
        for entry in entries.values()
        if entry["kind"] == "card" and entry["available_for_eden"]
    }
    assert available_cards == possible_cards


def test_catalog_keys_are_unique() -> None:
    catalog = load_catalog()
    keys = [(entry["kind"], entry["search_id"]) for entry in catalog["entries"]]

    assert len(keys) == len(set(keys)) == catalog["counts"]["entries"]


def test_all_collectibles_have_a_quality() -> None:
    catalog = load_catalog()
    collectibles = [
        entry for entry in catalog["entries"] if entry["kind"] in {"active", "passive"}
    ]

    assert len(collectibles) == 721
    assert all(entry["quality"] in {0, 1, 2, 3, 4} for entry in collectibles)
