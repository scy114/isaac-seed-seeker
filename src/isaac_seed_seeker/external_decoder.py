"""Adapter for an independently checked-out Eden J460 decoder.

The third-party implementation remains outside this repository.  This module only
loads its public-ish Python entry points and translates our SearchJob contract.
"""

from __future__ import annotations

import importlib
import json
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from .domain import base_trinket_id
from .job import SearchJob
from .seed_codec import UINT32_MAX


@dataclass(frozen=True)
class ItemTarget:
    trinket_id: int
    active_ids: frozenset[int]
    passive_ids: frozenset[int]

    @classmethod
    def from_job(cls, job: SearchJob) -> ItemTarget:
        active = job.filter.active_items
        passive = job.filter.passive_items
        if job.filter.trinket is None:
            raise ValueError("J460 item search requires filter.trinket")
        if active is None or not active.any_of:
            raise ValueError("J460 item search requires active_items.any")
        if passive is None or not passive.any_of:
            raise ValueError("J460 item search requires passive_items.any")
        if active.all_of or active.none_of or passive.all_of or passive.none_of:
            raise ValueError("J460 adapter currently supports only item any-filters")
        if job.filter.ranges or job.filter.card is not None or job.filter.pill is not None:
            raise ValueError("J460 adapter currently supports trinket plus active/passive items")
        return cls(
            trinket_id=base_trinket_id(job.filter.trinket),
            active_ids=active.any_of,
            passive_ids=passive.any_of,
        )

    def matches_items(self, values: Mapping[str, Any]) -> bool:
        return (
            int(values.get("active_id", 0)) in self.active_ids
            and int(values.get("passive_id", 0)) in self.passive_ids
        )


@dataclass(frozen=True)
class DecoderSearchResult:
    matches: tuple[dict[str, Any], ...]
    start_u32: int
    last_u32: int
    next_start_u32: int | None
    scanned: int
    elapsed_sec: float
    decoder_commit: str | None

    def to_dict(self) -> dict[str, Any]:
        return {
            "matches": list(self.matches),
            "count": len(self.matches),
            "start_u32": self.start_u32,
            "last_u32": self.last_u32,
            "next_start_u32": self.next_start_u32,
            "scanned": self.scanned,
            "elapsed_sec": self.elapsed_sec,
            "decoder_commit": self.decoder_commit,
            "verification": "requires_game_observer",
        }


def _validate_decoder_dir(decoder_dir: Path) -> None:
    required = (
        decoder_dir / "predict_eden.py",
        decoder_dir / "tools" / "eden_reverse_fast.py",
        decoder_dir / "tools" / "eden_predict.py",
        decoder_dir / "tools" / "eden_j460.py",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise ValueError(f"decoder directory is missing required files: {missing}")


def _decoder_commit(decoder_dir: Path) -> str | None:
    completed = subprocess.run(
        ["git", "-C", str(decoder_dir), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    return completed.stdout.strip() or None if completed.returncode == 0 else None


def _load_decoder(decoder_dir: Path) -> dict[str, Any]:
    _validate_decoder_dir(decoder_dir)
    tools_dir = (decoder_dir / "tools").resolve()
    loaded_tools = sys.modules.get("tools")
    if loaded_tools is not None:
        locations = {Path(item).resolve() for item in getattr(loaded_tools, "__path__", [])}
        if tools_dir not in locations:
            raise RuntimeError("a different top-level 'tools' package is already loaded")
    decoder_text = str(decoder_dir.resolve())
    if decoder_text not in sys.path:
        sys.path.insert(0, decoder_text)
    reverse = importlib.import_module("tools.eden_reverse")
    fast = importlib.import_module("tools.eden_reverse_fast")
    predict = importlib.import_module("tools.eden_predict")
    eden = importlib.import_module("tools.eden_j460")
    seeds = importlib.import_module("tools.seed_codec")
    if not fast.numba_available():
        raise RuntimeError("decoder fast path needs its optional numpy and numba dependencies")
    return {
        "criteria": reverse.EdenReverseCriteria,
        "pack_tables": fast.pack_tables,
        "pack_criteria": fast.pack_criteria,
        "fast_match_seed": fast.fast_match_seed,
        "load_proc_table": predict.load_proc_table,
        "eden_starting_items": eden.eden_starting_items,
        "seed_label": seeds.custom_start_seed_label,
    }


def search_j460(
    job: SearchJob,
    *,
    decoder_dir: str | Path,
    proc_table: str | Path,
    trinket_pool: str | Path,
    start_u32: int = 1,
    max_scan: int = 5_000_000,
) -> DecoderSearchResult:
    """Search one contiguous seed window using the external decoder's fast kernel."""
    target = ItemTarget.from_job(job)
    root = Path(decoder_dir).resolve()
    proc_path = Path(proc_table).resolve()
    trinket_path = Path(trinket_pool).resolve()
    if not proc_path.is_file() or not trinket_path.is_file():
        raise ValueError("proc table and trinket pool snapshots are both required")
    if not 1 <= start_u32 <= UINT32_MAX:
        raise ValueError("start_u32 must be within 1..4294967295")
    if max_scan <= 0:
        raise ValueError("max_scan must be positive")

    api = _load_decoder(root)
    table = api["load_proc_table"](proc_path)
    criteria = api["criteria"](trinket_id=target.trinket_id)
    tables = api["pack_tables"](table, trinket_path)
    packed = api["pack_criteria"](criteria, tables)

    last_u32 = min(UINT32_MAX, start_u32 + max_scan - 1)
    matches: list[dict[str, Any]] = []
    started = time.perf_counter()
    scanned = 0
    for seed_u32 in range(start_u32, last_u32 + 1):
        scanned += 1
        if not api["fast_match_seed"](seed_u32, packed):
            continue
        seed = api["seed_label"](seed_u32)
        if seed is None:
            continue
        items = api["eden_starting_items"](
            seed_u32,
            table=table,
            trinket_pool_path=trinket_path,
        )
        if not target.matches_items(items):
            continue
        matches.append(
            {
                "seed": seed,
                "seed_u32": seed_u32,
                "trinket_id": target.trinket_id,
                "active_id": int(items["active_id"]),
                "passive_id": int(items["passive_id"]),
            }
        )
        if len(matches) >= job.max_results:
            last_u32 = seed_u32
            break

    next_start = last_u32 + 1 if last_u32 < UINT32_MAX else None
    return DecoderSearchResult(
        matches=tuple(matches),
        start_u32=start_u32,
        last_u32=last_u32,
        next_start_u32=next_start,
        scanned=scanned,
        elapsed_sec=round(time.perf_counter() - started, 3),
        decoder_commit=_decoder_commit(root),
    )


def render_candidate_job(source_path: str | Path, result: DecoderSearchResult) -> str:
    if not result.matches:
        raise ValueError("cannot render a candidate job without matches")
    source = json.loads(Path(source_path).read_text(encoding="utf-8"))
    source["candidates"] = {"values": [match["seed"] for match in result.matches]}
    source["id"] = f"{source['id']}-candidates-{result.start_u32:08x}-{result.last_u32:08x}"
    return json.dumps(source, ensure_ascii=False, indent=2) + "\n"
