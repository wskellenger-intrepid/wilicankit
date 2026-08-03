#!/usr/bin/env python3
"""Convert an old wilicankit binary config (v3 STORAGE_VERSION, *.cfg) to the
new JSON format (*.json) that apps/wilicankit/storage.c now loads/saves.

The struct layouts below mirror apps/wilicankit/can_model.h and the old (pre-
JSON) apps/wilicankit/storage.c exactly, including natural field alignment/
padding — they must stay in sync with can_model.h if that ever changes for
a v3-era file. Only STORAGE_VERSION 3 is supported (the version wilicankit
last wrote); older v1/v2 files are rejected.

Usage: python tools/wilicankit_cfg_to_json.py <input.cfg> <output.json>
"""
import argparse
import ctypes
import json
import sys

STORAGE_MAGIC = 0x57434647
STORAGE_VERSION = 3
CAN_MAX_SIGNALS = 32
CAN_MAX_MESSAGES = 8
CAN_MAX_PLACEMENTS = 16
CAN_MAX_CONTROLS = 16
CAN_SIGNAL_NAME_MAX = 24
CAN_SIGNAL_UNITS_MAX = 12
CAN_MESSAGE_NAME_MAX = 24
STORAGE_JSON_FORMAT_VERSION = 1


class StorageHeader(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("magic", ctypes.c_uint32),
        ("version", ctypes.c_uint16),
        ("reserved", ctypes.c_uint16),
    ]


class CanSignal(ctypes.LittleEndianStructure):
    _fields_ = [
        ("in_use", ctypes.c_bool),
        ("name", ctypes.c_char * CAN_SIGNAL_NAME_MAX),
        ("units", ctypes.c_char * CAN_SIGNAL_UNITS_MAX),
        ("bit_length", ctypes.c_uint8),
        ("scale", ctypes.c_double),
        ("offset", ctypes.c_double),
        ("min_value", ctypes.c_double),
        ("max_value", ctypes.c_double),
        ("value", ctypes.c_double),
    ]


class CanPlacement(ctypes.LittleEndianStructure):
    _fields_ = [
        ("signal_id", ctypes.c_uint8),
        ("start_bit", ctypes.c_uint8),
        ("big_endian", ctypes.c_bool),
    ]


class CanMessage(ctypes.LittleEndianStructure):
    _fields_ = [
        ("in_use", ctypes.c_bool),
        ("name", ctypes.c_char * CAN_MESSAGE_NAME_MAX),
        ("can_id", ctypes.c_uint32),
        ("extended_id", ctypes.c_bool),
        ("dlc", ctypes.c_uint8),
        ("channel", ctypes.c_uint8),
        ("period_us", ctypes.c_uint32),
        ("enabled", ctypes.c_bool),
        ("placement_count", ctypes.c_uint8),
        ("placements", CanPlacement * CAN_MAX_PLACEMENTS),
    ]


class CanControl(ctypes.LittleEndianStructure):
    _fields_ = [
        ("in_use", ctypes.c_bool),
        ("signal_id", ctypes.c_uint8),
        ("control_type", ctypes.c_uint8),
    ]


def _text(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace")


def signal_to_dict(s: CanSignal) -> dict:
    return {
        "in_use": bool(s.in_use),
        "name": _text(s.name),
        "units": _text(s.units),
        "bit_length": s.bit_length,
        "scale": s.scale,
        "offset": s.offset,
        "min_value": s.min_value,
        "max_value": s.max_value,
        "value": s.value,
    }


def placement_to_dict(p: CanPlacement) -> dict:
    return {
        "signal_id": p.signal_id,
        "start_bit": p.start_bit,
        "big_endian": bool(p.big_endian),
    }


def message_to_dict(m: CanMessage) -> dict:
    return {
        "in_use": bool(m.in_use),
        "name": _text(m.name),
        "can_id": m.can_id,
        "extended_id": bool(m.extended_id),
        "dlc": m.dlc,
        "channel": m.channel,
        "period_us": m.period_us,
        "enabled": bool(m.enabled),
        "placement_count": m.placement_count,
        "placements": [placement_to_dict(p) for p in m.placements],
    }


def control_to_dict(c: CanControl) -> dict:
    return {
        "in_use": bool(c.in_use),
        "signal_id": c.signal_id,
        "control_type": c.control_type,
    }


def convert(data: bytes) -> dict:
    header = StorageHeader.from_buffer_copy(data, 0)
    if header.magic != STORAGE_MAGIC:
        raise ValueError(f"bad magic 0x{header.magic:08x} (expected 0x{STORAGE_MAGIC:08x})")
    if header.version != STORAGE_VERSION:
        raise ValueError(
            f"unsupported version {header.version} (only v{STORAGE_VERSION} is supported by this script)"
        )

    offset = ctypes.sizeof(StorageHeader)
    signals = (CanSignal * CAN_MAX_SIGNALS).from_buffer_copy(data, offset)
    offset += ctypes.sizeof(signals)
    messages = (CanMessage * CAN_MAX_MESSAGES).from_buffer_copy(data, offset)
    offset += ctypes.sizeof(messages)
    controls = (CanControl * CAN_MAX_CONTROLS).from_buffer_copy(data, offset)
    offset += ctypes.sizeof(controls)

    if offset != len(data):
        print(
            f"warning: consumed {offset} bytes but file is {len(data)} bytes "
            "(struct layout mismatch? extra trailing bytes ignored)",
            file=sys.stderr,
        )

    return {
        "format_version": STORAGE_JSON_FORMAT_VERSION,
        "signals": [signal_to_dict(s) for s in signals],
        "messages": [message_to_dict(m) for m in messages],
        "controls": [control_to_dict(c) for c in controls],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="old binary config (*.cfg)")
    ap.add_argument("output", help="new JSON config (*.json)")
    args = ap.parse_args()

    with open(args.input, "rb") as f:
        data = f.read()

    doc = convert(data)

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")

    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
