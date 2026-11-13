from pathlib import Path
from struct import pack,pack_into,unpack_from
from make_esp import records,subs
import sys
source,dest=map(Path,sys.argv[1:]);raw=source.read_bytes();changes=[]
def walk(lo,hi):
 out=bytearray()
 while lo<hi:
  tag,size,flags,fid,*_=unpack_from('<4sIIIIHH',raw,lo)
  if tag==b'GRUP':
   body=walk(lo+24,lo+size);header=bytearray(raw[lo:lo+24]);pack_into('<I',header,4,len(body)+24);out.extend(header+body);lo+=size;continue
  fields=[]
  for sub,value in subs(raw[lo+24:lo+24+size]):
   if tag==b'MGEF' and 0x01000820<=fid<=0x01000823 and sub==b'DNAM':
    value=('디벨라의 갈망' if fid%2==0 else '디벨라의 순정').encode('utf-8')+b'\0';changes.append(fid)
   fields.append((sub,value))
  body=b''.join(pack('<4sH',k,len(v))+v for k,v in fields);header=bytearray(raw[lo:lo+24]);pack_into('<I',header,4,len(body));out.extend(header+body);lo+=24+size
 return bytes(out)
new=walk(0,len(raw));assert len(changes)==4
def mechanics(data):return [(t,f,v,[(k,d) for k,d in subs(b) if not(t==b'MGEF' and 0x01000820<=f<=0x01000823 and k==b'DNAM')]) for t,f,v,b in records(data)]
assert mechanics(raw)==mechanics(new)
dest.write_bytes(new);print('PASS exactly four enchantment DNAM labels; all mechanics byte-identical')
