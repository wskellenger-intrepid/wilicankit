#!/usr/bin/env python3
"""Convert a wilicankit v1 JSON config (fixed-position, every CAN_MAX_*
slot written) to the v2 schema (sparse, id-keyed) that
apps/wilicankit/storage.c now loads/saves.

v1 wrote every signal/message/control slot (up to the old CAN_MAX_SIGNALS=
32 / CAN_MAX_MESSAGES=8 / CAN_MAX_PLACEMENTS=16 / CAN_MAX_CONTROLS=16),
including unused ones, with an explicit "in_use" flag and a
"placement_count" field. v2 only writes in_use entries, each carrying an
explicit "id" (its old array index — unchanged, and still valid under the
new, larger capacities), drops "in_use" (implied by presence) and
"placement_count" (derived from the placements array length on load).

Usage: python tools/wilicankit_json_migrate_v1_to_v2.py <input_v1.json> <output_v2.json>
"""
import argparse
import json
import sys

V1_FORMAT_VERSION = 1
V2_FORMAT_VERSION = 2


def migrate_signals(v1_signals: list) -> list:
    out = []
    for i, s in enumerate(v1_signals):
        if not s.get("in_use"):
            continue
        out.append({
            "id": i,
            "name": s["name"],
            "units": s["units"],
            "bit_length": s["bit_length"],
            "scale": s["scale"],
            "offset": s["offset"],
            "min_value": s["min_value"],
            "max_value": s["max_value"],
            "value": s["value"],
        })
    return out


def migrate_placement(p: dict) -> dict:
    return {
        "signal_id": p["signal_id"],
        "start_bit": p["start_bit"],
        "big_endian": p["big_endian"],
    }


def migrate_messages(v1_messages: list) -> list:
    out = []
    for i, m in enumerate(v1_messages):
        if not m.get("in_use"):
            continue
        count = m["placement_count"]
        placements = [migrate_placement(p) for p in m["placements"][:count]]
        out.append({
            "id": i,
            "name": m["name"],
            "can_id": m["can_id"],
            "extended_id": m["extended_id"],
            "dlc": m["dlc"],
            "channel": m["channel"],
            "period_us": m["period_us"],
            "enabled": m["enabled"],
            "placements": placements,
        })
    return out


def migrate_controls(v1_controls: list) -> list:
    out = []
    for i, c in enumerate(v1_controls):
        if not c.get("in_use"):
            continue
        out.append({
            "id": i,
            "signal_id": c["signal_id"],
            "control_type": c["control_type"],
        })
    return out


def migrate(doc: dict) -> dict:
    version = doc.get("format_version")
    if version != V1_FORMAT_VERSION:
        raise ValueError(
            f"unsupported format_version {version!r} (only v{V1_FORMAT_VERSION} is accepted by this script)"
        )
    return {
        "format_version": V2_FORMAT_VERSION,
        "signals": migrate_signals(doc["signals"]),
        "messages": migrate_messages(doc["messages"]),
        "controls": migrate_controls(doc["controls"]),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="old v1 JSON config")
    ap.add_argument("output", help="new v2 JSON config")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        doc = json.load(f)

    out_doc = migrate(doc)

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(out_doc, f, indent=2)
        f.write("\n")

    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
