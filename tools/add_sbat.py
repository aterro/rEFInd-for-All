#!/usr/bin/env python3

import argparse
import struct
from pathlib import Path


def align(value, boundary):
    return (value + boundary - 1) // boundary * boundary


def add_sbat(image_path, sbat_path):
    image = bytearray(image_path.read_bytes())
    sbat = sbat_path.read_bytes()

    if image[:2] != b"MZ":
        raise ValueError(f"{image_path} is not a PE image")

    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{image_path} has an invalid PE signature")

    coff_offset = pe_offset + 4
    machine, section_count = struct.unpack_from("<HH", image, coff_offset)
    if machine != 0x8664:
        raise ValueError(f"{image_path} is not an X64 EFI image")

    optional_size = struct.unpack_from("<H", image, coff_offset + 16)[0]
    optional_offset = coff_offset + 20
    if struct.unpack_from("<H", image, optional_offset)[0] != 0x20B:
        raise ValueError(f"{image_path} does not have a PE32+ optional header")

    section_alignment, file_alignment = struct.unpack_from(
        "<II", image, optional_offset + 32
    )
    section_offset = optional_offset + optional_size
    sections = []
    for index in range(section_count):
        offset = section_offset + index * 40
        name, virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<8sIIII", image, offset
        )
        if name.rstrip(b"\0") == b".sbat":
            raise ValueError(f"{image_path} already has an .sbat section")
        sections.append((virtual_address, max(virtual_size, raw_size)))

    size_of_headers = struct.unpack_from("<I", image, optional_offset + 60)[0]
    new_section_offset = section_offset + section_count * 40
    if new_section_offset + 40 > size_of_headers:
        raise ValueError(f"{image_path} has no room for an additional PE section")

    raw_alignment = max(file_alignment, 512)
    raw_size = align(len(sbat), raw_alignment)
    raw_offset = align(len(image), raw_alignment)
    image.extend(b"\0" * (raw_offset - len(image)))
    image.extend(sbat)
    image.extend(b"\0" * (raw_size - len(sbat)))

    virtual_address = max(
        10_000_000,
        align(
            max((address + size for address, size in sections), default=0),
            section_alignment,
        ),
    )
    struct.pack_into(
        "<8sIIIIIIHHI",
        image,
        new_section_offset,
        b".sbat\0\0\0",
        len(sbat),
        virtual_address,
        raw_size,
        raw_offset,
        0,
        0,
        0,
        0,
        0x40000040,
    )
    struct.pack_into("<H", image, coff_offset + 2, section_count + 1)
    struct.pack_into(
        "<I",
        image,
        optional_offset + 8,
        struct.unpack_from("<I", image, optional_offset + 8)[0] + raw_size,
    )
    struct.pack_into(
        "<I",
        image,
        optional_offset + 56,
        align(virtual_address + raw_size, section_alignment),
    )
    image_path.write_bytes(image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("sbat", type=Path)
    args = parser.parse_args()
    add_sbat(args.image, args.sbat)


if __name__ == "__main__":
    main()
