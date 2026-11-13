"""Append independent TDS AV abilities to the verified named ESPFE."""
import sys
from math import copysign
from pathlib import Path
from struct import pack, unpack_from
from make_esp import LOCAL, ESL_FLAG, make_record, records, subs, zstr

ABILITIES = (
    ("Guard", ((39, 40),)),
    ("Ward", ((41, 10), (42, 10), (43, 10), (44, 5), (45, 10), (40, 10))),
    ("Predator", ((39, -40),)),
    ("Carapace", ((39, 80),)),
    ("Purity", ((24, 200), (25, 200), (26, 200), (39, 100),
                (155, 80), (156, 80), (157, 80),
                (41, 50), (42, 50), (43, 50), (44, 20), (45, 50), (40, 50))),
)

# R23 tier additions for the ActorValue channels. Weapon damage, received
# damage, skill and spell cost have separate engine application paths.
RESOURCES = (24, 25, 26)
REGEN = (155, 156, 157)
ELEMENTAL = (41, 42, 43)
DISEASE_POISON = (45, 40)
KARMA_STEPS = (
    ((RESOURCES, 10), ((39,), -10)),
    (((39,), 20), (DISEASE_POISON, -5)),
    (((39,), -15),),
    ((ELEMENTAL, 5),),
    ((RESOURCES, 20), (REGEN, 10), ((39,), -20), ((44,), -5)),
    (((39,), 30), ((44,), 5), (DISEASE_POISON, 10), (ELEMENTAL, -5)),
    ((RESOURCES, 30), ((39,), -40), (ELEMENTAL, -5)),
)

def karma_abilities():
    totals = {}
    for tier, changes in enumerate(KARMA_STEPS, 1):
        for actors, amount in changes:
            for av in actors:
                totals[av] = totals.get(av, 0) + amount
        for variant, scale in enumerate((1.0, 0.25, 2.0)):
            yield (f"Karma{tier}Variant{variant}",
                   tuple((av, amount * scale if amount < 0 else amount)
                         for av, amount in sorted(totals.items()) if amount))

ABILITIES += tuple(karma_abilities())

# Source X stage percentages from the same header used by native selection.
def add_satisfaction_abilities():
    import re
    rules = Path(__file__).with_name("ProgressionRules.h").read_text(encoding="utf-8")
    for negative, name in ((False, "positiveSteps"), (True, "negativeSteps")):
        block = rules.split(f"{name}{{", 1)[1].split("};", 1)[0]
        percentages = [int(p) for p in re.findall(r"EffectStep\{\d+,\s*(\d+)\}", block)]
        assert len(percentages) == 13
        for index, percent in enumerate(percentages):
            for variant, multiplier in enumerate((1.0, 0.25) if negative else (1.0,)):
                values = ((39, 60), (41, 20), (42, 20), (43, 20),
                          (44, 10), (45, 20), (40, 20))
                amount = percent / 100 * multiplier * (-1 if negative else 1)
                yield (f"Satisfaction{int(negative)}_{index}_{variant}",
                       tuple((av, base * amount) for av, base in values))

KARMA_END = len(ABILITIES)
ABILITIES += tuple(add_satisfaction_abilities())

LEGACY_END = len(ABILITIES)

def component_karma():
    positive, negative = {}, {}
    for tier, changes in enumerate(KARMA_STEPS, 1):
        for actors, amount in changes:
            target = positive if amount > 0 else negative
            for av in actors:
                target[av] = target.get(av, 0) + amount
        for variant, scale in enumerate((0.25, 2.0)):
            values = ((av, positive.get(av, 0) + negative.get(av, 0) * scale)
                      for av in sorted(positive.keys() | negative.keys()))
            yield (f"Karma{tier}Components{variant}", tuple((av, amount) for av, amount in values if amount))

def low_karma_satisfaction():
    negative = ABILITIES[KARMA_END + 13:LEGACY_END]
    for tier, multiplier in enumerate((0.75, 0.80, 0.85, 0.90)):
        for name, values in negative:
            resistant = name.endswith("_1")
            factor = max(0.0, multiplier - 0.75) * 4 if resistant else multiplier
            yield (f"LowKarma{tier}{name}", tuple((av, amount * factor) for av, amount in values))

