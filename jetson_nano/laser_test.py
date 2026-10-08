"""Manual Jetson-to-STM32 laser and pan/tilt test."""

from __future__ import annotations

import argparse

from communication.stm32_link import Stm32Link
from record.last_seen import LastSeenStore
from vision.aim_mapper import AimMapper


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="STM32 port, for example /dev/ttyACM0")
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("ping")
    subcommands.add_parser("home")

    aim = subcommands.add_parser("aim")
    aim.add_argument("pan", type=int)
    aim.add_argument("tilt", type=int)

    point = subcommands.add_parser("point")
    point.add_argument("pan", type=int)
    point.add_argument("tilt", type=int)
    point.add_argument("--seconds", type=float, default=1.0)

    pixel = subcommands.add_parser("pixel")
    pixel.add_argument("x", type=int)
    pixel.add_argument("y", type=int)
    pixel.add_argument("--width", type=int, default=1280)
    pixel.add_argument("--height", type=int, default=720)
    pixel.add_argument("--seconds", type=float, default=1.0)

    item = subcommands.add_parser("item")
    item.add_argument("item_id")
    item.add_argument("--width", type=int, default=1280)
    item.add_argument("--height", type=int, default=720)
    item.add_argument("--seconds", type=float, default=1.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    pan = tilt = None
    storage = None
    try:
        if args.command == "pixel":
            mapper = AimMapper.from_env(args.width, args.height)
            pan, tilt = mapper.map(args.x, args.y)
        elif args.command == "item":
            storage = LastSeenStore.from_env()
            x, y = storage.get_coordinates(args.item_id)
            mapper = AimMapper.from_env(args.width, args.height)
            pan, tilt = mapper.map(x, y)

        with Stm32Link(port=args.port) as link:
            if args.command == "ping":
                link.ping()
            elif args.command == "home":
                link.home()
            elif args.command == "aim":
                link.aim(args.pan, args.tilt)
            elif args.command == "point":
                link.point(args.pan, args.tilt, on_seconds=args.seconds)
            elif args.command in {"pixel", "item"}:
                assert pan is not None and tilt is not None
                link.point(pan, tilt, on_seconds=args.seconds)
                print(f"AIM:{pan},{tilt}")
            print("OK")
    finally:
        if storage is not None:
            storage.close()


if __name__ == "__main__":
    main()
