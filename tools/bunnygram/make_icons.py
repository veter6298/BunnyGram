#!/usr/bin/env python3
"""Собирает иконки и логотип bunnyGram без внешних зависимостей.

Рисует md3-квадрат с зайцем, затем раскладывает PNG/ICO/ICNS по ресурсам
Telegram Desktop. Запуск: python3 tools/bunnygram/make_icons.py
"""

from __future__ import annotations

import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
ART = ROOT / "Telegram/Resources/art"
XCODE = ROOT / "Telegram/Telegram/Images.xcassets"
DOCS = ROOT / "docs/readme"

BG_TOP = (44, 44, 46)
BG_BOTTOM = (24, 24, 26)
BUNNY_TOP = (66, 178, 255)
BUNNY_BOTTOM = (10, 104, 225)
EAR_INNER = (11, 95, 191)


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def in_round_rect(x, y, size, radius):
    if x < 0 or y < 0 or x > size or y > size:
        return False
    cx = min(max(x, radius), size - radius)
    cy = min(max(y, radius), size - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius


def capsule(x, y, x1, y1, x2, y2, radius):
    dx, dy = x2 - x1, y2 - y1
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / length2))
    px, py = x1 + dx * t, y1 + dy * t
    return (x - px) ** 2 + (y - py) ** 2 <= radius * radius


def render(size, ss=2, shape="squircle"):
    """RGBA-байты размера size, отрисованные с суперсэмплингом ss."""
    big = size * ss
    scale = 1024.0 / big
    head = (512.0, 622.0, 192.0)
    ear_radius = 50.0
    ears = (
        (470.0, 470.0, 438.0, 212.0),
        (554.0, 470.0, 586.0, 212.0),
    )
    inner_radius = 17.0
    inners = (
        (464.0, 430.0, 444.0, 268.0),
        (560.0, 430.0, 580.0, 268.0),
    )
    eyes = ((456.0, 604.0, 21.0), (568.0, 604.0, 21.0))
    eye_colour = (14, 42, 68)
    radius = 224.0 if shape != "circle" else 512.0
    buffer = bytearray(big * big * 4)
    offset = 0
    for y in range(big):
        uy = (y + 0.5) * scale
        bg = mix(BG_TOP, BG_BOTTOM, uy / 1024.0)
        bunny = mix(BUNNY_TOP, BUNNY_BOTTOM, max(0.0, min(1.0, (uy - 216.0) / 600.0)))
        for x in range(big):
            ux = (x + 0.5) * scale
            if shape == "square":
                inside = True
            elif shape == "circle":
                inside = (ux - 512.0) ** 2 + (uy - 512.0) ** 2 <= 512.0 * 512.0
            else:
                inside = in_round_rect(ux, uy, 1024.0, radius)
            if not inside:
                offset += 4
                continue
            colour = bg
            if (ux - head[0]) ** 2 + (uy - head[1]) ** 2 <= head[2] ** 2:
                colour = bunny
            for x1, y1, x2, y2 in ears:
                if capsule(ux, uy, x1, y1, x2, y2, ear_radius):
                    colour = bunny
                    break
            for x1, y1, x2, y2 in inners:
                if capsule(ux, uy, x1, y1, x2, y2, inner_radius):
                    colour = EAR_INNER
                    break
            for ex, ey, er in eyes:
                if (ux - ex) ** 2 + (uy - ey) ** 2 <= er * er:
                    colour = eye_colour
                    break
            buffer[offset] = colour[0]
            buffer[offset + 1] = colour[1]
            buffer[offset + 2] = colour[2]
            buffer[offset + 3] = 255
            offset += 4
    return downsample(buffer, big, big, size, size)


def downsample(source, width, height, target_width, target_height):
    """Площадное усреднение с предумноженной альфой."""
    out = bytearray(target_width * target_height * 4)
    for ty in range(target_height):
        y0 = ty * height // target_height
        y1 = max(y0 + 1, (ty + 1) * height // target_height)
        for tx in range(target_width):
            x0 = tx * width // target_width
            x1 = max(x0 + 1, (tx + 1) * width // target_width)
            count = (x1 - x0) * (y1 - y0)
            premul = [0, 0, 0]
            alpha = 0
            for y in range(y0, y1):
                row = y * width * 4
                for x in range(x0, x1):
                    i = row + x * 4
                    a = source[i + 3]
                    alpha += a
                    if a:
                        premul[0] += source[i] * a
                        premul[1] += source[i + 1] * a
                        premul[2] += source[i + 2] * a
            o = (ty * target_width + tx) * 4
            avg = alpha // count
            out[o + 3] = avg
            if alpha:
                out[o] = min(255, (premul[0] // alpha))
                out[o + 1] = min(255, (premul[1] // alpha))
                out[o + 2] = min(255, (premul[2] // alpha))
            del premul
    return out


def png(width, height, rgba):
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


def write_png(path, width, height, rgba):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png(width, height, rgba))


def write_ico(path, images):
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, payload in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32,
            len(payload), offset)
        offset += len(payload)
        data += payload
    path.write_bytes(header + entries + data)


def main():
    master = render(1024, ss=2)
    squircle = {1024: master}
    for size in (512, 256, 128, 64, 32, 16):
        squircle[size] = downsample(squircle[size * 2], size * 2, size * 2, size, size)
    for size in (96, 48):
        squircle[size] = render(size, ss=4)
    square = render(256, ss=4, shape="square")
    circle = render(1024, ss=2, shape="circle")

    for size in (16, 32, 48, 64, 128, 256, 512):
        write_png(ART / f"icon{size}.png", size, size, squircle[size])
        write_png(ART / f"icon{size}@2x.png", size * 2, size * 2, squircle[size * 2])

    write_ico(ART / "icon256.ico", [
        (size, png(size, size, squircle[size]))
        for size in (16, 32, 48, 64, 128, 256)
    ])

    write_png(ART / "logo_256.png", 256, 256, squircle[256])
    write_png(ART / "logo_256_no_margin.png", 256, 256, square)
    write_png(ART / "icon_round512@2x.png", 1024, 1024, circle)
    write_png(DOCS / "logo.png", 512, 512, squircle[512])

    appicon = XCODE / "Icon.appiconset"
    for size in (16, 32, 128, 256, 512):
        write_png(appicon / f"icon{size}.png", size, size, squircle[size])
        write_png(appicon / f"icon{size}@2x.png", size * 2, size * 2, squircle[size * 2])

    iconset = XCODE / "Icon.iconset"
    for size in (16, 32, 128, 256, 512):
        write_png(iconset / f"icon_{size}x{size}.png", size, size, squircle[size])
        write_png(iconset / f"icon_{size}x{size}@2x.png", size * 2, size * 2, squircle[size * 2])

    print("icons written")


if __name__ == "__main__":
    main()
