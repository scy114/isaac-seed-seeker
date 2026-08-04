"""Detect immutable facts used to identify a game search profile."""

from __future__ import annotations

import hashlib
import json
import re
import xml.etree.ElementTree as ET
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

_VERSION_RE = re.compile(r"Binding of Isaac: Repentance\+\s+(v[^\s]+)")
_MOD_RE = re.compile(r"LOADED MOD .*[\\/]mods[\\/](.*?)(?:[\\/]content[\\/]?)?$", re.IGNORECASE)


@dataclass(frozen=True)
class GameProfile:
    schema_version: int
    id: str
    game_version: str
    game_build: str
    game_dir: str
    item_count: int | None
    resources: dict[str, str]
    loaded_mods: tuple[str, ...]
    mods_hash: str
    accuracy: str = "game-observed"

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _latest_session(lines: Iterable[str]) -> tuple[str, tuple[str, ...]]:
    materialized = list(lines)
    version = "unknown"
    session_start = 0
    for index, line in enumerate(materialized):
        match = _VERSION_RE.search(line)
        if match:
            version = match.group(1)
            session_start = index
    loaded_mods = set()
    for line in materialized[session_start:]:
        match = _MOD_RE.search(line.strip())
        if match:
            loaded_mods.add(match.group(1).replace("\\", "/").strip("/"))
    return version, tuple(sorted(loaded_mods))


def _maximum_xml_id(path: Path) -> int | None:
    if not path.is_file():
        return None
    maximum = 0
    for element in ET.parse(path).iter():
        raw_id = element.attrib.get("id")
        if raw_id and raw_id.isdigit():
            maximum = max(maximum, int(raw_id))
    return maximum or None


def detect_profile(profile_id: str, log_path: str | Path, game_dir: str | Path) -> GameProfile:
    log = Path(log_path)
    root = Path(game_dir)
    if not log.is_file():
        raise ValueError(f"game log does not exist: {log}")
    if not root.is_dir():
        raise ValueError(f"game directory does not exist: {root}")

    game_version, loaded_mods = _latest_session(
        log.read_text(encoding="utf-8", errors="replace").splitlines()
    )
    build = game_version.rsplit(".", 1)[-1] if game_version != "unknown" else "unknown"
    mods_hash = hashlib.sha256("\n".join(loaded_mods).encode("utf-8")).hexdigest()
    resources_root = root / "extracted_resources" / "resources"
    resources: dict[str, str] = {}
    for name in ("items.xml", "items_metadata.xml", "itempools.xml"):
        candidate = resources_root / name
        if candidate.is_file():
            resources[name] = _sha256(candidate)

    return GameProfile(
        schema_version=1,
        id=profile_id,
        game_version=game_version,
        game_build=build,
        game_dir=str(root.resolve()),
        item_count=_maximum_xml_id(resources_root / "items.xml"),
        resources=resources,
        loaded_mods=loaded_mods,
        mods_hash=mods_hash,
    )


def load_profile(path: str | Path) -> GameProfile:
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or int(raw.get("schema_version", 0)) != 1:
        raise ValueError("unsupported profile")
    return GameProfile(
        schema_version=1,
        id=str(raw["id"]),
        game_version=str(raw.get("game_version", "unknown")),
        game_build=str(raw.get("game_build", "unknown")),
        game_dir=str(raw.get("game_dir", "")),
        item_count=int(raw["item_count"]) if raw.get("item_count") is not None else None,
        resources={str(key): str(value) for key, value in dict(raw.get("resources", {})).items()},
        loaded_mods=tuple(str(value) for value in raw.get("loaded_mods", [])),
        mods_hash=str(raw.get("mods_hash", hashlib.sha256(b"").hexdigest())),
        accuracy=str(raw.get("accuracy", "game-observed")),
    )
