"""Stable domain objects shared by CLI, filters, and future predictors."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterable, Mapping


TRINKET_ID_MASK = 0x7FFF


def base_trinket_id(value: int) -> int:
    """Strip the golden-trinket flag while preserving the base trinket ID."""
    return int(value) & TRINKET_ID_MASK


def _as_int_set(value: object, field_name: str) -> frozenset[int]:
    if value is None:
        return frozenset()
    if not isinstance(value, list):
        raise ValueError(f"{field_name} must be a list")
    result = frozenset(int(item) for item in value)
    if any(item <= 0 for item in result):
        raise ValueError(f"{field_name} item IDs must be positive")
    return result


@dataclass(frozen=True)
class NumberRange:
    minimum: float | None = None
    maximum: float | None = None

    @classmethod
    def from_dict(cls, value: object, field_name: str) -> NumberRange | None:
        if value is None:
            return None
        if not isinstance(value, Mapping):
            raise ValueError(f"{field_name} must be an object")
        unknown = set(value) - {"min", "max"}
        if unknown:
            raise ValueError(f"{field_name} has unknown keys: {sorted(unknown)}")
        minimum = float(value["min"]) if "min" in value else None
        maximum = float(value["max"]) if "max" in value else None
        if minimum is None and maximum is None:
            raise ValueError(f"{field_name} requires min or max")
        if minimum is not None and maximum is not None and minimum > maximum:
            raise ValueError(f"{field_name}.min cannot exceed max")
        return cls(minimum, maximum)

    def matches(self, value: float) -> bool:
        return (self.minimum is None or value >= self.minimum) and (
            self.maximum is None or value <= self.maximum
        )


@dataclass(frozen=True)
class IdSetFilter:
    all_of: frozenset[int] = frozenset()
    any_of: frozenset[int] = frozenset()
    none_of: frozenset[int] = frozenset()

    @classmethod
    def from_dict(cls, value: object, field_name: str) -> IdSetFilter | None:
        if value is None:
            return None
        if not isinstance(value, Mapping):
            raise ValueError(f"{field_name} must be an object")
        unknown = set(value) - {"all", "any", "none"}
        if unknown:
            raise ValueError(f"{field_name} has unknown keys: {sorted(unknown)}")
        return cls(
            all_of=_as_int_set(value.get("all"), f"{field_name}.all"),
            any_of=_as_int_set(value.get("any"), f"{field_name}.any"),
            none_of=_as_int_set(value.get("none"), f"{field_name}.none"),
        )

    def matches(self, values: Iterable[int]) -> bool:
        present = set(values)
        return (
            self.all_of.issubset(present)
            and (not self.any_of or bool(self.any_of & present))
            and not bool(self.none_of & present)
        )


@dataclass(frozen=True)
class EdenObservation:
    schema_version: int
    profile_id: str
    game_version: str
    seed: str
    seed_u32: int
    stats: Mapping[str, float]
    health: Mapping[str, float]
    pickups: Mapping[str, int]
    active_items: tuple[int, ...] = ()
    passive_items: tuple[int, ...] = ()
    pocket: Mapping[str, int] = field(default_factory=dict)
    continued: bool = False

    @classmethod
    def from_dict(cls, value: Mapping[str, Any]) -> EdenObservation:
        if int(value.get("schema_version", 0)) != 1:
            raise ValueError("unsupported observation schema_version")
        return cls(
            schema_version=1,
            profile_id=str(value.get("profile_id", "")),
            game_version=str(value.get("game_version", "")),
            seed=str(value["seed"]),
            seed_u32=int(value["seed_u32"]),
            stats={key: float(number) for key, number in dict(value.get("stats", {})).items()},
            health={key: float(number) for key, number in dict(value.get("health", {})).items()},
            pickups={key: int(number) for key, number in dict(value.get("pickups", {})).items()},
            active_items=tuple(int(item) for item in value.get("active_items", [])),
            passive_items=tuple(int(item) for item in value.get("passive_items", [])),
            pocket={key: int(number) for key, number in dict(value.get("pocket", {})).items()},
            continued=bool(value.get("continued", False)),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": self.schema_version,
            "profile_id": self.profile_id,
            "game_version": self.game_version,
            "seed": self.seed,
            "seed_u32": self.seed_u32,
            "continued": self.continued,
            "stats": dict(self.stats),
            "health": dict(self.health),
            "pickups": dict(self.pickups),
            "active_items": list(self.active_items),
            "passive_items": list(self.passive_items),
            "pocket": dict(self.pocket),
        }


_RANGE_FIELDS = {
    "damage": ("stats", "damage"),
    "tears": ("stats", "tears"),
    "move_speed": ("stats", "move_speed"),
    "shot_speed": ("stats", "shot_speed"),
    "luck": ("stats", "luck"),
    "red_hearts": ("health", "red_hearts"),
    "soul_hearts": ("health", "soul_hearts"),
    "coins": ("pickups", "coins"),
    "keys": ("pickups", "keys"),
    "bombs": ("pickups", "bombs"),
}


@dataclass(frozen=True)
class EdenFilter:
    ranges: Mapping[str, NumberRange] = field(default_factory=dict)
    active_items: IdSetFilter | None = None
    passive_items: IdSetFilter | None = None
    card: int | None = None
    pill: int | None = None
    trinket: int | None = None

    @classmethod
    def from_dict(cls, value: object) -> EdenFilter:
        if not isinstance(value, Mapping):
            raise ValueError("filter must be an object")
        allowed = set(_RANGE_FIELDS) | {"active_items", "passive_items", "card", "pill", "trinket"}
        unknown = set(value) - allowed
        if unknown:
            raise ValueError(f"filter has unknown keys: {sorted(unknown)}")
        ranges = {
            name: parsed
            for name in _RANGE_FIELDS
            if (parsed := NumberRange.from_dict(value.get(name), name)) is not None
        }
        pocket_values: dict[str, int | None] = {}
        for name in ("card", "pill", "trinket"):
            pocket_values[name] = int(value[name]) if name in value else None
            if pocket_values[name] is not None and pocket_values[name] < 0:
                raise ValueError(f"{name} cannot be negative")
        return cls(
            ranges=ranges,
            active_items=IdSetFilter.from_dict(value.get("active_items"), "active_items"),
            passive_items=IdSetFilter.from_dict(value.get("passive_items"), "passive_items"),
            **pocket_values,
        )

    def matches(self, observation: EdenObservation) -> bool:
        for name, expected in self.ranges.items():
            section_name, value_name = _RANGE_FIELDS[name]
            section = getattr(observation, section_name)
            if value_name not in section or not expected.matches(float(section[value_name])):
                return False
        if self.active_items and not self.active_items.matches(observation.active_items):
            return False
        if self.passive_items and not self.passive_items.matches(observation.passive_items):
            return False
        for name in ("card", "pill", "trinket"):
            expected_id = getattr(self, name)
            actual_id = observation.pocket.get(name, 0)
            if name == "trinket":
                actual_id = base_trinket_id(actual_id)
            if expected_id is not None and actual_id != expected_id:
                return False
        return True
