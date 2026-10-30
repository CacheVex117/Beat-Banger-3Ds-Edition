"""Inspect / rebuild an NCCH logo (LZ11-compressed darc).

    logo_tool.py extract <cia> <out.lz>          logo region of the first NCCH in a CIA
    logo_tool.py list <logo.lz>                  darc entries
    logo_tool.py blank <logo.lz> <out.lz>        every BCLIM image fully transparent, everything else byte-identical
"""
import struct
import sys


def lz11_decompress(src: bytes) -> bytes:
    assert src[0] == 0x11, f"not LZ11 (0x{src[0]:02x})"
    size = src[1] | (src[2] << 8) | (src[3] << 16)
    pos = 4
    if size == 0:
        size = struct.unpack_from("<I", src, 4)[0]
        pos = 8
    out = bytearray()
    while len(out) < size:
        flags = src[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b0 = src[pos]
                ind = b0 >> 4
                if ind == 0:
                    b1, b2 = src[pos + 1], src[pos + 2]
                    length = (((b0 & 0xF) << 4) | (b1 >> 4)) + 0x11
                    disp = (((b1 & 0xF) << 8) | b2) + 1
                    pos += 3
                elif ind == 1:
                    b1, b2, b3 = src[pos + 1], src[pos + 2], src[pos + 3]
                    length = (((b0 & 0xF) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111
                    disp = (((b2 & 0xF) << 8) | b3) + 1
                    pos += 4
                else:
                    b1 = src[pos + 1]
                    length = ind + 1
                    disp = (((b0 & 0xF) << 8) | b1) + 1
                    pos += 2
                for _ in range(length):
                    out.append(out[-disp])
            else:
                out.append(src[pos])
                pos += 1
    return bytes(out)


def lz11_store(data: bytes) -> bytes:
    """LZ11 with only literal blocks (valid for any LZ11 decoder)."""
    n = len(data)
    out = bytearray([0x11, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
    for i in range(0, n, 8):
        out.append(0)
        out += data[i:i + 8]
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def lz11_compress(data: bytes) -> bytes:
    """Greedy LZ11 (window 4096, matches up to 0x10110 bytes)."""
    n = len(data)
    out = bytearray([0x11, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
    pos = 0
    while pos < n:
        flag_at = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if pos >= n:
                break
            best_len, best_disp = 0, 0
            for disp in range(1, min(pos, 4096) + 1):
                ln = 0
                while ln < 0x10110 and pos + ln < n and data[pos + ln] == data[pos + ln - disp]:
                    ln += 1
                if ln > best_len:
                    best_len, best_disp = ln, disp
                    if ln >= 0x10110:
                        break
            if best_len >= 3:
                flags |= 0x80 >> bit
                d = best_disp - 1
                if best_len <= 0x10:
                    out += bytes([((best_len - 1) << 4) | (d >> 8), d & 0xFF])
                elif best_len <= 0x110:
                    l = best_len - 0x11
                    out += bytes([l >> 4, ((l & 0xF) << 4) | (d >> 8), d & 0xFF])
                else:
                    l = best_len - 0x111
                    out += bytes([0x10 | (l >> 12), (l >> 4) & 0xFF, ((l & 0xF) << 4) | (d >> 8), d & 0xFF])
                pos += best_len
            else:
                out.append(data[pos])
                pos += 1
        out[flag_at] = flags
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def darc_entries(d: bytes):
    assert d[:4] == b"darc", d[:4]
    bom, hlen, ver, flen, tbl_off, tbl_len, data_off = struct.unpack_from("<HHIIIII", d, 4)
    root_end = struct.unpack_from("<I", d, tbl_off + 8)[0]
    names_off = tbl_off + root_end * 12
    out = []
    path = []
    ends = []
    for i in range(root_end):
        name_off, a, b = struct.unpack_from("<III", d, tbl_off + i * 12)
        is_dir = bool(name_off & 0x01000000)
        no = names_off + (name_off & 0xFFFFFF)
        end = no
        while d[end:end + 2] != b"\0\0":
            end += 2
        name = d[no:end].decode("utf-16-le")
        while ends and i >= ends[-1]:
            ends.pop()
            path.pop()
        if is_dir:
            path.append(name)
            ends.append(b)
        else:
            out.append(("/".join(p for p in path if p) + "/" + name, a, b))
    return out


def main():
    cmd = sys.argv[1]
    if cmd == "extract":
        head = open(sys.argv[2], "rb").read(64 << 20)
        base = head.find(b"NCCH") - 0x100
        off, size = struct.unpack_from("<II", head, base + 0x198)
        blob = head[base + off * 0x200: base + (off + size) * 0x200]
        open(sys.argv[3], "wb").write(blob)
        print(f"logo region {size * 0x200} bytes")
    elif cmd == "list":
        d = lz11_decompress(open(sys.argv[2], "rb").read())
        print(f"darc {len(d)} bytes")
        for name, off, size in darc_entries(d):
            magic = d[off + size - 0x28: off + size - 0x24] if name.endswith(".bclim") else d[off:off + 4]
            print(f"  {name:40s} off {off:6d} size {size:6d} {magic}")
    elif cmd == "blank":
        d = bytearray(lz11_decompress(open(sys.argv[2], "rb").read()))
        n = 0
        for name, off, size in darc_entries(bytes(d)):
            if not name.endswith(".bclim"):
                continue
            # BCLIM: pixel data, then a 0x28-byte footer (CLIM header + imag block); the image data size is in imag
            footer = off + size - 0x28
            assert d[footer:footer + 4] == b"CLIM", name
            img_size = struct.unpack_from("<I", d, footer + 0x14 + 0x10)[0]
            fmt = d[footer + 0x14 + 0x0C]
            d[off:off + img_size] = bytes(img_size)   # all zero: alpha 0 in every format with alpha (ETC1A4, RGBA, LA, A)
            print(f"  blanked {name}: {img_size} bytes, format {fmt}")
            n += 1
        comp = lz11_compress(bytes(d))
        assert lz11_decompress(comp) == bytes(d), "LZ11 round trip failed"
        open(sys.argv[3], "wb").write(comp)
        print(f"{n} image(s) blanked; logo {len(comp)} bytes compressed (round trip OK)")


if __name__ == "__main__":
    main()
