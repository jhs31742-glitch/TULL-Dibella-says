from pathlib import Path
from struct import pack,unpack_from,pack_into
from make_esp import records,subs,make_record,zstr,LOCAL
import sys
path,master=map(Path,sys.argv[1:])
old=path.read_bytes(); before=list(records(old)); ids={fid:(tag,body) for tag,fid,_,body in before}
assert LOCAL|0xFEC not in ids and LOCAL|0xFED not in ids
vanilla={fid:(tag,body) for tag,fid,_,body in records(master.read_bytes(),allowed={b'SPEL',b'EQUP'})}
template=dict(subs(vanilla[0xC4DE1][1]));assert unpack_from('<I',template[b'ETYP'])[0]==0x25BEE
spit=bytearray(template[b'SPIT']);pack_into('<f',spit,12,0.0)
data=bytearray(dict(subs(ids[LOCAL|0x820][1]))[b'DATA'])
pack_into('<IIi',data,80,1,0,-1) # disk casting=FireAndForget, delivery=Self
name='디벨라의 봉헌'
effect=make_record(b'MGEF',LOCAL|0xFED,[(b'EDID',zstr('TDSOfferingMarker')),(b'FULL',zstr(name)),(b'DATA',data),(b'DNAM',zstr(''))])
spell=make_record(b'SPEL',LOCAL|0xFEC,[(b'EDID',zstr('TDSOfferingPower')),(b'OBND',bytes(12)),(b'FULL',zstr(name)),(b'ETYP',template[b'ETYP']),(b'SPIT',spit),(b'DESC',zstr('양수 만족도를 모두 제거하고 절반을 타락도로 전환합니다. 소수점 이하는 버립니다. 만족도가 0 이하이면 전환하지 않습니다. 신체 회복 II: 제거 전 만족도만큼 골드를 얻습니다. III: 골드 획득량이 두 배가 됩니다.')),(b'EFID',pack('<I',LOCAL|0xFED)),(b'EFIT',pack('<fII',0.0,0,0))])
extra={b'MGEF':effect,b'SPEL':spell};pos=24+len(before[0][3]);out=bytearray(old[:pos]);version,count,nxt=unpack_from('<fII',out,30);pack_into('<fII',out,30,version,count+2,max(nxt,0xFEE))
while pos<len(old):
 sig,size,group=unpack_from('<4sI4s',old,pos);assert sig==b'GRUP';add=extra.pop(group,b'');out.extend(old[pos:pos+4]+pack('<I',size+len(add))+old[pos+8:pos+size]+add);pos+=size
assert not extra
after=list(records(out));assert len(after)==len(before)+2
assert [r for r in after[1:] if r[1] in ids]==before[1:]
path.write_bytes(out);print('PASS appended offering SPEL FEC + inert MGEF FED; all existing records unchanged')
