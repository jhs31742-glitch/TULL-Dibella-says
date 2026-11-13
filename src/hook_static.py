from pathlib import Path
from struct import unpack_from as u, calcsize
root=Path(r'H:\Modding 2')
db=(root/'File Mod Skyrim SE/mods/Address Library for SKSE Plugins/SKSE/Plugins/version-1-5-97-0.bin').read_bytes()
fmt,*v,n=u('<6i',db)
assert fmt==1 and v==[1,5,97,0]
p=24+n
ptr,count=u('<2i',db,p)
p+=8
def get(fmt):
 global p
 x=u('<'+fmt,db,p)[0]; p+=calcsize('<'+fmt)
 return x
def step(mode,prior):
 if mode==0:return get('Q')
 if mode==1:return prior+1
 if mode==2:return prior+get('B')
 if mode==3:return prior-get('B')
 if mode==4:return prior+get('H')
 if mode==5:return prior-get('H')
 if mode==6:return get('H')
 if mode==7:return get('I')
 raise ValueError(mode)
i=off=0
for _ in range(count):
 mode=get('B'); i=step(mode&15,i)
 scale=ptr if mode&128 else 1
 off=step((mode>>4)&7,off//scale)*scale
 if i==37633:
  rva=off+0x16a
  break
else: raise RuntimeError('ID not found')
exe=(root/'STOCK GAME/Skyrim Special Edition/SkyrimSE.exe').read_bytes()
pe=u('<I',exe,0x3c)[0]
assert exe[pe:pe+4]==b'PE\0\0'
size=u('<H',exe,pe+20)[0]
for k in range(u('<H',exe,pe+6)[0]):
 q=pe+24+size+40*k
 virtual,va,raw,where=u('<4I',exe,q+8)
 if va<=rva<va+raw:
  pos=where+rva-va
  print(f'ID37633={off:#x} CALL_RVA={rva:#x} SECTION={exe[q:q+8]!r} BYTES={exe[pos:pos+5].hex()} NEAR={exe[pos-12:pos+16].hex()}')
  if exe[pos]!=0xe8:
   from collections import Counter
   from math import log2
   sample=exe[pos-2048:pos+2048]
   entropy=-sum(n/len(sample)*log2(n/len(sample)) for n in Counter(sample).values())
   entry=u('<I',exe,pe+24+16)[0]
   bound=False
   for j in range(u('<H',exe,pe+6)[0]):
    section=pe+24+size+40*j
    v,base=u('<2I',exe,section+8)
    bound |= exe[section:section+5]==b'.bind' and base<=entry<base+v
   if bound and entropy>7.8:
    print(f'RUNTIME_OPCODE_REQUIRED packed_entry=.bind local_entropy={entropy:.3f}')
    break
   raise RuntimeError('Unpacked call opcode mismatch')
  destination=rva+5+u('<i',exe,pos+1)[0]
  print(f'PASS TARGET_RVA={destination:#x}')
  break
else: raise RuntimeError('RVA not found')