ABILITIES += tuple(component_karma()) + tuple(low_karma_satisfaction())
SKILL_START = len(ABILITIES)
# Skill AVs rather than the Fortify enchantment percent modifiers.
SKILL_ACTOR_VALUES = (6, 7, 8, 9, 11, 12, 22, 20)
ABILITIES += (("PuritySkills", tuple((av, 50) for av in SKILL_ACTOR_VALUES)),
              ("KarmaSkills", tuple((av, 10) for av in SKILL_ACTOR_VALUES)))

SPEECH_START = len(ABILITIES)
SPEECH_BONUSES = (7, 14, 21, 29, 36, 43, 50)
ABILITIES += tuple((f"KarmaSpeech{i}", ((17, amount),))
                   for i, amount in enumerate(SPEECH_BONUSES, start=1))

# Quantize only TDS-owned effect values. Resource-base penalty is separate.
from decimal import Decimal, ROUND_DOWN
ABILITIES = tuple((name, tuple((av, float(Decimal(str(amount)).quantize(
    Decimal("0.1"), rounding=ROUND_DOWN))) for av, amount in entries))
    for name, entries in ABILITIES)

FIRST_EFFECT_ID = 0x100
EXTENDED_ESL_VERSION = 1.71

def spell_id(index):
    return 1 + index

def write_native_table(path):
    rows = ["#pragma once", "", "#include <array>", "#include <span>", "#include <utility>", "",
            "namespace tds::growth_forms {", "using Entry = std::pair<int,float>;"]
    for name, values in ABILITIES:
        entries = ", ".join(f"Entry{{{av}, {float(amount)}f}}" for av, amount in values)
        rows.append(f"inline constexpr std::array {name}{{{entries}}};")
    names = ", ".join(name for name, _ in ABILITIES)
    rows.append(f"inline constexpr std::array<std::span<const Entry>, {len(ABILITIES)}> effects{{{names}}};")
    ids = ", ".join(hex(spell_id(i)) + "u" for i in range(len(ABILITIES)))
    rows.append(f"inline constexpr std::array spellIds{{{ids}}};")
    rows.append(f"inline constexpr auto firstEffectId = {hex(FIRST_EFFECT_ID)}u;")
    rows.append(f"inline constexpr auto puritySkillSlot = {SKILL_START}u;")
    rows.append(f"inline constexpr auto karmaSkillSlot = {SKILL_START + 1}u;")
    rows.append(f"inline constexpr auto karmaSpeechSlot = {SPEECH_START}u;")
    rows.append("}  // namespace tds::growth_forms")
    with path.open("x", encoding="ascii", newline="\n") as output:
        output.write("\n".join(rows) + "\n")

def fix_template_effect_links(raw):
    """Repair the old generated templates' misplaced DualCastScale."""
    result = bytearray(raw)
    _, header_size = unpack_from("<4sI", raw)
    cursor = 24 + header_size
    repaired = 0
    while cursor < len(raw):
        tag, group_size, group = unpack_from("<4sI4s", raw, cursor)
        assert tag == b"GRUP"
        if group == b"MGEF":
            pos = cursor + 24
            while pos < cursor + group_size:
                record_type, size, flags, fid = unpack_from("<4sIII", raw, pos)
                assert record_type == b"MGEF" and not flags & 0x40000 and fid >> 24 == 1
                payload = raw[pos+24:pos+24+size]
                data = dict(subs(payload))[b"DATA"]
                signature = pack("<4sH", b"DATA", len(data)) + data
                assert payload.count(signature) == 1
                start = pos + 24 + payload.index(signature) + 6
                assert unpack_from("<II", raw, start+104) == (0, 0x3F800000)
                result[start+104:start+112] = pack("<fI",1.0,0)
                repaired += 1
                pos += 24 + size
        cursor += group_size
    assert repaired == 28
    return result

