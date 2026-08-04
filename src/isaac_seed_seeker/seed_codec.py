"""Binding of Isaac run-seed encoding and checksum validation."""

from __future__ import annotations

ALPHABET = "ABCDEFGHJKLMNPQRSTWXYZ01234V6789"
SEED_XOR = 0x0FEF7FFD
UINT32_MAX = 0xFFFFFFFF
_REVERSE = {character: index for index, character in enumerate(ALPHABET)}


def normalize_seed(value: str) -> str:
    compact = "".join(value.upper().split())
    if len(compact) != 8:
        raise ValueError("seed must contain exactly eight characters")
    invalid = sorted(set(compact) - set(ALPHABET))
    if invalid:
        raise ValueError(f"seed contains invalid characters: {''.join(invalid)}")
    return f"{compact[:4]} {compact[4:]}"


def _checksum(seed: int) -> int:
    value = seed & UINT32_MAX
    checksum = 0
    while value:
        checksum = (checksum + (value & 0xFF)) & 0xFF
        checksum = (2 * checksum + (checksum >> 7)) & 0xFF
        value >>= 5
    return checksum


def seed_to_string(seed: int) -> str:
    if not 0 <= seed <= UINT32_MAX:
        raise ValueError("seed integer must fit in uint32")
    payload = (((seed ^ SEED_XOR) & UINT32_MAX) << 8) | _checksum(seed)
    compact = "".join(ALPHABET[(payload >> (5 * index)) & 31] for index in range(7, -1, -1))
    return f"{compact[:4]} {compact[4:]}"


def string_to_seed(value: str, *, verify_checksum: bool = True) -> int:
    normalized = normalize_seed(value)
    compact = normalized.replace(" ", "")
    payload = 0
    for character in compact:
        payload = (payload << 5) | _REVERSE[character]
    seed = ((payload >> 8) ^ SEED_XOR) & UINT32_MAX
    if verify_checksum and seed_to_string(seed) != normalized:
        raise ValueError("seed checksum is invalid")
    return seed
