import pytest

from isaac_seed_seeker.job import CandidateSpec, SearchJob
from isaac_seed_seeker.seed_codec import string_to_seed


def test_range_candidate_limit_is_deterministic() -> None:
    spec = CandidateSpec.from_dict({"start": 1, "end": 100, "step": 2, "max_candidates": 3})
    values = list(spec.iter_seed_strings())
    assert [string_to_seed(value) for value in values] == [1, 3, 5]


def test_explicit_candidates_are_checksum_validated() -> None:
    with pytest.raises(ValueError, match="checksum"):
        CandidateSpec.from_dict({"values": ["MASV SYFA"]})


def test_zero_seed_is_rejected_for_game_execution() -> None:
    from isaac_seed_seeker.seed_codec import seed_to_string

    with pytest.raises(ValueError, match="zero"):
        CandidateSpec.from_dict({"values": [seed_to_string(0)]})


def test_mvp_rejects_other_characters() -> None:
    with pytest.raises(ValueError, match="only supports"):
        SearchJob.from_dict(
            {
                "schema_version": 1,
                "id": "wrong-character",
                "profile_id": "local-j460",
                "character": "isaac",
                "candidates": {"values": ["MASV SYFS"]},
                "filter": {},
            }
        )
