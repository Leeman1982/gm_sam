#!/usr/bin/env python3
# ============================================================================
#  pack_sf2.py  -  Build the SF2 bank image for the external flash module.
#
#  Produces a flat binary whose layout matches Sf2Flash.cpp:
#
#     offset 0 : DirHdr  { uint32 magic='SF2D' (0x44324653 LE); uint32 count }
#                Entry[count] { uint32 offset; uint32 size; char name[24] }
#     ...      : each bank blob, 4 KB-aligned, at its recorded offset
#
#  Bank index order on the command line == index used by SF2_DEFAULT_BANK and
#  the UI. Keep OPL2 first (index 0) to match the firmware default.
#
#  USAGE
#     python3 pack_sf2.py -o sf2_image.bin \
#         OPL2:OPL2_FM_v2_FAT_Adlib.sf2  Merlin:Merlin_GM_V1.2_Bank.sf2
#
#  Then flash sf2_image.bin to your external SPI flash chip at address 0 with
#  your programmer (e.g. flashrom, an esptool-style writer, or a dedicated SPI
#  flasher). See README "Flashing the SF2 bank".
#
#  NOTE: tsf expands samples to float in RAM on the RP2350, so slim each .sf2 in
#  Polyphone first (see README). This tool does not slim - it only packs.
# ============================================================================
import argparse, struct, sys, os

DIR_MAGIC = 0x44324653          # 'S','F','2','D' little-endian
ALIGN     = 4096                # 4 KB sector alignment for the blobs
NAME_LEN  = 24

def main():
    ap = argparse.ArgumentParser(description="Pack SF2 banks into a flash image")
    ap.add_argument("-o", "--out", required=True, help="output image file")
    ap.add_argument("banks", nargs="+",
                    help="bank entries as NAME:path/to/bank.sf2 (order = index)")
    args = ap.parse_args()

    entries = []
    for spec in args.banks:
        if ":" not in spec:
            sys.exit(f"bad bank spec '{spec}' (expected NAME:path.sf2)")
        name, path = spec.split(":", 1)
        if not os.path.isfile(path):
            sys.exit(f"no such file: {path}")
        with open(path, "rb") as f:
            data = f.read()
        if len(name.encode()) >= NAME_LEN:
            sys.exit(f"name too long (<{NAME_LEN} bytes): {name}")
        entries.append((name, data))

    count = len(entries)
    header_size = 8 + count * (4 + 4 + NAME_LEN)

    def align(n): return (n + ALIGN - 1) & ~(ALIGN - 1)

    # Lay out blob offsets after the (aligned) directory.
    cursor = align(header_size)
    placed = []
    for name, data in entries:
        placed.append((name, cursor, data))
        cursor = align(cursor + len(data))
    image_size = cursor

    buf = bytearray(b"\xFF" * image_size)
    struct.pack_into("<II", buf, 0, DIR_MAGIC, count)
    pos = 8
    for name, offset, data in placed:
        nb = name.encode().ljust(NAME_LEN, b"\x00")
        struct.pack_into("<II", buf, pos, offset, len(data))
        buf[pos + 8: pos + 8 + NAME_LEN] = nb
        pos += 8 + NAME_LEN
    for _, offset, data in placed:
        buf[offset: offset + len(data)] = data

    with open(args.out, "wb") as f:
        f.write(buf)

    print(f"wrote {args.out}  ({image_size/1024/1024:.2f} MB, {count} bank(s))")
    for i, (name, offset, data) in enumerate(placed):
        print(f"  [{i}] {name:<10} offset=0x{offset:08X} size={len(data)} bytes")

if __name__ == "__main__":
    main()
