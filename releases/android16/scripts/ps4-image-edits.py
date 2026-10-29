#!/usr/bin/env python3
"""Apply the PS4 changes to the Lineage 23.2 TV system image (after add-gapps).

Usage: ps4-image-edits.py <system-gapps.img> <output.img>

Every change was first proven live on a CUH-1102A (run 15, see PLAN.md).
Patched files are made here from the image's own originals, so the output
depends only on the base image and image-inputs/.

  Mesa 26     libgallium_dri.so (x86_64, x86): addrlib traps off, Liverpool
              raster config (patch-mesa26.py)
  Allocator   YUV rows padded to 512 bytes, full-size CPU map (patch-minigbm-yuv.py)
  Codecs      ffmpeg H.264/HEVC/VP9 advertised up to 1080p60, AV1 up to 720p30,
              the PS4's real limits
  Settings    Device Preferences without Lineage's "Wi-Fi/BT firmware
              management" entry (axml-drop-pref.py, apk-swap-keep-sigblock.py)
  Bluetooth   HAL in the host network namespace (image-inputs/etc/bluetooth-ps4.rc)
  Launcher    Android TV Home + TV Recommendations; Catapult removed
  Apps        Gboard replaces LeanKeyboard on first boot; SetOrientation and
              FirmwareTool removed
  Identity    Google TV Streamer build properties, so Play lists TV apps;
              the name people see stays "PlayStation 4"
"""
import os,re,shutil,subprocess,sys,tempfile
HERE=os.path.dirname(os.path.abspath(__file__))
INPUTS=os.path.join(HERE,'..','image-inputs')
DEBUGFS='/usr/sbin/debugfs'

FINGERPRINT='google/kirkwood/kirkwood:14/UTTK.260317.003/15205007:user/release-keys'
IDENTITY={'brand':'google','device':'kirkwood','manufacturer':'Google',
          'model':'Google TV Streamer','name':'kirkwood'}
PROP_FILES=['/system/build.prop','/system/vendor/build.prop','/system/product/etc/build.prop',
            '/system/system_ext/etc/build.prop','/system/vendor/odm/etc/build.prop']

REMOVE=['/system/system_ext/priv-app/Catapult','/system/system_ext/app/FirmwareTool',
        '/system/app/SetOrientation','/system/etc/user_app/LeanKeyboard.apk',
        '/system/vendor/etc/init/bluetooth-service-default.rc',
        # Stale for the edited APK; ART compiles the new one itself.
        '/system/system_ext/priv-app/TvSettingsTwoPanel/oat',
        '/system/system_ext/priv-app/TvSettingsTwoPanel/TvSettingsTwoPanel.apk.prof']

def dfs(img,cmd,write=False):
    args=[DEBUGFS]+(['-w'] if write else [])+['-R',cmd,img]
    return subprocess.run(args,capture_output=True,text=True).stdout

def label(img,path):
    out=dfs(img,'ea_get '+path+' security.selinux')
    m=re.search(r'"(u:object_r:[^"\\]+)',out)
    if not m: raise SystemExit('no SELinux label on '+path)
    return m.group(1)

def entries(img,path):
    # ls -p: /inode/mode/uid/gid/name/size/
    out=[]
    for line in dfs(img,'ls -p '+path).splitlines():
        f=line.split('/')
        if len(f)>6 and f[5] not in ('.','..',''): out.append((f[5],f[2].startswith('4')))
    return out

def remove_cmds(img,path):
    stat=dfs(img,'stat '+path)
    if 'Type: directory' in stat:
        cmds=[]
        for name,isdir in entries(img,path):
            cmds+=remove_cmds(img,path+'/'+name)
        return cmds+['rmdir '+path]
    if 'Type: regular' in stat: return ['rm '+path]
    raise SystemExit('missing in image: '+path)

