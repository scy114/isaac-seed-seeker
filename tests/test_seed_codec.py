import pytest

from isaac_seed_seeker.seed_codec import normalize_seed, seed_to_string, string_to_seed


def test_known_seed_from_local_j460_log() -> None:
    assert seed_to_string(1473169325) == "MASV SYFS"
    assert string_to_seed("masvsyfs") == 1473169325


@pytest.mark.parametrize("value", [0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF])
def test_round_trip(value: int) -> None:
    assert string_to_seed(seed_to_string(value)) == value


def test_normalization_and_checksum_rejection() -> None:
    assert normalize_seed("MASV\tSYFS") == "MASV SYFS"
    with pytest.raises(ValueError, match="checksum"):
        string_to_seed("MASV SYFA")
    with pytest.raises(ValueError, match="invalid characters"):
        string_to_seed("MASI SYFS")
