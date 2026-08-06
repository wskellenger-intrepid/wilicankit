"""Convert a PSRAM-linked wilicankit ELF into a UF2 for the display app loader.

picotool refuses this image ("entry point is not in mapped part of file"): it
only maps the flash and SRAM windows, not PSRAM at 0x11000000. This walks the
ELF program headers directly instead, which also avoids objcopy -Obinary
padding across the NOLOAD .psram_noload region.

Usage: elf2uf2_psram.py <in.elf> <out.uf2>
"""
import struct
import sys

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
FLAG_FAMILY_ID = 0x00002000
FAMILY_RP2350_ARM_S = 0xE48BFF59

# Must match fwImageStream.h: the loader classifies on the first block's
# address and drops anything outside its single decode window.
PSRAM_BASE = 0x11000000
PSRAM_SIZE = 0x800000

PT_LOAD = 1
PAGE = 256


def read_segments(elf):
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        sys.exit("not a little-endian ELF32")
    e_entry, e_phoff = struct.unpack_from("<II", elf, 24)
    e_phentsize, e_phnum = struct.unpack_from("<HH", elf, 42)
    segs = []
    for i in range(e_phnum):
        p_type, p_offset, _p_vaddr, p_paddr, p_filesz, _p_memsz = struct.unpack_from(
            "<IIIIII", elf, e_phoff + i * e_phentsize)
        if p_type == PT_LOAD and p_filesz:
            segs.append((p_paddr, elf[p_offset:p_offset + p_filesz]))
    return e_entry, segs


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf = open(sys.argv[1], "rb").read()
    entry, segs = read_segments(elf)

    if not PSRAM_BASE <= entry < PSRAM_BASE + PSRAM_SIZE:
        sys.exit(f"entry 0x{entry:08X} is outside the PSRAM window - "
                 "the loader would route this image to flash")

    # Scatter into 256-byte pages keyed by address so disjoint segments and
    # segments that do not start page-aligned both come out right.
    pages = {}
    for addr, data in segs:
        end = addr + len(data)
        if addr < PSRAM_BASE or end > PSRAM_BASE + PSRAM_SIZE:
            sys.exit(f"segment 0x{addr:08X}..0x{end:08X} escapes the PSRAM window")
        for off in range(len(data)):
            a = addr + off
            pages.setdefault(a & ~(PAGE - 1), bytearray(PAGE))[a % PAGE] = data[off]

    addrs = sorted(pages)
    blocks = []
    for i, a in enumerate(addrs):
        payload = bytes(pages[a]) + bytes(476 - PAGE)
        blocks.append(struct.pack("<IIIIIIII", UF2_MAGIC0, UF2_MAGIC1,
                                  FLAG_FAMILY_ID, a, PAGE, i, len(addrs),
                                  FAMILY_RP2350_ARM_S)
                      + payload + struct.pack("<I", UF2_MAGIC_END))
    open(sys.argv[2], "wb").write(b"".join(blocks))

    span = addrs[-1] + PAGE - addrs[0]
    print(f"{sys.argv[2]}: {len(blocks)} blocks, entry 0x{entry:08X}, "
          f"0x{addrs[0]:08X}..0x{addrs[-1] + PAGE:08X} "
          f"({len(blocks) * PAGE} bytes in a {span} byte span)")


main()
