#!/usr/bin/env python3
"""Build-time HighNoon DBC bundle validator.

The app is intentionally built from the Embedded-Sharepoint submodule rather
than an independently copied DBC.  DBC tools are permissive about duplicate
frame IDs, so reject them here before Photon embeds the bundle.
"""

from __future__ import annotations

import pathlib
import re
import sys


FRAME = re.compile(r"^BO_\s+(\d+)\s+([^:]+):")


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("usage: merge_highnoon_dbc.py OUTPUT INPUT...", file=sys.stderr)
        return 2

    output = pathlib.Path(argv[1])
    sources = [pathlib.Path(value) for value in argv[2:]]
    seen: dict[int, tuple[pathlib.Path, str]] = {}
    chunks: list[str] = []

    for source in sources:
        try:
            text = source.read_text(encoding="utf-8")
        except OSError as error:
            print(f"cannot read {source}: {error}", file=sys.stderr)
            return 1
        for line in text.splitlines():
            match = FRAME.match(line)
            if not match:
                continue
            frame_id = int(match.group(1))
            message = match.group(2)
            if frame_id > 0x1FFF:
                print(
                    f"{source}: {message} uses frame ID {frame_id}, outside Photon's classic CAN bundle",
                    file=sys.stderr,
                )
                return 1
            if frame_id in seen:
                previous_file, previous_message = seen[frame_id]
                print(
                    f"duplicate CAN frame 0x{frame_id:X}: {previous_file}:{previous_message} "
                    f"and {source}:{message}",
                    file=sys.stderr,
                )
                return 1
            seen[frame_id] = (source, message)
        chunks.append(f'CM_ "Photon source: {source.name}";\n{text.rstrip()}\n')

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(chunks), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
