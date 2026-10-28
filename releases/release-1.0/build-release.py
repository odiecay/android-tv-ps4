#!/usr/bin/env python3
"""Build the 32 GiB first-release candidate using explicit, operator-supplied toolchain inputs."""
from pathlib import Path
import argparse,os,subprocess,shutil,hashlib,json
p=argparse.ArgumentParser()
for arg in ['sdk','template','objects','runtime','assets','build','output','probe-build','probe-linker']:p.add_argument('--'+arg,type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent;b=a.build.resolve();b.mkdir(parents=True,exist_ok=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk.resolve();tools=sdk/'bin/linux';src=root/'source';cid='IV0000-ATVX16001_00-PS4ANDROIDTV1600'
source_hashes={x.name:hashlib.file_digest(x.open('rb'),'sha256').hexdigest() for x in sorted(src.iterdir()) if x.is_file()}
def digest(path):
 with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
runtime_inputs=json.loads((root/'runtime-inputs.json').read_text())
app_manifest=json.loads((root/'bundled-apps.json').read_text())
app_image=json.loads((root/'bundled-apps-image.json').read_text())
assert digest(root/'bundled-apps.json')==runtime_inputs['bundled_apps_sha256']==app_image['manifest_sha256']
assert digest(a.assets/'initramfs.cpio.xz')==runtime_inputs['initramfs_sha256']
assert digest(a.assets/'libgallium-baseline.so')==runtime_inputs['mesa_sha256']
assert digest(a.assets/'mt7668-wifi.tar.gz')==runtime_inputs['mt7668_wifi_sha256']
assert digest(a.assets/'bundled-apps.sfs')==app_image['sha256']
assert (a.assets/'bundled-apps.sfs').stat().st_size==app_image['bytes']==runtime_inputs['bundled_app_bytes']
expected_assets={'data-gib.txt','bzImage','mt7668-wifi.tar.gz','initramfs.cpio.xz','bootargs.txt','vram.txt','linux.elf','system.sfs','libgallium-baseline.so','ndk-translation.tar.gz','bundled-apps.sfs'}
assert {x.name for x in a.assets.iterdir()}==expected_assets,'Unexpected package assets'
metadata_hashes={name:digest(root/name) for name in ['image-metadata.h','bundled-apps.h','bundled-apps.json','bundled-apps-image.json','runtime-inputs.json']}
assert (a.assets/'data-gib.txt').read_text()=='32\n'
for directory in ['sce_sys','sce_module']:shutil.copytree(a.template/directory,b/directory,dirs_exist_ok=True)
shutil.copy2(root/'artwork/icon0.png',b/'sce_sys/icon0.png');shutil.copy2(root/'artwork/pic1.png',b/'sce_sys/pic1.png')
shutil.copy2(root/'image-metadata.h',b/'image-metadata.h')
shutil.copy2(root/'bundled-apps.h',b/'bundled-apps.h')
if (b/'assets').resolve()!=a.assets.resolve():
 shutil.copytree(a.assets,b/'assets',dirs_exist_ok=True)
assert {x.name for x in (b/'assets').iterdir()}==expected_assets,'Stale package build assets'
common=['clang-18','--target=x86_64-pc-freebsd12-elf','-O2','-fPIC','-funwind-tables','-mno-red-zone','-c','-DEDITION_GIB=32','-DRAW_IMAGES','-isysroot',str(sdk),'-isystem',str(sdk/'include'),'-I'+str(b),'-I'+str(src),'-Wall','-Wextra','-Werror','-Wno-unused-function']
subprocess.run(['python3',str(root/'embed-payload.py'),str(a.assets/'linux.elf'),str(b/'local-payload-image.h')],check=True)
subprocess.run(['python3',str(root/'build-kexec.py'),str(b)],check=True)
import zlib
boot_inputs=[]
# One kernel for every southbridge.
for prefix,name in [('KERNEL','bzImage'),('INITRD','initramfs.cpio.xz')]:
 data=(a.assets/name).read_bytes()
 boot_inputs += [f'#define ATV_{prefix}_BYTES {len(data)}ULL',f'#define ATV_{prefix}_CRC32 0x{zlib.crc32(data):08x}U']
# One ID for the whole asset set. The launcher trusts a file it verified earlier
# only while this ID and the installed file are unchanged.
# Each file also gets its own ID, so an update only re-checks files that changed.
asset_set=hashlib.sha256();asset_ids=[]
for x in sorted(a.assets.iterdir()):
 d=hashlib.file_digest(x.open('rb'),'sha256')
 asset_set.update(x.name.encode()+b'\0'+d.digest());asset_ids.append(f'{{"{x.name}","{d.hexdigest()[:32]}"}}')
boot_inputs.append(f'#define ATV_ASSET_SET_ID "{asset_set.hexdigest()[:32]}"')
boot_inputs.append('#define ATV_ASSET_IDS {'+','.join(asset_ids)+'}')
(b/'boot-inputs.h').write_text('\n'.join(boot_inputs)+'\n')
subprocess.run(['python3',str(root/'build-payload.py'),'--build',str(b),'--probe-build',str(a.probe_build),'--probe-linker',str(a.probe_linker)],check=True)
compiled={}
for name in ['install-prebuilt.c','image-copy.c','installer-ui.c','payload-send.c','boot-stage.c']:
 dest=b/(name+'.o');subprocess.run(common+[str(src/name),'-o',str(dest)],check=True);compiled[name]=str(dest)
# prepare-build.py compiles these helpers from pinned libjbc source and the
# local credential wrapper. Record the supplied object hashes in each build.
helper_hashes={};helpers=[]
for name in ['privileged.o','jailbreak.o','kernelrw.o']:
 f=a.objects/name;helper_hashes[name]=hashlib.file_digest(f.open('rb'),'sha256').hexdigest();helpers.append(str(f.resolve()))
def link(name,sources,libraries):
 subprocess.run(['ld.lld-18','-m','elf_x86_64','-pie','--script',str(sdk/'link.x'),'--eh-frame-hdr','-L'+str(sdk/'lib'),*[compiled[s] for s in sources],*helpers,str(sdk/'lib/crt1.o'),'-lc','-lkernel','-lSceSystemService','-lSceSysmodule',*libraries,'-o',str(b/name)],check=True)
link('launcher.elf',['install-prebuilt.c','image-copy.c','installer-ui.c','boot-stage.c','payload-send.c'],['-lSceVideoOut'])
boot_symbols=subprocess.check_output(['nm',str(b/'launcher.elf')],text=True)
assert 'ps4_launch_local_payload' not in boot_symbols
assert 'atv_send_payload' in boot_symbols
assert 'kexec_1352_image' not in boot_symbols
assert 'installer_slides' not in boot_symbols
assert (b/'launcher.elf').stat().st_size<2*1024*1024, 'Slideshow must not be embedded in the launcher'
# Package approved artwork as raw pixels. Read directly into VideoOut buffers;
# no decoded heap image or executable-resident slide array remains at boot.
from PIL import Image
(b/'artwork').mkdir(exist_ok=True)
artwork_hashes={}
for i in range(4):
 im=Image.open(root/f'artwork/slide-{i}.png').convert('RGB').convert('RGBA')
 assert im.size==(1280,720)
 data=im.tobytes('raw','BGRA');assert len(data)==1280*720*4
 name=f'slide-{i}.rgba';(b/'artwork'/name).write_bytes(data)
 artwork_hashes[name]=hashlib.sha256(data).hexdigest()
env=dict(os.environ,OO_PS4_TOOLCHAIN=str(sdk),DOTNET_SYSTEM_GLOBALIZATION_INVARIANT='1',LD_LIBRARY_PATH=str(a.runtime.resolve()))
def run(args):subprocess.run([str(x) for x in args],cwd=b,env=env,check=True)
run([tools/'create-fself','-in=launcher.elf','-out=launcher.oelf','--eboot','eboot.bin','--paid','0x3800000000000011'])
for key,value in [('CONTENT_ID',cid),('APP_VER','1.10'),('VERSION','1.10'),('TITLE','Android TV 16 Dev'),('TITLE_ID','ATVX16001')]:run([tools/'PkgTool.Core','sfo_setentry','sce_sys/param.sfo',key,'--type','Utf8','--maxsize','48' if key=='CONTENT_ID' else '128' if key=='TITLE' else '12' if key=='TITLE_ID' else '8','--value',value])
# Structural PKG validation does not enforce agreement with param.sfo.
import struct
sfo=(b/'sce_sys/param.sfo').read_bytes()
magic,version,keyoff,dataoff,count=struct.unpack_from('<5I',sfo)
fields={}
for i in range(count):
 key,fmt,length,maximum,offset=struct.unpack_from('<HHIII',sfo,20+16*i)
 name=sfo[keyoff+key:].split(b'\0',1)[0].decode()
 fields[name]=sfo[dataoff+offset:dataoff+offset+length].rstrip(b'\0')
assert fields['CONTENT_ID'].decode()==cid, 'PKG/SFO content ID mismatch'
assert fields['TITLE_ID'].decode()==cid.split('-')[1].split('_')[0], 'PKG/SFO title ID mismatch'
files=['eboot.bin','sce_sys/about/right.sprx','sce_sys/param.sfo','sce_sys/icon0.png','sce_sys/pic1.png','sce_module/libc.prx','sce_module/libSceFios2.prx']+['assets/'+x.name for x in sorted((b/'assets').iterdir())]
files += ['artwork/'+name for name in artwork_hashes]
run([tools/'create-gp4','-out','pkg.gp4','--content-id='+cid,'--files',' '.join(files)])
# create-gp4 ships a fixed directory template. Add every actual parent so new
# package assets are represented in the PFS directory tree as well as files.
import xml.etree.ElementTree as ET
tree=ET.parse(b/'pkg.gp4');dirs=tree.getroot().find('rootdir');assert dirs is not None
for filename in files:
 parent=dirs
 for part in Path(filename).parts[:-1]:
  child=next((x for x in parent.findall('dir') if x.get('targ_name')==part),None)
  if child is None:child=ET.SubElement(parent,'dir',{'targ_name':part})
  parent=child
tree.write(b/'pkg.gp4',encoding='utf-8',xml_declaration=True)
run([tools/'PkgTool.Core','pkg_build','pkg.gp4','.'])
f=b/(cid+'.pkg');v=subprocess.run([str(tools/'PkgTool.Core'),'pkg_validate',str(f)],cwd=b,env=env,text=True,capture_output=True,check=True);assert '[FAIL]' not in v.stdout and '[ERROR]' not in v.stdout
(out/'validation.txt').write_text(v.stdout)
name='Android-TV-for-PS4.pkg';sha=hashlib.file_digest(f.open('rb'),'sha256').hexdigest();dest=out/name;shutil.copyfile(f,dest);assert hashlib.file_digest(dest.open('rb'),'sha256').hexdigest()==sha
assert source_hashes=={x.name:hashlib.file_digest(x.open('rb'),'sha256').hexdigest() for x in sorted(src.iterdir()) if x.is_file()}, 'Source changed during build; do not publish artifact'
assert metadata_hashes=={name:digest(root/name) for name in metadata_hashes},'Release inputs changed during build'
manifest={'release':'1.0','edition_gib':32,'logical_data_bytes':34359738368,'file':name,'bytes':dest.stat().st_size,'sha256':sha,'content_id':cid,'package_validation':'passed','console_tested':False,'public_release_ready':False,'loader_build':json.loads((b/'kexec-build.json').read_text()),'helper_object_hashes':helper_hashes,'source_sha256':source_hashes,'delivery':'127.0.0.1:9090; GoldHEN BinLoader must be enabled; no external network','payload':json.loads((b/'payload-build.json').read_text()),'runtime_inputs':json.loads((root/'runtime-inputs.json').read_text()),'assets':{x.name:hashlib.file_digest(x.open('rb'),'sha256').hexdigest() for x in (b/'assets').iterdir()}}
manifest['artwork_sha256']=artwork_hashes
manifest['bundled_apps']=[{k:app[k] for k in ['label','package','version','abis','requires_translation','signer_sha256']} for app in app_manifest['apps']]
manifest['metadata_sha256']=metadata_hashes
manifest['launcher_elf_bytes']=(b/'launcher.elf').stat().st_size
(out/'release.json').write_text(json.dumps(manifest,indent=2)+'\n');(out/'SHA256SUMS').write_text(sha+'  '+name+'\n');print(json.dumps(manifest,indent=2),flush=True)
