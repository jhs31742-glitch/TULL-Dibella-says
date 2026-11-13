from ctypes import Structure, c_uint32, c_void_p, c_char_p, c_bool, POINTER, byref, WinDLL
from pathlib import Path

class SKSEInterface(Structure):
    _fields_ = [(n, c_uint32) for n in ("skse", "runtime", "editor", "is_editor")] + [
        (n, c_void_p) for n in ("query", "handle", "release", "plugin_info")]

class PluginInfo(Structure):
    _fields_ = [("info_version", c_uint32), ("name", c_char_p), ("version", c_uint32)]

def pack(a,b,c,d):
    return (a<<24)|(b<<16)|(c<<4)|d

dll=WinDLL(str(Path("build/SexLabLifetimeProgression.dll").resolve()))
assert dll.SKSEPlugin_Load
query=dll.SKSEPlugin_Query
query.argtypes=[POINTER(SKSEInterface), POINTER(PluginInfo)]
query.restype=c_bool
for runtime, editor, expected in [
    ((1,5,97,0),False,True), ((1,5,97,0),True,False), ((1,5,96,0),False,False),
    ((1,6,640,0),False,False), ((1,6,1170,0),False,False), ((1,7,0,0),False,False),
    ((1,4,15,0),False,False)]:
    proxy=SKSEInterface(pack(2,0,20,0),pack(*runtime),0,editor)
    info=PluginInfo()
    assert query(byref(proxy),byref(info))==expected, runtime
    assert info.name==b"SexLabLifetimeProgression" and info.version==pack(0,1,9,0)
    print(".".join(map(str,runtime)),f"editor={int(editor)}",f"accepted={int(expected)}")
print("HOST_DLL_LOAD_PASS PE=0.1.9.0 QUERY_GATE_7_PASS; SKSEPlugin_Load not invoked")
