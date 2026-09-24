from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math
import shutil

root = Path(__file__).resolve().parent
assets = root / 'figures'
assets.mkdir(exist_ok=True)
im = Image.new('RGB', (1800, 1300), 'white')
d = ImageDraw.Draw(im)
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 27)
bold = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 30)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 23)
ink = '#17374b'

def box(x,y,w,h,title,body,fill='#eaf2f8'):
    d.rounded_rectangle((x,y,x+w,y+h),radius=14,fill=fill,outline=ink,width=3)
    lines=[(title,bold)]+[(t,font) for t in body.split('\n')]
    start=y+(h-len(lines)*36)/2
    for n,(t,f) in enumerate(lines):
        d.text((x+w/2,start+n*36),t,font=f,fill=ink,anchor='mt')

def arrow(points,label=None,at=None,dashed=False):
    for a,b in zip(points,points[1:]):
        if dashed:
            dist=math.dist(a,b)
            for i in range(0,int(dist),20):
                j=min(i+11,dist)
                d.line((a[0]+(b[0]-a[0])*i/dist,a[1]+(b[1]-a[1])*i/dist,
                        a[0]+(b[0]-a[0])*j/dist,a[1]+(b[1]-a[1])*j/dist),fill=ink,width=3)
        else:
            d.line((a,b),fill=ink,width=4)
    a,b=points[-2:]; angle=math.atan2(b[1]-a[1],b[0]-a[0])
    d.polygon([b,(b[0]-17*math.cos(angle-.45),b[1]-17*math.sin(angle-.45)),
               (b[0]-17*math.cos(angle+.45),b[1]-17*math.sin(angle+.45))],fill=ink)
    if label:
        d.text(at,label,font=small,fill=ink,anchor='mm')

d.text((65,35),'CONTROL AND FREQUENCY STORAGE',font=bold,fill=ink)
box(65,100,340,135,'Slide potentiometer','ADC0 / GPIO 26\n0 to 10,000 Hz')
box(65,330,340,135,'Matrix keypad','GPIO 9-15\nScan + debounce')
box(580,330,430,135,'Mode and key control','Live tone / record / compose\nMute and playback selection')
box(1190,100,530,135,'Frequency recordings','9 sounds x 1,000 values\nRecord every 10 ms')
box(1190,330,530,135,'Playback and composition','Stored-key sequence + previews\nFrequency update every 1 ms')
box(580,570,430,125,'Frequency selection','Live ADC or stored frequency\nConvert Hz to phase increment')
box(1190,570,530,125,'Amplitude ramp','tone_enabled -> amplitude\nBounded gain: 0 to 2047')
arrow([(405,167),(1190,167)],'Mapped frequency samples',(800,142))
arrow([(405,210),(450,210),(450,630),(580,630)],'Live frequency',(320,600))
arrow([(405,397),(580,397)],'Key events',(490,370))
arrow([(1010,367),(1190,367)],'Play / sequence',(1100,335))
arrow([(1010,420),(1090,420),(1090,210),(1190,210)])
d.text((1095,270),'Record',font=small,fill=ink)
arrow([(1455,235),(1455,330)],'Stored Hz',(1535,279))
arrow([(1250,465),(1250,515),(900,515),(900,570)],'Playback frequency',(1070,490))
arrow([(1010,445),(1120,445),(1120,630),(1190,630)],dashed=True)
arrow([(790,465),(790,570)],'Select',(835,520),dashed=True)

d.rounded_rectangle((45,755,1745,1100),radius=18,outline='#738a9b',width=3)
d.text((65,776),'SYNTHESIS ISR: NOMINAL 50 kHz',font=bold,fill=ink)
box(70,860,270,150,'Timer alarm','20 us nominal\nper DAC sample')
box(420,860,300,150,'DDS phase','32-bit accumulator\nTop 8 bits index')
box(805,860,285,150,'Sine lookup','256 entries\nSigned sample')
box(1175,860,300,150,'Scale + offset','Apply amplitude\nAdd midpoint 2048')
box(1550,860,170,150,'SPI0','16-bit\nDAC word')
arrow([(790,695),(790,730),(570,730),(570,860)])
arrow([(1455,695),(1455,735),(1325,735),(1325,860)])
arrow([(340,935),(420,935)])
arrow([(720,935),(805,935)])
arrow([(1090,935),(1175,935)])
arrow([(1475,935),(1550,935)])
box(1330,1160,390,105,'DAC channel A','Analog audio output',fill='#edf6ee')
arrow([(1635,1010),(1635,1160)])
box(70,1160,790,105,'GPIO 2 timing probe','High at ISR entry; low at exit -> oscilloscope',fill='#f5f2e9')
arrow([(205,1010),(205,1160)],dashed=True)
d.text((925,1210),'Dashed arrows: control / timing',font=small,fill=ink)
im.save(assets/'signal_flow.png')
shutil.copyfile(Path('C:/Users/Kaelem/AppData/Local/Temp/codex-clipboard-9116b10f-9a78-4c2c-aaba-dbe207d6440a.png'),assets/'debounce_state_machine.png')

p=root/'Lab_1_Report.md'
s=p.read_text(encoding='utf-8')
start=s.index('**Figure 1.') if '**Figure 1.' in s else s.index('![Functional signal flow')
end=s.index('| Pico signal',start)
s=s[:start]+'![Functional signal flow derived from the saved implementation. Dashed arrows indicate control or timing signals; this is not an as-built wiring schematic.](figures/signal_flow.png)\n\n'+s[end:]
start=s.index('**Figure 2.') if '**Figure 2.' in s else s.index('![Four-state keypad')
end=s.index('Because the keypad',start)
s=s[:start]+'![Four-state keypad debounce diagram supplied by the group. The action on confirmed press matches the saved implementation; recording ends on confirmed release.](figures/debounce_state_machine.png)\n\n'+s[end:]
p.write_text(s,encoding='utf-8')
print('Created both report figures and replaced text diagrams.')
