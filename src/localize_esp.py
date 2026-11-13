from pathlib import Path
from struct import pack,pack_into,unpack_from
from make_esp import records,subs,make_record,LOCAL
import re,sys
source,ko,en=map(Path,sys.argv[1:]);raw=source.read_bytes()
english={
'SexLab 장면을 시작하지 못했습니다.':'SexLab could not start the scene.',
'SexLab에서 자위 애니메이션을 활성화하세요.':'Enable a masturbation animation in SexLab.',
'기본 자원 감소':'Base Resource Reduction',
'대상을 장면 참가자로 선택합니다. 최대 5명까지 선택할 수 있습니다.':'Select the target for a scene. Select up to five actors.',
'디벨라의 갈망':"Dibella's Longing",'디벨라의 순정':"Dibella's Innocence",'디벨라의 봉헌':"Dibella's Offering",'디벨라의 충동':"Dibella's Impulse",'자위하기':'Masturbate',
'디벨라의 충동 - 대상':"Dibella's Impulse - Target",'디벨라의 충동 - 자신':"Dibella's Impulse - Self",
'양수 만족도를 모두 제거하고 절반을 타락도로 전환합니다. 소수점 이하는 버립니다. 만족도가 0 이하이면 전환하지 않습니다. 신체 회복 II: 제거 전 만족도만큼 골드를 얻습니다. III: 골드 획득량이 두 배가 됩니다.':'Consume all positive satisfaction and gain half as corruption, rounded down. No conversion at zero or negative satisfaction. Body Recovery II grants gold equal to the consumed satisfaction. III doubles the gold reward.',
'자신을 장면 참가자로 선택합니다. 효과가 끝나면 선택한 대상들과 장면을 시작합니다.':'Select yourself for a scene. When the effect expires, start a scene with the selected actors.',
'자위 애니메이션을 시작합니다. SexLab에서 해당 애니메이션을 활성화해야 합니다.':'Start a solo masturbation animation. A suitable animation must be enabled in SexLab.',
'장면 기능이 아직 준비되지 않았습니다.':'The scene helper is not ready yet.',
'지금은 해당 대상과 장면을 시작할 수 없습니다.':'That actor is currently unavailable for a scene.',
'참가할 수 없는 대상이 있어 장면을 시작하지 못했습니다.':'An actor became unavailable; the scene could not start.',
'타락의 화술':'Corrupted Eloquence',
'디벨라의 갈망 · Silver Ring':"Dibella's Longing - Silver Ring",'디벨라의 순정 · Silver Necklace':"Dibella's Innocence - Silver Necklace",
'디벨라의 갈망 · 철검':"Dibella's Longing - Iron Sword",'디벨라의 순정 · 철검':"Dibella's Innocence - Iron Sword"}
korean={'디벨라의 갈망 · Silver Ring':'디벨라의 갈망 · 은반지','디벨라의 순정 · Silver Necklace':'디벨라의 순정 · 은목걸이'}
def convert(data,table,require):
 def walk(lo,hi):
  out=bytearray()
  while lo<hi:
   tag,size,flags,fid,unk,ver,unk2=unpack_from('<4sIIIIHH',data,lo)
   if tag==b'GRUP':
    body=walk(lo+24,lo+size);header=bytearray(data[lo:lo+24]);pack_into('<I',header,4,len(body)+24);out.extend(header+body);lo+=size;continue
   assert not flags&0x40000
   fields=[]
   for sub,value in subs(data[lo+24:lo+24+size]):
    if sub in [b'FULL',b'DESC'] or (tag==b'MGEF' and sub==b'DNAM'):
     text=value.rstrip(b'\0').decode('utf-8')
     if text in table:value=table[text].encode('utf-8')+b'\0'
     elif require and re.search('[가-힣]',text):raise AssertionError(text)
    fields.append((sub,value))
   body=b''.join(pack('<4sH',s,len(v))+v for s,v in fields);header=bytearray(data[lo:lo+24]);pack_into('<I',header,4,len(body));out.extend(header+body);lo+=24+size
  assert lo==hi
  return bytes(out)
 return walk(0,len(data))
kobytes=convert(raw,korean,False);enbytes=convert(raw,english,True);ko.write_bytes(kobytes);en.write_bytes(enbytes)
def semantic(b):
 return [(t,f,v,[(s,d) for s,d in subs(body) if not(s in [b'FULL',b'DESC'] or(t==b'MGEF' and s==b'DNAM'))]) for t,f,v,body in records(b)]
assert semantic(kobytes)==semantic(enbytes)==semantic(raw)
print('PASS Korean/English variants: identical FormIDs, masters, scripts and gameplay; text-only differences')
