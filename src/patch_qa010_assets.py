from pathlib import Path
from struct import unpack_from, pack_into
import sys

from make_esp import LOCAL, records, subs, make_record, zstr
from build_aura_assets import PALETTE, texture
from PIL import Image, ImageDraw


def build(source, package, preview):
    raw=source.read_bytes()
    all_before=list(records(raw))
    by_id={fid:(tag,body) for tag,fid,_,body in all_before}
    first,second=LOCAL|0x800,LOCAL|0x801
    assert by_id[first][0]==by_id[second][0]==b"WEAP"
    model=list(subs(by_id[first][1]))
    originals=[dict(subs(by_id[fid][1])) for fid in (first,second)]
    replacements={}
    for i,fid in enumerate((first,second)):
        original=originals[i]
        fields=[(k,original[k] if k in (b"EDID",b"EITM",b"EAMT") else
                 zstr(("디벨라의 갈망" if i==0 else "디벨라의 순정")+" · 철검") if k==b"FULL" else v)
                for k,v in model]
        replacements[fid]=make_record(b"WEAP",fid,fields)
    offset=24+len(all_before[0][3])
    result=bytearray(raw[:offset])
    changed=set()
    while offset<len(raw):
        sig,length,tag=unpack_from("<4sI4s",raw,offset)
        assert sig==b"GRUP"
        stop=offset+length
        if tag!=b"WEAP":
            result.extend(raw[offset:stop])
        else:
            group=bytearray(raw[offset:offset+24])
            pos=offset+24
            while pos<stop:
                kind,size,_,fid=unpack_from("<4sIII",raw,pos)
                assert kind==b"WEAP"
                end=pos+24+size
                group.extend(replacements.get(fid,raw[pos:end]))
                if fid in replacements:
                    changed.add(fid)
                pos=end
            assert pos==stop
            pack_into("<I",group,4,len(group))
            result.extend(group)
        offset=stop
    assert changed==replacements.keys()
    after=list(records(result))
    assert len(after)==len(all_before)
    assert all(a[:3]==b[:3] and (a==b or a[1] in changed) for a,b in zip(all_before,after))
    (package/"TULL Dibella says.esp").write_bytes(result)
    atlas=Image.new("RGB",(180*9,220),"#172030")
    labels=ImageDraw.Draw(atlas)
    for tier,colors in enumerate(PALETTE):
        target,=list((package/"textures/tds/aura").glob(f"aura_{tier:02}_*.dds"))
        texture(target,colors,tier)
        with Image.open(target) as image:
            rgba=image.convert("RGBA").resize((164,164))
            atlas.paste(rgba,(180*tier+8,14),rgba)
        labels.text((180*tier+12,190),"PURITY" if tier==0 else f"CORRUPTION {tier}",fill="white")
    atlas.save(preview)
    print("PASS same iron sword template, existing forms unchanged outside WEAP 800/801, 9 DDS regenerated")


if __name__=="__main__":
    build(*map(Path,sys.argv[1:]))
