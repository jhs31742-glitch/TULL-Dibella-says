from pathlib import Path
from struct import pack_into,unpack_from,pack
import json,sys
from make_esp import records,subs
source,destination,receipt=map(Path,sys.argv[1:]);old=source.read_bytes();allrec=list(records(old));owners={}
for tag,fid,_,body in allrec:
 fields=list(subs(body));d=dict(fields)
 if tag==b'SPEL':expected=unpack_from('<II',d[b'SPIT'],16)
 elif tag==b'ENCH':expected=(unpack_from('<I',d[b'ENIT'],8)[0],unpack_from('<I',d[b'ENIT'],16)[0])
 else:continue
 for key,value in fields:
  if key==b'EFID':
   effect=unpack_from('<I',value)[0];owners.setdefault(effect,set()).add(expected)
changes=[]
def walk(lo,hi):
 out=bytearray()
 while lo<hi:
  tag,size,flags,fid,_,_,_=unpack_from('<4sIIIIHH',old,lo)
  if tag==b'GRUP':
   out.extend(old[lo:lo+24]+walk(lo+24,lo+size));lo+=size;continue
  body=bytearray(old[lo+24:lo+24+size])
  if tag==b'MGEF':
   pos=0
   while pos<len(body):
    sub,n=unpack_from('<4sH',body,pos);start=pos+6
    if sub==b'DATA':
     cast,delivery=unpack_from('<ii',body,start+80)
     if cast==-1:
      choices=owners.get(fid,set());assert len(choices)==1,(hex(fid),choices)
      expected=next(iter(choices));assert expected[0] in [0,1,2,3] and expected[1] in range(6)
      before=bytes(body[start:start+n]);pack_into('<IIi',body,start+80,*expected,-1)
      if fid==0x01000FED:pack_into('<I',body,start+92,0) # prior wrong-offset write; no casting art intended
      assert bytes(body[start:start+80])==before[:80]
      assert bytes(body[start+96:start+n])==before[96:]
      changes.append({'FormID':hex(fid),'before_cast':cast,'after_cast':expected[0],'after_delivery':expected[1],'secondaryAV':'None'})
    pos=start+n
  out.extend(old[lo:lo+24]+body);lo+=24+size
 return bytes(out)
new=walk(0,len(old));assert len(new)==len(old) and len(changes)==1378
# Independently check that every modified record differs only within the four metadata words.
oldmap={fid:(t,b) for t,fid,_,b in allrec}
for tag,fid,_,body in records(new):
 before=oldmap[fid][1]
 if body!=before:
  assert tag==b'MGEF'
  for (a,av),(b,bv) in zip(subs(before),subs(body)):
   assert a==b
   if av!=bv:assert a==b'DATA' and av[:80]==bv[:80] and av[96:]==bv[96:]
destination.write_bytes(new);receipt.write_text(json.dumps(changes,indent=2),encoding='utf-8');print('PASS repaired 1378 invalid casting enums using actual SPEL/ENCH owners; no IDs, effects, magnitudes or non-metadata bytes changed')
