import json

import pytest

from isaac_seed_seeker.external_decoder import (
    DecoderSearchResult,
    ItemTarget,
    render_candidate_job,
)
from isaac_seed_seeker.job import SearchJob


def target_job() -> SearchJob:
    return SearchJob.from_dict(
        {
            "schema_version": 1,
            "id": "target",
            "profile_id": "local-j460",
            "character": "eden",
            "candidates": {"values": ["MASV SYFS"]},
            "filter": {
                "trinket": 169,
                "active_items": {"any": [145, 133]},
                "passive_items": {"any": [81, 134, 187, 212, 665]},
            },
            "max_results": 20,
        }
    )


def test_item_target_matches_or_groups() -> None:
    target = ItemTarget.from_job(target_job())
    assert target.matches_items({"active_id": 145, "passive_id": 665})
    assert target.matches_items({"active_id": 133, "passive_id": 81})
    assert not target.matches_items({"active_id": 105, "passive_id": 81})
    assert not target.matches_items({"active_id": 145, "passive_id": 331})


def test_item_target_rejects_filters_the_adapter_cannot_predict() -> None:
    raw = {
        "schema_version": 1,
        "id": "target",
        "profile_id": "local-j460",
        "candidates": {"values": ["MASV SYFS"]},
        "filter": {
            "damage": {"min": 4},
            "trinket": 169,
            "active_items": {"any": [145]},
            "passive_items": {"any": [81]},
        },
    }
    with pytest.raises(ValueError, match="currently supports"):
        ItemTarget.from_job(SearchJob.from_dict(raw))


def test_render_candidate_job_replaces_range_with_hits(tmp_path) -> None:
    source = tmp_path / "target.json"
    source.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "id": "target",
                "profile_id": "local-j460",
                "candidates": {"start": 1, "end": 100},
                "filter": {
                    "trinket": 169,
                    "active_items": {"any": [145]},
                    "passive_items": {"any": [81]},
                },
            }
        ),
        encoding="utf-8",
    )
    result = DecoderSearchResult(
        matches=(
            {
                "seed": "MASV SYFS",
                "seed_u32": 1473169325,
                "trinket_id": 169,
                "active_id": 145,
                "passive_id": 81,
            },
        ),
        start_u32=1,
        last_u32=100,
        next_start_u32=101,
        scanned=100,
        elapsed_sec=1.0,
        decoder_commit="abc",
    )
    rendered = json.loads(render_candidate_job(source, result))
    assert rendered["candidates"] == {"values": ["MASV SYFS"]}
    assert rendered["id"] == "target-candidates-00000001-00000064"


def test_render_candidate_job_rejects_empty_batch(tmp_path) -> None:
    empty = DecoderSearchResult((), 1, 10, 11, 10, 0.1, None)
    with pytest.raises(ValueError, match="without matches"):
        render_candidate_job(tmp_path / "missing.json", empty)
