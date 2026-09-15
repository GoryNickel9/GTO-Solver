"""Validate preflop blueprint game fixtures against their JSON Schema.

Exit codes: 0 all fixtures valid, 1 at least one invalid, 2 usage error,
77 the jsonschema package is unavailable (CTest treats it as SKIP).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
except ImportError:  # pragma: no cover - environment dependent
    print("PREFLOP_BLUEPRINT_FIXTURE_SCHEMA=SKIP jsonschema not installed")
    sys.exit(77)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("fixtures", nargs="+", type=Path)
    arguments = parser.parse_args()

    schema = json.loads(arguments.schema.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)

    failures = 0
    for fixture in arguments.fixtures:
        document = json.loads(fixture.read_text(encoding="utf-8"))
        errors = sorted(validator.iter_errors(document), key=lambda error: list(error.path))
        if errors:
            failures += 1
            for error in errors:
                location = "/".join(str(part) for part in error.path) or "<root>"
                print(f"{fixture.name}: {location}: {error.message}")
        # Cross-field checks that JSON Schema cannot express.
        if len(document.get("open_target_units", [])) != len(
            document.get("response_target_units", [])
        ):
            failures += 1
            print(f"{fixture.name}: response_target_units must match open_target_units")
        if len(document.get("positions", [])) != document.get("player_count"):
            failures += 1
            print(f"{fixture.name}: positions must have player_count entries")
        if document.get("positions", [""])[-1] != "BTN":
            failures += 1
            print(f"{fixture.name}: the last position must be BTN")

    if failures:
        print(f"PREFLOP_BLUEPRINT_FIXTURE_SCHEMA=FAIL failures={failures}")
        return 1
    print(f"PREFLOP_BLUEPRINT_FIXTURE_SCHEMA=PASS fixtures={len(arguments.fixtures)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
