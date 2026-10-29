#!/usr/bin/env python3
"""Build the PS4 controller-buttons system app.

It must be signed with the platform certificate of the Android image (the
public AOSP test key for this build) so it can inject the remote keys.
"""
from pathlib import Path
import argparse,subprocess,zipfile
p=argparse.ArgumentParser();p.add_argument('--android-jar',type=Path,required=True);p.add_argument('--tools',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--key',type=Path,required=True,help='platform.pk8');p.add_argument('--cert',type=Path,required=True,help='platform.x509.pem');a=p.parse_args()
s=Path(__file__).resolve().parent;b=a.build.resolve();(b/'classes').mkdir(parents=True,exist_ok=True);(b/'dex').mkdir(exist_ok=True)
def run(cmd):subprocess.run([str(x) for x in cmd],check=True)
run(['javac','--release','8','-Xlint:-options','-cp',a.android_jar,'-d',b/'classes',*sorted((s/'src').rglob('*.java'))])
run([a.tools/'d8','--min-api','33','--lib',a.android_jar,'--output',b/'dex',*sorted((b/'classes').rglob('*.class'))])
run([a.tools/'aapt2','compile','--dir',s/'res','-o',b/'res.zip'])
run([a.tools/'aapt2','link','-I',a.android_jar,'--manifest',s/'AndroidManifest.xml','-o',b/'unsigned.apk',b/'res.zip'])
with zipfile.ZipFile(b/'unsigned.apk','a',zipfile.ZIP_DEFLATED) as z:z.write(b/'dex/classes.dex','classes.dex')
run([a.tools/'zipalign','-f','4',b/'unsigned.apk',b/'aligned.apk'])
run([a.tools/'apksigner','sign','--key',a.key,'--cert',a.cert,'--out',b/'AtvController.apk',b/'aligned.apk'])
run([a.tools/'apksigner','verify','--print-certs',b/'AtvController.apk'])
print(b/'AtvController.apk')
