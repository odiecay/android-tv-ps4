from pathlib import Path
import base64,subprocess
from PIL import Image
root=Path(__file__).resolve().parents[1];a=root/'artwork';branding=a
def url(path):return 'data:image/png;base64,'+base64.b64encode(Path(path).read_bytes()).decode()
logo=url(branding/'android-tv-official.png');ps4=url(branding/'ps4-official.png');welcome=url(a/'android-first-run.png')
base=f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1280" height="720" viewBox="0 0 1280 720">
<defs><linearGradient id="bg" x2="1" y2=".5"><stop stop-color="#071327"/><stop offset="1" stop-color="#123c73"/></linearGradient><linearGradient id="glass" x2=".9" y2="1"><stop stop-color="#273f60"/><stop offset="1" stop-color="#101b2c"/></linearGradient><radialGradient id="light"><stop stop-color="#337dd1" stop-opacity=".35"/><stop offset="1" stop-color="#337dd1" stop-opacity="0"/></radialGradient></defs>
<rect width="1280" height="720" fill="url(#bg)"/><ellipse cx="920" cy="310" rx="480" ry="310" fill="url(#light)"/>
<image x="64" y="48" width="270" height="46" xlink:href="{logo}"/><image x="1038" y="57" width="178" height="36" xlink:href="{ps4}"/>
<path d="M64 123H1216" stroke="#ffffff" stroke-opacity=".12"/>
<rect y="538" width="1280" height="182" fill="#080f1d"/><path d="M0 538H1280" stroke="#3a5274" stroke-opacity=".7"/>
'''
def text(y,content,size=22,color='#c7d6e9',weight=400):return f'<text x="64" y="{y}" font-family="Roboto" font-size="{size}" font-weight="{weight}" fill="{color}">{content}</text>'
slides=[]
slides.append(base+text(196,'ANDROID TV 16',16,'#7ebeff')+text(254,'Your apps.',42,'#ffffff',500)+text(306,'On your PS4.',42,'#ffffff',500)+text(364,'Set up Android TV and install')+text(398,'the apps you want to use.')+f'''<rect x="510" y="163" width="706" height="341" rx="8" fill="#02050a" stroke="#668ab7" stroke-opacity=".6"/>
<image x="518" y="171" width="690" height="325" preserveAspectRatio="xMidYMid meet" xlink:href="{welcome}"/>
</svg>''')
# Native SVG clipping of the original Sony photograph. The raster remains unchanged.
controller=f'''<defs><mask id="controller-silhouette" maskUnits="userSpaceOnUse" x="0" y="0" width="1200" height="675"><path fill="white" stroke="black" stroke-width="8" d="M338 114 Q337 101 382 99 Q416 98 429 112 L440 120 L773 120 L780 112 Q785 100 822 99 Q853 99 862 112 L871 124 Q918 174 941 270 Q966 363 965 445 Q962 520 921 540 Q872 568 830 526 Q809 499 779 399 L767 387 Q725 418 686 399 L668 385 L539 385 Q487 425 441 390 L430 397 Q406 478 385 519 Q354 563 308 547 Q261 537 248 491 Q233 455 245 371 L273 244 Q287 180 325 126 Z"/></mask></defs>
<g transform="translate(540 151) scale(0.5633333333)"><image width="1200" height="675" mask="url(#controller-silhouette)" xlink:href="{url(a/'dualshock4-jet-black.png')}"/></g>'''
slides.append(base+text(196,'USB CONTROLLER',16,'#7ebeff')+text(254,'Use your',42,'#ffffff',500)+text(306,'controller.',42,'#ffffff',500)+text(364,'Navigate with the D-pad.')+text(398,'Press the PS button to go Home.')+text(444,'Hold it to restart or power off. Circle goes back.',20)+controller+'</svg>')
console=f'''<image x="620" y="70" width="585" height="510" preserveAspectRatio="xMidYMid meet" xlink:href="{url(a/'ps4-console-official.png')}"/>'''
power=f'''<defs><clipPath id="power-card"><rect x="590" y="163" width="626" height="341" rx="8"/></clipPath></defs><g clip-path="url(#power-card)"><rect x="590" y="163" width="626" height="341" rx="8" fill="#212121" stroke="#668ab7" stroke-opacity=".6"/><svg x="600" y="167" width="606" height="333" viewBox="180 95 800 575"><image width="1920" height="1080" xlink:href="{url(a/'android-power-menu.png')}"/></svg></g><rect x="590" y="163" width="626" height="341" rx="8" fill="none" stroke="#668ab7" stroke-opacity=".6"/>'''
slides.append(base+text(196,'INTERNAL STORAGE',16,'#7ebeff')+text(254,'Boot from',42,'#ffffff',500)+text(306,'your PS4.',42,'#ffffff',500)+text(364,'Load GoldHEN and open Android TV.')+text(398,'No PC or network needed to boot.')+text(444,'Your apps and settings stay on the console.',20)+console+'</svg>')
slides.append(base+text(196,'RESTART &amp; SHUT DOWN',16,'#7ebeff')+text(254,'Back to',42,'#ffffff',500)+text(306,'PS4?',42,'#ffffff',500)+text(364,'Choose Restart in Android Settings.')+text(398,'The console returns to PS4.')+text(444,'Shut down powers off the console.')+power+'</svg>')
for i,svg in enumerate(slides):
 (a/f'slide-{i}.svg').write_text(svg)
 subprocess.run(['inkscape',str(a/f'slide-{i}.svg'),'--export-filename='+str(a/f'slide-{i}.png')],check=True,stdout=subprocess.DEVNULL)
print('Four installer preview slides rendered; build-release.py packages the pixels separately')
