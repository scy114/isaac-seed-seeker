"""Search job parsing and candidate generation."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator, Mapping

from .domain import EdenFilter
from .seed_codec import UINT32_MAX, normalize_seed, seed_to_string, string_to_seed


@dataclass(frozen=True)
class CandidateSpec:
    values: tuple[str, ...] = ()
    start: int | None = None
    end: int | None = None
    step: int = 1
    max_candidates: int = 1000

    @classmethod
    def from_dict(cls, value: object) -> CandidateSpec:
        if not isinstance(value, Mapping):
            raise ValueError("candidates must be an object")
        if "values" in value:
            if set(value) != {"values"}:
                raise ValueError("explicit candidates only accept values")
            raw_values = value["values"]
            if not isinstance(raw_values, list) or not raw_values:
                raise ValueError("candidates.values must be a non-empty list")
            normalized = tuple(normalize_seed(str(seed)) for seed in raw_values)
            for seed in normalized:
                if string_to_seed(seed) == 0:
                    raise ValueError("zero is not a valid game RNG seed")
            return cls(values=normalized, max_candidates=len(normalized))
        allowed = {"start", "end", "step", "max_candidates"}
        unknown = set(value) - allowed
        if unknown:
            raise ValueError(f"candidates has unknown keys: {sorted(unknown)}")
        if "start" not in value or "end" not in value:
            raise ValueError("range candidates require start and end")
        start = int(value["start"])
        end = int(value["end"])
        step = int(value.get("step", 1))
        maximum = int(value.get("max_candidates", 1000))
        if not 1 <= start <= UINT32_MAX or not 1 <= end <= UINT32_MAX:
            raise ValueError("candidate range must be within 1..4294967295")
        if start > end:
            raise ValueError("candidates.start cannot exceed end")
        if step <= 0 or not 1 <= maximum <= 100000:
            raise ValueError("invalid candidate step or max_candidates")
        return cls(start=start, end=end, step=step, max_candidates=maximum)

    def iter_seed_strings(self) -> Iterator[str]:
        if self.values:
            yield from self.values
            return
        assert self.start is not None and self.end is not None
        produced = 0
        current = self.start
        while current <= self.end and produced < self.max_candidates:
            yield seed_to_string(current)
            current += self.step
            produced += 1


@dataclass(frozen=True)
class SearchJob:
    schema_version: int
    id: str
    profile_id: str
    candidates: CandidateSpec
    filter: EdenFilter
    max_results: int = 20
    character: str = "eden"

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> SearchJob:
        allowed = {
            "schema_version", "id", "profile_id", "character", "candidates", "filter", "max_results"
        }
        unknown = set(value) - allowed
        if unknown:
            raise ValueError(f"job has unknown keys: {sorted(unknown)}")
        if int(value.get("schema_version", 0)) != 1:
            raise ValueError("unsupported job schema_version")
        job_id = str(value.get("id", "")).strip()
        profile_id = str(value.get("profile_id", "")).strip()
        character = str(value.get("character", "eden")).lower()
        if not job_id or not profile_id:
            raise ValueError("job id and profile_id are required")
        if character != "eden":
            raise ValueError("MVP only supports the eden character")
        max_results = int(value.get("max_results", 20))
        if max_results <= 0:
            raise ValueError("max_results must be positive")
        return cls(
            schema_version=1,
            id=job_id,
            profile_id=profile_id,
            character=character,
            candidates=CandidateSpec.from_dict(value.get("candidates")),
            filter=EdenFilter.from_dict(value.get("filter")),
            max_results=max_results,
        )

    @classmethod
    def load(cls, path: str | Path) -> SearchJob:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("job root must be an object")
        return cls.from_dict(data)
