"""Replace entries in an APK and keep its original APK Signing Block.
System apps on read-only partitions have their certificates read without
digest verification, so the block still identifies the signer."""
import sys,zipfile,struct,subprocess,os
src,dst,*pairs=sys.argv[1:]
repl={pairs[i]:open(pairs[i+1],'rb').read() for i in range(0,len(pairs),2)}
d=open(src,'rb').read()
eocd=d.rfind(b'PK\x05\x06'); cd_off,=struct.unpack_from('<I',d,eocd+16)
magic=d[cd_off-16:cd_off]; assert magic==b'APK Sig Block 42', 'no signing block'
bsz,=struct.unpack_from('<Q',d,cd_off-24); block=d[cd_off-bsz-8:cd_off]
tmp=dst+'.unaligned'
with zipfile.ZipFile(src) as zi, zipfile.ZipFile(tmp,'w') as zo:
    for info in zi.infolist():
        data=repl.pop(info.filename,None) or zi.read(info.filename)
        zo.writestr(info,data,compress_type=info.compress_type)
assert not repl, repl
subprocess.run([os.path.expanduser('~/Android/Sdk/build-tools/34.0.0/zipalign'),'-f','-p','4',tmp,dst],check=True); os.remove(tmp)
z=open(dst,'rb').read(); e=z.rfind(b'PK\x05\x06'); co,=struct.unpack_from('<I',z,e+16)
out=bytearray(z[:co]+block+z[co:]); e2=len(z[:co])+len(block)+(e-co)
struct.pack_into('<I',out,e2+16,co+len(block))
open(dst,'wb').write(out); print('ok',dst,len(out))