def build(source, target, native_table):
    old = fix_template_effect_links(source.read_bytes())
    original = list(records(old))
    tag, _, _, header = original[0]
    assert tag == b"TES4" and unpack_from("<I", old, 8)[0] == ESL_FLAG
    originals = {fid for _, fid, _, _ in original}
    template = next(fields for t, fid, _, fields in original if t == b"SPEL" and fid == LOCAL | 0x901)
    spell_type = dict(subs(template))[b"SPIT"]
    additions = {b"MGEF": [], b"SPEL": []}
    effect_id = FIRST_EFFECT_ID
    assert spell_id(len(ABILITIES)-1) < FIRST_EFFECT_ID
    for index, (name, values) in enumerate(ABILITIES):
        fields = [(b"EDID", zstr("TDSProgression" + name)), (b"OBND", bytes(12)),
                  (b"FULL", zstr("TDS " + name)), (b"SPIT", spell_type), (b"DESC", zstr(""))]
        for av, amount in values:
            flags = 0x08208802 | (4 if copysign(1.0, amount) < 0 else 0)
            data = bytearray(152)
            data[:20] = pack("<IfIii", flags, 0.0, 0, -1, -1)
            data[68:72] = pack("<i", av)
            data[80:84] = pack("<i", -1)
            data[104:108] = pack("<f", 1.0)
            additions[b"MGEF"].append(make_record(b"MGEF", LOCAL | effect_id,
                [(b"EDID", zstr(f"TDSProgression{name}{av}")), (b"FULL", zstr("TDS " + name)),
                 (b"DATA", data), (b"DNAM", zstr(""))]))
            fields += [(b"EFID", pack("<I", LOCAL | effect_id)),
                       (b"EFIT", pack("<fII", abs(amount), 0, 0))]
            effect_id += 1
        additions[b"SPEL"].append(make_record(b"SPEL", LOCAL | spell_id(index), fields))
    pos = 24 + len(header)
    out = bytearray(old[:pos])
    hedr = out.index(b"HEDR", 24)
    assert hedr == 24
    _, count, prior_next = unpack_from("<fII", out, hedr + 6)
    out[hedr + 6:hedr + 18] = pack("<fII", EXTENDED_ESL_VERSION,
        count + sum(map(len, additions.values())), max(prior_next,effect_id))
    seen = set()
    while pos < len(old):
        signature, size, group = unpack_from("<4sI4s", old, pos)
        assert signature == b"GRUP" and size >= 24 and pos + size <= len(old)
        extra = b"".join(additions.get(group, ()))
        if extra:
            assert group not in seen
            seen.add(group)
        out += old[pos:pos + 4] + pack("<I", size + len(extra)) + old[pos + 8:pos + size] + extra
        pos += size
    assert seen == additions.keys()
    parsed = list(records(out))
    assert len({fid for _, fid, _, _ in parsed}) == len(parsed)
    assert all(fid >> 24 == 1 and 1 <= (fid & 0xFFFFFF) <= 0xFFF for _, fid, _, _ in parsed[1:])
    assert any(1 <= (fid & 0xFFFFFF) < 0x800 for _, fid, _, _ in parsed[1:])
    assert effect_id <= 0x800
    assert [record for record in parsed[1:] if record[1] in originals] == original[1:]
    assert len(parsed) == len(original) + sum(map(len, additions.values()))
    by_id = {fid: (tag, list(subs(payload))) for tag, fid, _, payload in parsed[1:]}
    for index, (_, values) in enumerate(ABILITIES):
        tag, fields = by_id[LOCAL | spell_id(index)]
        assert tag == b"SPEL"
        ids = [unpack_from("<I", value)[0] for key, value in fields if key == b"EFID"]
        fits = [unpack_from("<fII", value) for key, value in fields if key == b"EFIT"]
        assert len(ids) == len(fits) == len(values)
        for fid, (magnitude, area, duration), (av, amount) in zip(ids, fits, values):
            tag, effects = by_id[fid]
            data = dict(effects)[b"DATA"]
            flags = unpack_from("<I", data)[0]
            assert tag == b"MGEF" and fid not in originals and (fid & 0xFFFFFF) <= 0xFFF
            assert unpack_from("<ii", data, 64) == (0, av)
            assert bool(flags & 4) == (copysign(1.0, amount) < 0) and (magnitude, area, duration) == (unpack_from("<f", pack("<f", abs(amount)))[0], 0, 0)
    with target.open("xb") as handle:
        handle.write(out)
    write_native_table(native_table)
    print(f"PASS preserved {len(original) - 1} original records, {len(ABILITIES)} ability records, {effect_id - FIRST_EFFECT_ID} relocated effects")

if __name__ == "__main__":
    build(*map(Path, sys.argv[1:]))