from pathlib import Path
from struct import pack, pack_into, unpack_from
import sys

from make_esp import LOCAL, make_group, make_record, records, subs, zstr

PERK_ID = LOCAL | 0xFD0


def build(master, source, destination):
    data = master.read_bytes()
    vanilla = next(payload for tag, fid, _, payload in records(data, allowed={b"PERK"})
                   if tag == b"PERK" and fid == 0xF2CAA)
    template = list(subs(vanilla))
    for field in ((b"PRKE", bytes((2, 0, 0))), (b"DATA", bytes((38, 3, 2))),
                  (b"EPFT", b"\x01"), (b"PRKF", b"")):
        assert field in template, field
    old = source.read_bytes()
    prior = list(records(old))
    assert all(tag != b"PERK" and fid != PERK_ID for tag, fid, _, _ in prior)
    perk = make_record(b"PERK", PERK_ID, [
        (b"EDID", zstr("TDSOwnedSpellCost")), (b"FULL", zstr("TDS Spell Cost")),
        (b"DATA", bytes((0, 0, 1, 1, 1))),
        (b"PRKE", bytes((2, 0, 0))), (b"DATA", bytes((38, 3, 2))),
        (b"EPFT", b"\x01"), (b"EPFD", pack("<f", 1)), (b"PRKF", b""),
    ])
    result = bytearray(old)
    assert result[24:28] == b"HEDR"
    version, count, next_id = unpack_from("<fII", old, 30)
    assert abs(version - 1.71) < 0.0001
    pack_into("<II", result, 34, count + 1, max(next_id, 0xFD1))
    result += make_group(b"PERK", [perk])
    after = list(records(result))
    assert after[1:-1] == prior[1:] and after[-1][1] == PERK_ID
    with destination.open("xb") as output:
        output.write(result)
    print("PASS vanilla 000F2CAA schema, new PERK FD0, all existing records identical")


if __name__ == "__main__":
    build(*map(Path, sys.argv[1:]))
