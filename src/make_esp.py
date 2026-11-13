"""Build TDS QA ESP from verified Skyrim.esm templates and native stage output."""
from collections import defaultdict
from pathlib import Path
from struct import pack, pack_into, unpack_from
import csv
import sys
import zlib

LOCAL = 0x01000000  # on-disk self index after Skyrim.esm
ESL_FLAG = 0x00000200
VANILLA_IDS = {0x1397E, 0x12EB7, 0x3B97C, 0x9171B, 0x10FB95, 0x10FB77,
               0x4605A, 0x493AA, 0x10F814, 0x954D7}
GROUPS = ("WeaponGrowth", "WeaponDecay", "AccessoryGrowth", "AccessoryDecay")
DISPLAY_NAMES = ("디벨라의 갈망", "디벨라의 순정", "디벨라의 갈망", "디벨라의 순정")
ITEMS = (0x12EB7, 0x12EB7, 0x3B97C, 0x9171B)
ITEM_NAMES = ("철검", "철검", "Silver Ring", "Silver Necklace")
# ActorValue values from CommonLibSSE ActorValues.h. Resource/AV lists mirror StageData.cpp.
VALUES = ((24, 26), (24, 26), (39, 25, 26, 45, 40, 44, 41, 43, 42, 83),
          (39, 25, 26, 45, 40, 44, 41, 43, 42, 83))


def zstr(s):
    return s.encode("utf-8") + b"\0"


def records(data, lo=0, hi=None, allowed=None):
    hi = len(data) if hi is None else hi
    while lo < hi:
        tag, size, flags, fid, _, version, _ = unpack_from("<4sIIIIHH", data, lo)
        if tag == b"GRUP":
            if allowed is None or data[lo + 8:lo + 12] in allowed:
                yield from records(data, lo + 24, lo + size, allowed)
            lo += size
            continue
        payload = data[lo + 24:lo + 24 + size]
        lo += 24 + size
        if flags & 0x40000:
            raw = zlib.decompress(payload[4:])
            assert len(raw) == unpack_from("<I", payload)[0]
            payload = raw
        yield tag, fid, version, payload
    assert lo == hi


def subs(payload):
    pos = 0
    while pos < len(payload):
        tag, size = unpack_from("<4sH", payload, pos)
        pos += 6
        if tag == b"XXXX":
            size, tag, _ = unpack_from("<I4sH", payload, pos)
            pos += 10
        yield tag, payload[pos:pos + size]
        pos += size
    assert pos == len(payload)


def make_record(tag, fid, fields, flags=0):
    payload = b"".join(pack("<4sH", sig, len(data)) + data for sig, data in fields)
    return pack("<4sIIIIHH", tag, len(payload), flags, fid, 0, 44, 0) + payload


def make_group(tag, records):
    payload = b"".join(records)
    return pack("<4sI4sIII", b"GRUP", len(payload) + 24, tag, 0, 0, 0) + payload


