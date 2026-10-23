import struct
def W(v): return struct.pack('<I', v & 0xffffffff)
class Asm:
    def __init__(self):
        self.ins=[]; self.labels={}
    def L(self,n): self.labels[n]=len(self.ins)
    def add(self,w): self.ins.append(("raw",w,None))
    def ldr(self,rd,rn,imm): self.add(0xE5900000|(rn<<16)|(rd<<12)|(imm&0xFFF))
    def str(self,rd,rn,imm): self.add(0xE5800000|(rn<<16)|(rd<<12)|(imm&0xFFF))
    def strh(self,rd,rn,imm):
        v=imm&0xFFF; lo=v&0xF; hi=(v>>4)&0xF
        self.add(0xE1C000B0|(rn<<16)|(hi<<8)|(rd<<12)|lo)
    def strp(self,rd,rn,imm): self.add(0xE4800000|(rn<<16)|(rd<<12)|(imm&0xFFF))
    def subs(self,rd,rn,imm): self.add(0xE2500000|(rn<<16)|(rd<<12)|(imm&0xFF))
    def adr(self,rd,label): self.ins.append(("adr",rd,label))
    def b(self,label,cond=0xE): self.ins.append(("b",cond,label))
    def bne(self,label): self.b(label,cond=0x1)
    def pool(self,*ws):
        for w in ws: self.ins.append(("poolw",w,None))
    def resolve(self,base):
        out=bytearray(); n=len(self.ins); addr=[base+i*4 for i in range(n)]
        for i,(k,a,b1) in enumerate(self.ins):
            if k=="raw": out+=W(a)
            elif k=="poolw": out+=W(a)
            elif k=="adr":
                t=addr[self.labels[b1]]; off=t-(addr[i]+8)
                if off<0 or off>4095: raise SystemExit(f"adr {off}")
                out+=W(0xE28F0000|(a<<12)|off)
            elif k=="b":
                t=addr[self.labels[b1]]; rel=(t-(addr[i]+8))//4
                out+=W(((a&0xF)<<28)|(0xA<<24)|(rel&0xFFFFFF))
        return bytes(out)

def make_rom(code, out):
    src=open("/workspace/mgba/cinema/gba/blend/disabled-bg-semitrans-blend/test.gba","rb").read()
    r=bytearray(0x40000); r[0:0x100]=src[0:0x100]
    r[0xC:0x10]=struct.pack("<I",0x00000100)
    r[0x18:0x1C]=struct.pack("<I",0xE25EF004)   # SUBS pc,lr,#4 return from IRQ
    r[0:4]=struct.pack('<I',0xEA000000|((0x08000100-0x08000000-8)//4 & 0xFFFFFF))
    r[0x100:0x100+len(code)]=code
    inst=struct.pack('<I',0xEAFFFFFE)
    for a in range(0x100+len(code),0x40000,4): r[a:a+4]=inst
    open(out,"wb").write(bytes(r)); print("wrote",out,"code",len(code))

def m4(obj, out, bldct=0x02C4, blda=0):
    a=Asm()
    a.L('start')
    a.adr(2,'pool')
    a.ldr(0,2,0);  a.ldr(1,2,4); a.str(1,0,0)
    a.ldr(1,2,8);  a.str(1,0,4)
    a.ldr(1,2,12); a.str(1,0,0x50)
    a.ldr(1,2,100); a.str(1,0,0x202)   # IE=0
    a.ldr(1,2,100); a.str(1,0,0x208)   # IME=0
    a.ldr(0,2,16); a.ldr(1,2,20); a.ldr(4,2,24)
    a.L('fl')
    a.strp(1,0,4); a.subs(4,4,1); a.bne('fl')
    a.ldr(0,2,28); a.ldr(1,2,32); a.ldr(4,2,36)
    a.L('fr')
    a.strp(1,0,4); a.subs(4,4,1); a.bne('fr')
    if obj:
        a.ldr(0,2,40); a.ldr(1,2,44); a.str(1,0,0)
        a.ldr(1,2,48); a.str(1,0,4)
        a.ldr(0,2,52); a.ldr(1,2,56); a.ldr(4,2,60)
        a.L('tile')
        a.strp(1,0,4); a.subs(4,4,1); a.bne('tile')
        a.ldr(0,2,64); a.ldr(1,2,68); a.str(1,0,4)
        a.ldr(1,2,72); a.str(1,0,8)
        a.ldr(1,2,76); a.str(1,0,0x202)
    a.ldr(3,2,0)
    a.ldr(1,2,80);  a.strh(1,3,0x00)   # DISPCNT
    a.ldr(1,2,4);   a.strh(1,3,0x04)   # DISPSTAT
    a.ldr(1,2,84);  a.strh(1,3,0x50)   # BLDCNT
    a.ldr(1,2,88);  a.strh(1,3,0x52)   # BLDALPHA
    a.ldr(1,2,92);  a.strh(1,3,0x54)   # BLDY
    a.L('loop')
    a.b('loop')
    a.L('pool')
    if obj:
        a.pool(0x04000000, 0x0080, 0, 0,
               0x06000000, 0x03030303, 0x12C0,
               0x06004B00, 0x04040404, 0x12C0,
               0x07000000, (60 | (100<<16)), 0x200,
               0x06014000, 0x01010101, 16,
               0x05000000, 0x001F0000, 0x000003E0, 0x001F0000,
               0x1404, bldct, blda, 0x000A, 0)
    else:
        a.pool(0x04000000, 0x0080, 0, 0,
               0x06000000, 0x03030303, 0x12C0,
               0x06004B00, 0x04040404, 0x12C0,
               0, 0, 0,
               0, 0, 0,
               0, 0, 0, 0,
               0x1404, 0x02C4, 0, 0x000A, 0)
    make_rom(a.resolve(0x08000100), out)
m4(True, "/workspace/BA/tmp_t2_m4.gba")
m4(False, "/workspace/BA/tmp_t2_m4nobj.gba")
m4(True, "/workspace/BA/tmp_t2_noblend.gba", bldct=0x0000)
m4(True, "/workspace/BA/tmp_t2_alpha.gba", bldct=0x0044, blda=0x0A0A)
m4(True, "/workspace/BA/tmp_t2_objsemi.gba", bldct=0x0040, blda=0x0888)
m4(False, "/workspace/BA/tmp_t2_alpha_nobj.gba", bldct=0x0044, blda=0x0A0A)

