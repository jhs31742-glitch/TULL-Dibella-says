from pathlib import Path
from struct import pack,pack_into,unpack_from
import sys
from make_esp import records,subs,make_record,make_group,zstr,LOCAL
source,reference,destination=map(Path,sys.argv[1:])
old=source.read_bytes();before=list(records(old));ids={f:(t,b) for t,f,_,b in before};ref={f:(t,list(subs(b))) for t,f,_,b in records(reference.read_bytes())}
assert not set(ids)&{LOCAL|i for i in range(0xF60,0xF6C)}
def wstr(v):
 b=v.encode('utf-8');return pack('<H',len(b))+b
def effect_vmad(mode):
 return pack('<HHH',5,2,1)+wstr('TDS_ImpulseEffect')+bytes([0])+pack('<H',1 if mode else 0)+(wstr('Mode')+bytes([3,1])+pack('<i',1) if mode else b'')
extra={b'MGEF':[],b'SPEL':[],b'QUST':[],b'MESG':[]}
for s,e,template_s,template_e,name,mode in [(0xF60,0xF63,0x020012C8,0x020012CA,'디벨라의 충동 - 자신',0),(0xF61,0xF64,0x020012C5,0x020012C7,'디벨라의 충동 - 대상',0),(0xF62,0xF65,0x020012C8,0x020012CA,'자위하기',1)]:
 ef=[]
 for tag,data in ref[template_e][1]:
  if tag==b'EDID':data=zstr('TDSImpulseEffect'+str(mode)+str(e))
  elif tag==b'FULL':data=zstr(name)
  elif tag==b'VMAD':data=effect_vmad(mode)
  ef.append((tag,data))
 extra[b'MGEF'].append(make_record(b'MGEF',LOCAL|e,ef))
 fields=[]
 for tag,data in ref[template_s][1]:
  if tag==b'EDID':data=zstr('TDSImpulse'+('Solo' if mode else 'Self' if s==0xF60 else 'Target'))
  elif tag==b'FULL':data=zstr(name)
  elif tag==b'ETYP' and mode:data=pack('<I',0x25BEE)
  elif tag==b'SPIT':
   data=bytearray(data);pack_into('<I',data,0,0);pack_into('<I',data,4,0);pack_into('<I',data,8,3 if mode else 0);data=bytes(data)
  elif tag==b'EFID':data=pack('<I',LOCAL|e)
  elif tag==b'EFIT':data=pack('<fII',0,0,1 if mode else 15 if s==0xF60 else 5)
  elif tag==b'DESC':continue
  fields.append((tag,data))
 desc='자위 애니메이션을 시작합니다. SexLab에서 해당 애니메이션을 활성화해야 합니다.' if mode else '자신을 장면 참가자로 선택합니다. 효과가 끝나면 선택한 대상들과 장면을 시작합니다.' if s==0xF60 else '대상을 장면 참가자로 선택합니다. 최대 5명까지 선택할 수 있습니다.'
 fields.append((b'DESC',zstr(desc)));extra[b'SPEL'].append(make_record(b'SPEL',LOCAL|s,fields))
q=[]
for tag,data in ref[0x02000D62][1]:
 if tag==b'EDID':data=zstr('TDSImpulseControllerQuest')
 elif tag==b'FULL':data=zstr('디벨라의 충동')
 elif tag==b'FLTR':data=zstr('TDS')
 elif tag==b'ANAM':data=pack('<I',1)
 elif tag==b'VMAD':
  index=data.index(wstr('sslMatchMaker'));prefix=data[:index].replace(pack('<I',0x02000D62),pack('<I',LOCAL|0xF66));data=prefix+wstr('TDS_ImpulseController')+bytes([0])+pack('<H',0)
 q.append((tag,data))
extra[b'QUST'].append(make_record(b'QUST',LOCAL|0xF66,q))
messages=['지금은 해당 대상과 장면을 시작할 수 없습니다.','참가할 수 없는 대상이 있어 장면을 시작하지 못했습니다.','SexLab에서 자위 애니메이션을 활성화하세요.','SexLab 장면을 시작하지 못했습니다.','장면 기능이 아직 준비되지 않았습니다.']
for i,message in enumerate(messages):
 extra[b'MESG'].append(make_record(b'MESG',LOCAL|0xF67+i,[(b'EDID',zstr('TDSImpulseNotice'+str(i))),(b'DESC',zstr(message)),(b'DNAM',pack('<I',0)),(b'TNAM',pack('<I',4))]))
pos=24+len(before[0][3]);out=bytearray(old[:pos]);version,count,nextid=unpack_from('<fII',out,30);pack_into('<fII',out,30,version,count+12,nextid)
while pos<len(old):
 sig,size,group=unpack_from('<4sI4s',old,pos);assert sig==b'GRUP';add=b''.join(extra.pop(group,[]));out.extend(old[pos:pos+4]+pack('<I',size+len(add))+old[pos+8:pos+size]+add);pos+=size
for group,entries in extra.items():out.extend(make_group(group,entries))
after=list(records(out));assert len(after)==len(before)+12;assert [r for r in after[1:] if r[1] in ids]==before[1:]
destination.write_bytes(out);print('PASS added 3 spells, 3 effects, alias quest and 5 localized messages; all previous records unchanged')
