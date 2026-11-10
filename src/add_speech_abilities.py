"""Append the seven Speech abilities without rewriting prior ESP records."""
from pathlib import Path
from struct import pack, unpack_from, pack_into
import sys

from add_progression_abilities import (
    ABILITIES, SPEECH_START, FIRST_EFFECT_ID, LOCAL, make_record,
    records, spell_id, subs, write_native_table, zstr,
)

def build(source, destination, native_table):
    old=source.read_bytes()
    before=list(records(old))
    ids={fid:(tag,body) for tag,fid,_,body in before}
    effects,spells=[],[]
    next_effect=FIRST_EFFECT_ID+sum(len(entries) for _,entries in ABILITIES[:SPEECH_START])
    original_spell=dict(subs(ids[LOCAL|spell_id(SPEECH_START-1)][1]))
    original_effect=dict(subs(ids[LOCAL|FIRST_EFFECT_ID][1]))
    for slot,(name,entries) in enumerate(ABILITIES[SPEECH_START:],SPEECH_START):
        [(av,amount)]=entries
        spell=LOCAL|spell_id(slot)
        effect=LOCAL|next_effect
        assert spell not in ids and effect not in ids
        next_effect+=1
        data=bytearray(original_effect[b"DATA"])
        pack_into("<i",data,68,av)
        effects.append(make_record(b"MGEF",effect,[
            (b"EDID",zstr("TDSProgression"+name+"17")),(b"FULL",zstr("타락의 화술")),
            (b"DATA",data),(b"DNAM",zstr(""))]))
        spells.append(make_record(b"SPEL",spell,[
            (b"EDID",zstr("TDSProgression"+name)),(b"OBND",bytes(12)),
            (b"FULL",zstr("타락의 화술")),(b"SPIT",original_spell[b"SPIT"]),
            (b"DESC",zstr("")),(b"EFID",pack("<I",effect)),(b"EFIT",pack("<fII",amount,0,0))]))
    additions={b"MGEF":b"".join(effects),b"SPEL":b"".join(spells)}
    pos=24+len(before[0][3])
    out=bytearray(old[:pos])
    version,count,prior_next=unpack_from("<fII",out,30)
    pack_into("<fII",out,30,version,count+len(effects)+len(spells),max(prior_next,next_effect))
    while pos<len(old):
        sig,size,group=unpack_from("<4sI4s",old,pos)
        assert sig==b"GRUP" and size>=24
        extra=additions.pop(group,b"")
        out.extend(old[pos:pos+4]+pack("<I",size+len(extra))+old[pos+8:pos+size]+extra)
        pos+=size
    assert not additions
    after=list(records(out))
    assert len(after)==len(before)+14
    assert [record for record in after[1:] if record[1] in ids]==before[1:]
    destination.write_bytes(out)
    write_native_table(native_table)
    print("PASS: 7 Speech abilities, 7 effects; old ESP records identical")

if __name__=="__main__":
    build(*map(Path,sys.argv[1:]))
