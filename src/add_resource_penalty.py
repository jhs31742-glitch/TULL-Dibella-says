from pathlib import Path
from struct import pack, unpack_from, pack_into
from make_esp import LOCAL, records, subs, make_record, zstr
import sys

SPELL=0xFE8
EFFECT=0xFE9
path=Path(sys.argv[1])
old=path.read_bytes()
before=list(records(old))
by_id={fid:(tag,body) for tag,fid,_,body in before}
assert not set(by_id) & {LOCAL+i for i in range(SPELL,EFFECT+3)}
spell_template=dict(subs(by_id[LOCAL|5][1]))
effect_template=dict(subs(by_id[LOCAL|0x100][1]))
fields=[(b"EDID",zstr("TDSBaseResourcePenalty")),(b"OBND",bytes(12)),
        (b"FULL",zstr("기본 자원 감소")),(b"SPIT",spell_template[b"SPIT"]),(b"DESC",zstr(""))]
effects=[]
for i,(name,av) in enumerate(zip(("Health","Magicka","Stamina"),(24,25,26))):
    local_id=LOCAL|(EFFECT+i)
    data=bytearray(effect_template[b"DATA"])
    pack_into("<I",data,0,unpack_from("<I",data)[0]|4)
    pack_into("<i",data,68,av)
    effects.append(make_record(b"MGEF",local_id,[(b"EDID",zstr("TDSBaseResourcePenalty"+name)),
        (b"FULL",zstr("기본 자원 감소")),(b"DATA",data),(b"DNAM",zstr(""))]))
    fields.extend([(b"EFID",pack("<I",local_id)),(b"EFIT",pack("<fII",0.0,0,0))])
extra={b"MGEF":b"".join(effects),b"SPEL":make_record(b"SPEL",LOCAL|SPELL,fields)}
pos=24+len(before[0][3])
out=bytearray(old[:pos])
version,count,next_id=unpack_from("<fII",out,30)
pack_into("<fII",out,30,version,count+4,max(next_id,EFFECT+3))
while pos<len(old):
    sig,size,group=unpack_from("<4sI4s",old,pos)
    assert sig==b"GRUP" and size>=24
    add=extra.pop(group,b"")
    out.extend(old[pos:pos+4]+pack("<I",size+len(add))+old[pos+8:pos+size]+add)
    pos+=size
assert not extra
after=list(records(out))
assert len(after)==len(before)+4
assert [item for item in after[1:] if item[1] in by_id]==before[1:]
path.write_bytes(out)
print("PASS: added one resource ability and three detrimental AV effects; all existing records preserved")
