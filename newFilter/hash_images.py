#!/usr/bin/env python3

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def dct_matrix(n: int) -> np.ndarray:
    x = np.arange(n)
    k = x.reshape(-1, 1)
    mat = np.cos(np.pi * (2 * x + 1) * k / (2 * n))
    mat[0, :] /= np.sqrt(2)
    return mat * np.sqrt(2 / n)


def dct2(block: np.ndarray, mat: np.ndarray) -> np.ndarray:
    return mat @ block @ mat.T


def bits_to_uint64(bits: np.ndarray) -> int:
    flat = bits.flatten()
    value = 0
    for bit in flat:
        value = (value << 1) | int(bool(bit))
    return value


def ahash(image: Image.Image) -> int:
    img = image.convert("L").resize((8, 8), Image.LANCZOS)
    pixels = np.asarray(img, dtype=np.float32)
    avg = pixels.mean()
    bits = pixels > avg
    return bits_to_uint64(bits)


def dhash(image: Image.Image) -> int:
    img = image.convert("L").resize((9, 8), Image.LANCZOS)
    pixels = np.asarray(img, dtype=np.float32)
    diff = pixels[:, 1:] > pixels[:, :-1]
    return bits_to_uint64(diff)


def phash(image: Image.Image) -> int:
    hash_size = 8
    highfreq = 4
    size = hash_size * highfreq
    img = image.convert("L").resize((size, size), Image.LANCZOS)
    pixels = np.asarray(img, dtype=np.float32)
    mat = dct_matrix(size)
    coeffs = dct2(pixels, mat)
    dct_low = coeffs[:hash_size, :hash_size]
    median = np.median(dct_low[1:, 1:])
    bits = dct_low > median
    return bits_to_uint64(bits)


def iter_images(root: Path):
    exts = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"}
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() in exts:
            yield path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Input image directory")
    parser.add_argument("--output", required=True, help="Output hash file")
    parser.add_argument("--method", choices=["ahash", "dhash", "phash"], required=True)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--with-path", action="store_true")
    args = parser.parse_args()

    root = Path(args.input)
    output = Path(args.output)

    if not root.exists():
        raise SystemExit(f"Input not found: {root}")

    if args.method == "ahash":
        func = ahash
    elif args.method == "dhash":
        func = dhash
    else:
        func = phash

    count = 0
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as f:
        for path in iter_images(root):
            try:
                with Image.open(path) as img:
                    value = func(img)
            except Exception:
                continue
            if args.with_path:
                f.write(f"0x{value:016x}\t{path}\n")
            else:
                f.write(f"0x{value:016x}\n")
            count += 1
            if args.limit and count >= args.limit:
                break
    print(f"Hashed {count} images -> {output}")


if __name__ == "__main__":
    main()