def main(src,out):
    work=tempfile.mkdtemp()
    try:
        subprocess.run(['cp','--sparse=always',src,out],check=True)
        subprocess.run(['/usr/sbin/e2fsck','-fy',out],check=True,stdout=subprocess.DEVNULL)
        cmds=[]
        def put(local,path,lab,mode='0100644'):
            lf=os.path.join(work,'label-'+str(len(cmds)))
            open(lf,'wb').write(lab.encode()+b'\0')
            cmds.extend(['write '+local+' '+path,'sif '+path+' mode '+mode,'sif '+path+' uid 0',
                         'sif '+path+' gid 0','ea_set -f '+lf+' '+path+' security.selinux'])
        def mkdir(path,lab='u:object_r:system_file:s0'):
            lf=os.path.join(work,'label-'+str(len(cmds)))
            open(lf,'wb').write(lab.encode()+b'\0')
            cmds.extend(['mkdir '+path,'sif '+path+' mode 040755','sif '+path+' uid 0',
                         'sif '+path+' gid 0','ea_set -f '+lf+' '+path+' security.selinux'])
        def replace(path,local):
            lab=label(src,path); cmds.append('rm '+path); put(local,path,lab)

        # Mesa
        for abi,lib in (('x86_64','/system/vendor/lib64/libgallium_dri.so'),('x86','/system/vendor/lib/libgallium_dri.so')):
            orig=os.path.join(work,'mesa-'+abi+'.orig'); new=os.path.join(work,'mesa-'+abi)
            dfs(src,'dump '+lib+' '+orig)
            subprocess.run([sys.executable,'-I',os.path.join(HERE,'patch-mesa26.py'),orig,new,abi],check=True)
            replace(lib,new)

        # Allocator: 512-byte YUV luma rows so radeonsi can import video frames
        gb='/system/vendor/lib64/libminigbm_gralloc_gbm_mesa.so'
        orig=os.path.join(work,'minigbm.orig'); new=os.path.join(work,'minigbm')
        dfs(src,'dump '+gb+' '+orig)
        subprocess.run([sys.executable,'-I',os.path.join(HERE,'patch-minigbm-yuv.py'),orig,new],check=True)
        replace(gb,new)

        # Video decoders: advertise what the PS4 CPU decodes in real time.
        # Lineage declares 4K30 and a block rate that allows 1440p60, so
        # YouTube started 1440p60, fell ~14 s behind and froze while it
        # dropped the backlog. 1080p60 played with no drops; 1440p stutters.
        mc='/system/vendor/etc/media_codecs_ffmpeg_c2.xml'
        local=os.path.join(work,'media_codecs_ffmpeg_c2.xml'); dfs(src,'dump '+mc+' '+local)
        text=open(local,encoding='utf8').read()
        for codec in ('h264','hevc','vp9'):
            m=re.search(r'<MediaCodec name="c2\.ffmpeg\.'+codec+r'\.decoder".*?</MediaCodec>',text,re.S)
            blk=m.group(0)
            new=re.sub(r'<Limit name="blocks-per-second"[^>]*/>','<Limit name="blocks-per-second" min="24" max="489600" />',blk)
            new=new.replace('<Limit name="performance-point-3840x2160" value="30" />','<Limit name="performance-point-1920x1080" value="60" />')
            if new==blk: raise SystemExit('codec limits not found for '+codec)
            text=text.replace(blk,new)
        # AV1: software decode at 1080p used all 8 cores (688%) and YouTube's x86
        # build picked it over VP9. Advertise AV1 only up to 720p30.
        m=re.search(r'<MediaCodec name="c2\.ffmpeg\.av1\.decoder".*?</MediaCodec>',text,re.S); blk=m.group(0)
        new=blk.replace('<Limit name="size" min="2x2" max="2048x2048" />','<Limit name="size" min="2x2" max="2048x2048" />\n            <Limit name="block-size" value="16x16" />\n            <Limit name="blocks-per-second" min="1" max="108000" />',1)
        if new==blk: raise SystemExit('ffmpeg av1 limits not found')
        text=text.replace(blk,new)
        open(local,'w',encoding='utf8').write(text)
        replace(mc,local)
        gv='/system/vendor/etc/media_codecs_google_c2_video.xml'
        local=os.path.join(work,'media_codecs_google_c2_video.xml'); dfs(src,'dump '+gv+' '+local)
        text=open(local,encoding='utf8').read()
        m=re.search(r'<MediaCodec name="c2\.android\.av1\.decoder".*?</MediaCodec>',text,re.S); blk=m.group(0)
        new=re.sub(r'<Limit name="blocks-per-second" range="1-\d+" />','<Limit name="blocks-per-second" range="1-108000" />',blk)
        if new==blk: raise SystemExit('android av1 limits not found')
        open(local,'w',encoding='utf8').write(text.replace(blk,new))
        replace(gv,local)

        # Settings without the firmware tool entry
        tvs='/system/system_ext/priv-app/TvSettingsTwoPanel/TvSettingsTwoPanel.apk'
        apk=os.path.join(work,'TvSettings.apk'); dfs(src,'dump '+tvs+' '+apk)
        swap=[]
        for x in ('device','device_two_panel','device_x'):
            a=os.path.join(work,x+'.xml'); b=os.path.join(work,x+'.new.xml')
            open(a,'wb').write(subprocess.run(['unzip','-p',apk,'res/xml/'+x+'.xml'],capture_output=True,check=True).stdout)
            subprocess.run([sys.executable,'-I',os.path.join(HERE,'axml-drop-pref.py'),a,b],check=True)
            swap+=['res/xml/'+x+'.xml',b]
        newapk=os.path.join(work,'TvSettings.new.apk')
        subprocess.run([sys.executable,'-I',os.path.join(HERE,'apk-swap-keep-sigblock.py'),apk,newapk]+swap,check=True)

        for p in REMOVE: cmds+=remove_cmds(src,p)
        replace(tvs,newapk)

        # Bluetooth
        put(os.path.join(INPUTS,'etc/bluetooth-ps4.rc'),'/system/vendor/etc/init/bluetooth-ps4.rc','u:object_r:vendor_configs_file:s0')

        # Launcher
        for app in ('AndroidTVHome','TVRecommendations'):
            d='/system/product/priv-app/'+app; mkdir(d)
            for f in sorted(os.listdir(os.path.join(INPUTS,app))):
                put(os.path.join(INPUTS,app,f),d+'/'+f,'u:object_r:system_file:s0')
        put(os.path.join(INPUTS,'etc/privapp-permissions-ps4-atvhome.xml'),
            '/system/product/etc/permissions/privapp-permissions-ps4-atvhome.xml','u:object_r:system_file:s0')

        # Gboard installs from user_app on first boot like the other bundled apps
        put(os.path.join(INPUTS,'Gboard.apk'),'/system/etc/user_app/Gboard.apk','u:object_r:system_file:s0')
        # Codey's app set for 16: Google's native x86 YouTube for Android TV
        # (7.05.301, same Google signer 21199d11.. as the Play build, which is
        # ARM64 and runs translated), TV Bro and BiglyBT. user_app installs on
        # first boot.
        for apk in ('YouTubeTV-x86.apk','TVBro.apk','BiglyBT.apk'):
            put(os.path.join(INPUTS,apk),'/system/etc/user_app/'+apk,'u:object_r:system_file:s0')

        # Identity
        for pf in PROP_FILES:
            local=os.path.join(work,pf.strip('/').replace('/','_')); dfs(src,'dump '+pf+' '+local)
            text=open(local,encoding='utf8').read()
            def sub(m):
                key=m.group(1)
                if key.endswith('build.fingerprint'): return key+'='+FINGERPRINT
                for k,v in IDENTITY.items():
                    if re.fullmatch(r'ro\.product\.(\w+\.)?'+k,key): return key+'='+v
                if key in ('ro.build.product','ro.build.device'): return key+'='+IDENTITY['device']
                if key=='ro.build.tags': return key+'=release-keys'
                return m.group(0)
            text=re.sub(r'^([\w.]+)=.*$',sub,text,flags=re.M)
            if pf=='/system/build.prop':
                text+='\n# PS4: the name people see\nbluetooth.device.default_name=PlayStation 4\n'
            open(local,'w',encoding='utf8').write(text)
            replace(pf,local)

        cf=os.path.join(work,'cmds'); open(cf,'w').write('\n'.join(cmds)+'\n')
        log=subprocess.run([DEBUGFS,'-w','-f',cf,out],capture_output=True,text=True)
        bad=[l for l in (log.stdout+log.stderr).splitlines() if re.search(r'error|not found|exists|cannot|Couldn',l,re.I)]
        if bad: raise SystemExit('debugfs problems:\n'+'\n'.join(bad))
        subprocess.run(['/usr/sbin/e2fsck','-fn',out],check=True)
        print('applied',len(cmds),'debugfs commands')
    finally:
        shutil.rmtree(work)

if __name__=='__main__':
    main(*sys.argv[1:3])
