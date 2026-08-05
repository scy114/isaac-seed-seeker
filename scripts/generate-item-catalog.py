"""Generate the offline Eden item-name catalog from pinned Wiki snapshots."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "data" / "catalog" / "sources.lock.json"
PROFILE_PATH = ROOT / "data" / "catalog" / "profile-j460.json"
OUTPUT_PATH = ROOT / "data" / "catalog" / "eden-start-catalog.j460.json"
SOURCE_DIR = ROOT / "data" / "catalog" / "sources"

KIND_ORDER = {"active": 0, "passive": 1, "trinket": 2, "card": 3, "pill": 4}
EDEN_CARD_IDS = frozenset((*range(1, 23), *range(42, 55), *range(56, 79), 80))
EDEN_NORMAL_PILL_IDS = frozenset(range(1, 23))


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def split_values(value: object) -> list[str]:
    if value is None:
        return []
    return [part.strip() for part in str(value).split(";") if part.strip()]


def unique(values: Iterable[str], excluded: Iterable[str] = ()) -> list[str]:
    excluded_keys = {value.casefold() for value in excluded if value}
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        key = value.casefold()
        if key in excluded_keys or key in seen:
            continue
        seen.add(key)
        result.append(value)
    return result


def rows_by_field(document: dict[str, Any]) -> tuple[dict[str, int], list[list[Any]]]:
    fields = {
        field["name"]: index for index, field in enumerate(document["schema"]["fields"])
    }
    return fields, document["data"]


def source_snapshot(lock_entry: dict[str, Any]) -> dict[str, Any]:
    path = SOURCE_DIR / f"huiji-{lock_entry['key']}.{lock_entry['revision']}.json"
    snapshot = load_json(path)
    source = snapshot["source"]
    for field in ("key", "title", "page_url", "revision", "timestamp", "sha1", "license"):
        if source[field] != lock_entry[field]:
            raise ValueError(f"{path}: source {field} does not match sources.lock.json")
    return snapshot


def wiki_entry(
    row: list[Any],
    fields: dict[str, int],
    keyword: dict[str, Any],
) -> dict[str, Any]:
    name_zh = str(row[fields["namezh"]])
    name_en = str(row[fields["nameen"]])
    aliases = unique(
        [
            *split_values(row[fields["namelist"]]),
            *split_values(keyword.get("aliases")),
        ],
        excluded=(name_zh, name_en),
    )
    return {
        "wiki_key": str(row[fields["page"]]),
        "source_id": int(row[fields["id"]]),
        "name_zh": name_zh,
        "name_en": name_en,
        "aliases": aliases,
        "pinyin": split_values(keyword.get("pinyin")),
    }


def build_catalog(
    item_snapshot: dict[str, Any],
    keyword_snapshot: dict[str, Any],
    profile: dict[str, Any],
) -> dict[str, Any]:
    item_fields, item_rows = rows_by_field(item_snapshot["document"])
    keyword_fields, keyword_rows = rows_by_field(keyword_snapshot["document"])
    keywords = {
        str(row[keyword_fields["page"]]): {
            "aliases": row[keyword_fields["name_alias"]],
            "pinyin": row[keyword_fields["PinyinIndex"]],
        }
        for row in keyword_rows
    }
    collectible_profile = {int(entry["id"]): entry for entry in profile["collectibles"]}
    trinket_profile = {int(entry["id"]): entry for entry in profile["trinkets"]}

    entries: list[dict[str, Any]] = []
    for row in item_rows:
        base = wiki_entry(row, item_fields, keywords.get(str(row[item_fields["page"]]), {}))
        source_id = base["source_id"]
        source_type = str(row[item_fields["type"]])
        if source_type == "道具":
            profile_entry = collectible_profile.get(source_id)
            if profile_entry is None:
                raise ValueError(f"collectible {source_id} is absent from the J460 Profile")
            entries.append(
                {
                    **base,
                    "kind": profile_entry["kind"],
                    "search_id": source_id,
                    "variant": "normal",
                    "available_for_eden": bool(profile_entry["available_for_eden"]),
                }
            )
        elif source_type == "饰品":
            profile_entry = trinket_profile.get(source_id)
            if profile_entry is None:
                raise ValueError(f"trinket {source_id} is absent from the J460 Profile")
            entries.append(
                {
                    **base,
                    "kind": "trinket",
                    "search_id": source_id,
                    "variant": "normal",
                    "available_for_eden": bool(profile_entry["available_for_eden"]),
                }
            )
        elif source_type == "卡牌":
            entries.append(
                {
                    **base,
                    "kind": "card",
                    "search_id": source_id,
                    "variant": "normal",
                    "available_for_eden": source_id in EDEN_CARD_IDS,
                }
            )
        elif source_type == "胶囊":
            entries.append(
                {
                    **base,
                    "kind": "pill",
                    "search_id": source_id,
                    "variant": "normal",
                    "available_for_eden": source_id in EDEN_NORMAL_PILL_IDS,
                }
            )
            if source_id in EDEN_NORMAL_PILL_IDS:
                entries.append(
                    {
                        **base,
                        "wiki_key": f"{base['wiki_key']}-horse",
                        "kind": "pill",
                        "search_id": source_id + 55,
                        "variant": "horse",
                        "name_zh": f"大胶囊：{base['name_zh']}",
                        "name_en": f"Horse Pill: {base['name_en']}",
                        "aliases": unique(
                            [
                                *base["aliases"],
                                base["name_zh"],
                                base["name_en"],
                                *(f"大胶囊：{alias}" for alias in base["aliases"]),
                            ]
                        ),
                        "pinyin": unique(
                            [*base["pinyin"], *(f"dajiaonang{value}" for value in base["pinyin"])]
                        ),
                        "available_for_eden": True,
                    }
                )
        else:
            raise ValueError(f"unsupported Wiki item type: {source_type}")

    entries.sort(key=lambda entry: (KIND_ORDER[entry["kind"]], entry["search_id"], entry["variant"]))
    keys = [(entry["kind"], entry["search_id"]) for entry in entries]
    if len(keys) != len(set(keys)):
        duplicates = [key for key, count in Counter(keys).items() if count > 1]
        raise ValueError(f"duplicate catalog keys: {duplicates}")

    source_metadata = [item_snapshot["source"], keyword_snapshot["source"]]
    by_kind = Counter(entry["kind"] for entry in entries)
    available_by_kind = Counter(
        entry["kind"] for entry in entries if entry["available_for_eden"]
    )
    return {
        "schema_version": 1,
        "catalog_id": (
            f"huiji-j460-{item_snapshot['source']['revision']}"
            f"-{keyword_snapshot['source']['revision']}"
        ),
        "profile": {
            "id": profile["profile_id"],
            "game_version": profile["game_version"],
            "game_build": profile["game_build"],
            "source_sha256": profile["source_sha256"],
        },
        "sources": source_metadata,
        "normalization": {
            "trinket": "golden trinkets use the base trinket ID",
            "horse_pill": "search_id = source pill effect ID + 55",
            "backend_contract": "the WebUI submits search_id values; names never reach the RNG kernel",
        },
        "counts": {
            "entries": len(entries),
            "by_kind": dict(sorted(by_kind.items(), key=lambda item: KIND_ORDER[item[0]])),
            "available_for_eden": dict(
                sorted(available_by_kind.items(), key=lambda item: KIND_ORDER[item[0]])
            ),
        },
        "entries": entries,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH)
    parser.add_argument("--check", action="store_true", help="fail if output is not up to date")
    arguments = parser.parse_args()

    lock = load_json(LOCK_PATH)
    sources = {entry["key"]: entry for entry in lock["sources"]}
    item_snapshot = source_snapshot(sources["item"])
    keyword_snapshot = source_snapshot(sources["item-keywords"])
    profile = load_json(PROFILE_PATH)
    catalog = build_catalog(item_snapshot, keyword_snapshot, profile)
    rendered = json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"

    if arguments.check:
        if not arguments.output.is_file() or arguments.output.read_text(encoding="utf-8") != rendered:
            raise SystemExit(f"catalog is stale: run {Path(__file__).name}")
        return

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(rendered, encoding="utf-8")
    print(
        f"generated {arguments.output} with {catalog['counts']['entries']} entries "
        f"for {catalog['profile']['id']}"
    )


if __name__ == "__main__":
    main()
