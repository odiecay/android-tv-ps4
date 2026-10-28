#!/usr/bin/env python3
"""Build the Android TV for PS4 uninstaller PKG (source/uninstaller.c)."""
from pathlib import Path
import argparse,os,subprocess,shutil,hashlib
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser()
for arg in ['sdk','template','objects','runtime','build','output']:p.add_argument('--'+arg,type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent;src=root/'source'
b=a.build.resolve();shutil.rmtree(b,ignore_errors=True);b.mkdir(parents=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sdk=a.sdk.resolve();tools=sdk/'bin/linux'
cid='IV0000-ATVU00001_00-PS4ANDROIDTVUNIN'
for directory in ['sce_sys','sce_module']:shutil.copytree(a.template/directory,b/directory,dirs_exist_ok=True)
shutil.copy2(root/'artwork/icon0.png',b/'sce_sys/icon0.png');shutil.copy2(root/'artwork/pic1.png',b/'sce_sys/pic1.png')
subprocess.run(['clang-18','--target=x86_64-pc-freebsd12-elf','-O2','-fPIC','-funwind-tables','-mno-red-zone','-c','-isysroot',str(sdk),'-isystem',str(sdk/'include'),'-I'+str(src),
 '-Wall','-Wextra','-Werror','-Wno-unused-function',str(src/'uninstaller.c'),'-o',str(b/'uninstaller.o')],check=True)
helpers=[str((a.objects/n).resolve()) for n in ['privileged.o','jailbreak.o','kernelrw.o']]
subprocess.run(['ld.lld-18','-m','elf_x86_64','-pie','--script',str(sdk/'link.x'),'--eh-frame-hdr','-L'+str(sdk/'lib'),str(b/'uninstaller.o'),*helpers,str(sdk/'lib/crt1.o'),
 '-lc','-lkernel','-lSceSystemService','-lSceSysmodule','-lSceVideoOut','-lSceUserService','-lScePad','-o',str(b/'uninstaller.elf')],check=True)
env=dict(os.environ,OO_PS4_TOOLCHAIN=str(sdk),DOTNET_SYSTEM_GLOBALIZATION_INVARIANT='1',LD_LIBRARY_PATH=str(a.runtime.resolve()))
def run(args):subprocess.run([str(x) for x in args],cwd=b,env=env,check=True)
run([tools/'create-fself','-in=uninstaller.elf','-out=uninstaller.oelf','--eboot','eboot.bin','--paid','0x3800000000000011'])
for key,value in [('CONTENT_ID',cid),('APP_VER','1.01'),('VERSION','1.01'),('TITLE','Android TV for PS4 Uninstaller'),('TITLE_ID','ATVU00001')]:
 run([tools/'PkgTool.Core','sfo_setentry','sce_sys/param.sfo',key,'--type','Utf8','--maxsize','48' if key=='CONTENT_ID' else '128' if key=='TITLE' else '12' if key=='TITLE_ID' else '8','--value',value])
files=['eboot.bin','sce_sys/about/right.sprx','sce_sys/param.sfo','sce_sys/icon0.png','sce_sys/pic1.png','sce_module/libc.prx','sce_module/libSceFios2.prx']
run([tools/'create-gp4','-out','pkg.gp4','--content-id='+cid,'--files',' '.join(files)])
tree=ET.parse(b/'pkg.gp4');dirs=tree.getroot().find('rootdir');assert dirs is not None
for filename in files:
 parent=dirs
 for part in Path(filename).parts[:-1]:
  child=next((x for x in parent.findall('dir') if x.get('targ_name')==part),None)
  if child is None:child=ET.SubElement(parent,'dir',{'targ_name':part})
  parent=child
tree.write(b/'pkg.gp4',encoding='utf-8',xml_declaration=True)
run([tools/'PkgTool.Core','pkg_build','pkg.gp4','.'])
f=b/(cid+'.pkg');v=subprocess.run([str(tools/'PkgTool.Core'),'pkg_validate',str(f)],cwd=b,env=env,text=True,capture_output=True,check=True)
assert '[FAIL]' not in v.stdout and '[ERROR]' not in v.stdout,v.stdout
dest=out/'Android-TV-for-PS4-Uninstaller.pkg';shutil.copyfile(f,dest)
print(hashlib.file_digest(dest.open('rb'),'sha256').hexdigest(),dest)