def write(vanilla_path, csv_path, output_path):
    vanilla = {fid: (tag, list(subs(payload))) for tag, fid, _, payload in records(
        vanilla_path.read_bytes(), allowed={b"ENCH", b"MGEF", b"SPEL", b"ARMO", b"WEAP"})
               if fid in VANILLA_IDS}
    assert set(vanilla) == VANILLA_IDS
    assert dict(vanilla[0x10F814][1])[b"EDID"] == zstr("DA11AbFortifyHealth")
    groups = defaultdict(list)

    def add(tag, local, fields):
        groups[tag].append(make_record(tag, LOCAL | local, fields))

    def effect(local, magnitude):
        return [(b"EFID", pack("<I", LOCAL | local)), (b"EFIT", pack("<fII", magnitude, 0, 0))]

    add(b"FLST", 0x830, [(b"EDID", zstr("TDSAccessoryEnchantSlots")),
        (b"LNAM", pack("<I", 0x10CD09)), (b"LNAM", pack("<I", 0x10CD0A))])
    stage_rows = list(csv.reader(csv_path.read_text(encoding="ascii").splitlines()))
    assert len(stage_rows) == 40
    for g, name in enumerate(GROUPS):
        is_weapon = g < 2
        source = 0x4605A if is_weapon else 0x493AA
        # Use vanilla disk layout only. Marker has no shader, sound, art or gameplay magnitude.
        template = dict(vanilla[source][1])[b"DATA"]
        assert len(template) == 152
        marker = bytearray(152)
        pack_into("<IfIii", marker, 0, 0x08200A12, 0.0, 0, -1, -1)
        marker[64:80] = template[64:80]
        pack_into("<II", marker, 64, 1, 24)  # inert Script archetype, no attached script
        pack_into("<i", marker, 88, -1)
        pack_into("<II", marker, 80, 1 if is_weapon else 0, 1 if is_weapon else 0)
        pack_into("<f", marker, 104, 1.0)
        add(b"MGEF", 0x820 + g, [(b"EDID", zstr("TDSMarker" + name)), (b"FULL", zstr(DISPLAY_NAMES[g])),
            (b"DATA", marker), (b"DNAM", zstr(DISPLAY_NAMES[g]))])

        ench_source = 0x10FB95 if is_weapon else 0x10FB77
        ench = bytearray(dict(vanilla[ench_source][1])[b"ENIT"])
        assert len(ench) == 36
        # Vanilla learnable base template, with explicit zero charge/cost and no inherited restrictions.
        pack_into("<IIII", ench, 0, 0, 1, int(is_weapon), 0)
        pack_into("<II", ench, 28, 0, 0 if is_weapon else LOCAL | 0x830)
        add(b"ENCH", 0x810 + g, [(b"EDID", zstr("TDSEnch" + name)), (b"OBND", bytes(12)),
            (b"FULL", zstr(DISPLAY_NAMES[g])), (b"ENIT", ench)] + effect(0x820 + g, 0.0))

        tag, source_item = vanilla[ITEMS[g]]
        item = []
        for key, value in source_item:
            if key == b"EDID":
                value = zstr("TDSSample" + name)
            elif key == b"FULL":
                value = zstr(DISPLAY_NAMES[g] + " · " + ITEM_NAMES[g])
            elif key == b"DESC":
                value = zstr("")
            item.append((key, value))
            if key == b"FULL":
                item.append((b"EITM", pack("<I", LOCAL | (0x810 + g))))
                if is_weapon:
                    item.append((b"EAMT", pack("<H", 0)))
        add(tag, 0x800 + g, item)

        for i, av in enumerate(VALUES[g]):
            data = bytearray(152)
            pack_into("<IfIii", data, 0, 0x08208802, 0.0, 0, -1, -1)
            # Constant, self-delivered engine-owned ability using the vanilla DATA layout.
            pack_into("<i", data, 68, av)
            pack_into("<i", data, 88, -1)
            pack_into("<f", data, 104, 1.0)
            # Value Modifier stacks independently across the four owned spells.
            pack_into("<I", data, 64, 0)
            add(b"MGEF", 0x840 + g * 16 + i,
                [(b"EDID", zstr(f"TDSOwned{name}{av}")), (b"FULL", zstr(DISPLAY_NAMES[g])),
                 (b"DATA", data), (b"DNAM", zstr(""))])

        spell_template = dict(vanilla[0x954D7][1])[b"SPIT"]
        assert len(spell_template) == 36
        for index, row in enumerate(stage_rows, 1):
            stage, *amounts = map(int, row)
            assert stage == index and len(amounts) == 12
            amounts = amounts[:2] if is_weapon else amounts[2:]
            effects = [field for i, amount in enumerate(amounts) if amount
                       for field in effect(0x840 + g * 16 + i, amount)]
            if not effects:
                effects = effect(0x840 + g * 16, 0)
            add(b"SPEL", 0x900 + g * 0x40 + stage,
                [(b"EDID", zstr(f"TDSAbility{name}{stage:02}")), (b"OBND", bytes(12)),
                 (b"FULL", zstr(DISPLAY_NAMES[g])), (b"SPIT", spell_template), (b"DESC", zstr(""))] + effects)

    count = sum(map(len, groups.values()))
    header = make_record(b"TES4", 0, [(b"HEDR", pack("<fII", 1.7, count + len(groups), 0xA00)),
        (b"CNAM", zstr("TDS")), (b"SNAM", zstr("TULL Dibella says 0.1.2")),
        (b"MAST", zstr("Skyrim.esm")), (b"DATA", bytes(8))], flags=ESL_FLAG)
    output_path.write_bytes(header + b"".join(make_group(k, groups[k]) for k in
        (b"MGEF", b"ENCH", b"SPEL", b"ARMO", b"WEAP", b"FLST")))
    parsed = list(records(output_path.read_bytes()))
    assert len(parsed) == count + 1
    assert len({r[1] for r in parsed}) == len(parsed)
    assert all(r[1] >> 24 == 1 and 0x800 <= (r[1] & 0xFFFFFF) <= 0xFFF for r in parsed[1:])
    assert unpack_from("<I", output_path.read_bytes(), 8)[0] == ESL_FLAG
    print("PASS TES4 flags=0x00000200 ESPFE, legacy SE light FormID range 0x800..0xFFF")
    print(f"PASS records={count} local_additions_only=True samples=4 enchantments=4 abilities=160")


if __name__ == "__main__":
    write(*(Path(arg) for arg in sys.argv[1:]))
