from pathlib import Path
from struct import pack, unpack_from, pack_into
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter
from add_progression_abilities import records, make_record, zstr, subs

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "package"
POT = Path(r"G:\SkyrimDev\staging\MagickaBarrier_GlyphRise_20261014\MOD_FILES")
MODEL = POT / r"meshes\PathOfTULLBUNG\Stage15\Barrier\TULL_Barrier_GlyphRise.nif"
OLD_TEXTURE = rb"textures\pathoftullbung\stage15\barrier\barrier_glyph_columns.dds"
PALETTE = [
    ("c8f7ff","ffbd48"), ("baf1ff","ffc6dc"), ("baf1ff","ff89b5"),
    ("baf1ff","ff80af"), ("baf1ff","ff76a9"), ("baf1ff","ff6ba2"),
    ("baf1ff","ff5e9a"), ("baf1ff","ff5593"), ("baf1ff","ff4c8d"),
]
# Independent emission envelopes preserve distinct blue-white and pink motes.
BLUE_ALPHA = (232,213,174,122,78,44,18,8,0)
ACCENT_ALPHA = (250,76,124,180,217,242,248,252,255)
ACCENT_MOTES = (2,1,1,2,3,3,3,4,4)

def texture(path, colors, tier):
    # Four atlas columns mapped to the original rising billboards.
    # The gaps keep the glow soft and stop adjacent samples at the UV edges.
    width=height=256
    image=Image.new("RGBA",(width,height))
    light=Image.new("RGBA",image.size)
    particles=Image.new("RGBA",image.size)
    wash=ImageDraw.Draw(light)
    paint=ImageDraw.Draw(particles)
    palette=[tuple(bytes.fromhex(color)) for color in colors]
    for column in range(4):
        rng=random.Random(4810+column)
        for j in range(4):
            x=column*64+rng.uniform(17,47)
            y=rng.uniform(23,232)
            accent=(j+column)%4<ACCENT_MOTES[tier]
            color=palette[1 if accent else 0]
            alpha=(ACCENT_ALPHA if accent else BLUE_ALPHA)[tier]
            radius=rng.uniform(1.8,4.2)
            wash.ellipse((x-10,y-13,x+10,y+13),fill=color+(alpha//4,))
            paint.ellipse((x-radius,y-radius,x+radius,y+radius),fill=color+(round(alpha*rng.uniform(.76,1)),))
        for j in range(0 if tier<6 else 1 if tier<8 else 2):
            x=column*64+23+(j*11+column*3)%22
            y=50+j*65+column*9
            size=13+(column+j)%3*2
            points=[]
            for step in range(48):
                t=step*math.tau/48
                points.append((x+size*math.sin(t)**3/2,
                    y-size*(13*math.cos(t)-5*math.cos(2*t)-2*math.cos(3*t)-math.cos(4*t))/32))
            paint.polygon(points,fill=palette[1]+(232,))
    image=Image.alpha_composite(image,light.filter(ImageFilter.GaussianBlur(7)))
    image=Image.alpha_composite(image,particles.filter(ImageFilter.GaussianBlur(3)))
    image=Image.alpha_composite(image,particles.filter(ImageFilter.GaussianBlur(.7)))
    header=pack("<31I",124,0x100F,height,width,width*4,0,0,*([0]*11),
                32,0x41,0,32,0xFF0000,0xFF00,0xFF,0xFF000000,0x1000,0,0,0,0)
    path.write_bytes(b"DDS "+header+image.tobytes("raw","BGRA"))

def main(source):
    donor=MODEL.read_bytes()
    assert donor.count(OLD_TEXTURE)==12
    arts=[]
    for tier,color in enumerate(PALETTE):
        stem=f"aura_{tier:02}"
        prefix="textures\\tds\\aura\\"
        tex=prefix+stem+"_"+"r"*(len(OLD_TEXTURE)-len(prefix)-len(stem)-5)+".dds"
        new=tex.encode("ascii")
        assert len(new)==len(OLD_TEXTURE)
        target=ASSETS / tex
        target.parent.mkdir(parents=True,exist_ok=True)
        texture(target,color,tier)
        relative=f"TDS\\Aura\\{stem}.nif"
        nif=ASSETS/"meshes"/relative
        nif.parent.mkdir(parents=True,exist_ok=True)
        raw=donor.replace(OLD_TEXTURE,new)
        assert raw.replace(new,OLD_TEXTURE)==donor and raw.count(new)==12
        nif.write_bytes(raw)
        arts.append(make_record(b"ARTO",0x01000FF0+tier,[(b"EDID",zstr(f"TDSCombatAura{tier:02}")),
                    (b"OBND",bytes(12)),(b"MODL",zstr(relative)),(b"DNAM",pack("<I",1))]))
    original=source.read_bytes()
    before=list(records(original))
    assert not any(tag==b"ARTO" for tag, *_ in before)
    assert not ({fid for _,fid,_,_ in before} & set(range(0x01000FF0,0x01000FF9)))
    patched=bytearray(original)
    assert patched[24:28]==b"HEDR"
    version,count,next_id=unpack_from("<fII",patched,30)
    assert round(version,2)==1.71
    pack_into("<fII",patched,30,version,count+len(arts),max(next_id,0xFF9))
    data=b"".join(arts)
    patched.extend(pack("<4sI4siHHHH",b"GRUP",24+len(data),b"ARTO",0,0,0,0,0)+data)
    after=list(records(patched))
    assert after[1:len(before)]==before[1:] and len(after)==len(before)+9
    assert all(tag==b"ARTO" and dict(subs(body))[b"DNAM"]==pack("<I",1) for tag,_,_,body in after[-9:])
    (ASSETS/"TULL Dibella says.esp").write_bytes(patched)
    print("PASS: 9 DDS, 9 NIFs with original loop controllers, 9 ARTO records, all prior records identical")

if __name__=="__main__":
    main(Path(sys.argv[1]))
