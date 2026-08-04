"""Import observer records and query them deterministically."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Iterable, Iterator

from .domain import EdenObservation
from .job import SearchJob

LOG_PREFIX = "ISAAC_SEED_SEEKER observation "


def iter_log_observations(lines: Iterable[str]) -> Iterator[EdenObservation]:
    for line in lines:
        marker = line.find(LOG_PREFIX)
        if marker < 0:
            continue
        payload = line[marker + len(LOG_PREFIX) :].strip()
        try:
            raw = json.loads(payload)
            if isinstance(raw, dict):
                yield EdenObservation.from_dict(raw)
        except (json.JSONDecodeError, KeyError, TypeError, ValueError):
            continue


def import_game_log(path: str | Path) -> list[EdenObservation]:
    log_path = Path(path)
    if not log_path.is_file():
        raise ValueError(f"game log does not exist: {log_path}")
    latest: dict[tuple[str, int], EdenObservation] = {}
    with log_path.open("r", encoding="utf-8", errors="replace") as stream:
        for observation in iter_log_observations(stream):
            latest[(observation.profile_id, observation.seed_u32)] = observation
    return list(latest.values())


def iter_jsonl(path: str | Path) -> Iterator[EdenObservation]:
    with Path(path).open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                raw = json.loads(line)
                if not isinstance(raw, dict):
                    raise ValueError("record is not an object")
                yield EdenObservation.from_dict(raw)
            except (json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
                raise ValueError(f"invalid observation at line {line_number}: {error}") from error


def query_observations(observations: Iterable[EdenObservation], job: SearchJob) -> list[EdenObservation]:
    matches: list[EdenObservation] = []
    for observation in observations:
        if observation.profile_id != job.profile_id:
            continue
        if job.filter.matches(observation):
            matches.append(observation)
            if len(matches) >= job.max_results:
                break
    return matches


def write_jsonl(path: str | Path, observations: Iterable[EdenObservation]) -> int:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        for observation in observations:
            stream.write(json.dumps(observation.to_dict(), ensure_ascii=False, separators=(",", ":")))
            stream.write("\n")
            count += 1
    return count
