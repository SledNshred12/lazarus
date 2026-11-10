import re, json
from PIL import Image
SRC='/home/claude/lazarus/web/index.html'
OUT='/home/claude/lazarus/3ds'
S=open(SRC,encoding='utf-8').read()
TYPES=['hall','farm','trade','incubator','lab','clinic','scout','tower','shelter','dronebay','kennel']
def obj(name):
    i=S.index('var '+name+'=')+len('var '+name+'=')
    j=S.index(';',i) if name in('COSTS','LIM') else S.index('\n};',i)+2
    t=S[i:j]
    return t
def parse_arrays(t):
    return {m.group(1):[int(x) for x in m.group(2).split(',')] for m in re.finditer(r'(\w+):\[([\d,]+)\]',t)}
COSTS=parse_arrays(obj('COSTS')); LIM=parse_arrays(obj('LIM'))
for k,a in COSTS.items():
    while len(a)<19: a.append(int(a[-1]*1.3/50+0.5)*50)
bt=obj('BT')
BT={}
for t in TYPES:
    m=re.search(t+r":\{name:'([^']+)',w:(\d),h:(\d),hz:(\d+),max:(\d+)(?:,base:(\d+))?",bt)
    BT[t]=dict(name=m.group(1),w=int(m.group(2)),h=int(m.group(3)),base=int(m.group(6) or 0))
SC=0.6
meta=json.load(open('/home/claude/sprites/meta_all.json'))
anch={}
for t in TYPES:
    lines=[]
    for lv in range(1,21):
        im=Image.open(f'/home/claude/sprites/{t}_{lv}.png').convert('RGBA')
        w,h=im.size; nw,nh=max(1,round(w*SC)),max(1,round(h*SC))
        im=im.resize((nw,nh),Image.LANCZOS)
        im.save(f'{OUT}/gfx/{t}_{lv}.png')
        m=meta[f'{t}_{lv}']
        anch[(t,lv)]=(round(m['ax']*SC),round(m['ay']*SC),nw,nh)
        lines.append(f'{t}_{lv}.png')
    open(f'{OUT}/gfx/{t}.t3s','w').write('--atlas -f rgba -z auto\n'+'\n'.join(lines)+'\n')
h=['/* generated from the game source by tools/gen3ds.py */','#pragma once',f'#define NTYPES {len(TYPES)}','typedef struct { const char *id,*name; int w,h,base; } BType;','static const BType BTYPES[NTYPES]={']
for t in TYPES: h.append(f'  {{"{t}","{BT[t]["name"]}",{BT[t]["w"]},{BT[t]["h"]},{BT[t]["base"]}}},')
h.append('};')
h.append('static const int COSTS[NTYPES][19]={')
for t in TYPES:
    c=COSTS.get(t,[0]*19); c=(c+[0]*19)[:19]; h.append('  {'+','.join(map(str,c))+'},')
h.append('};')
h.append('static const int LIMS[NTYPES][10]={')
for t in TYPES:
    c=LIM.get(t,[0]*10); c=(c+[0]*10)[:10]; h.append('  {'+','.join(map(str,c))+'},')
h.append('};')
h.append('static const short ANCH[NTYPES][20][4]={')
for t in TYPES:
    h.append('  {'+','.join('{%d,%d,%d,%d}'%anch[(t,lv)] for lv in range(1,21))+'},')
h.append('};')
open(f'{OUT}/source/data.h','w').write('\n'.join(h)+'\n')
print(BT); print({t:COSTS.get(t) for t in TYPES}); print({t:LIM.get(t) for t in TYPES})

# crops
cm=json.load(open('/home/claude/sprites/meta_crops.json'))
lines=[]; ca=[]
for c in ['wheat','corn','berries']:
    row=[]
    for i in range(3):
        im=Image.open(f'/home/claude/sprites/crop_{c}_{i}.png').convert('RGBA')
        nw,nh=max(1,round(im.width*SC)),max(1,round(im.height*SC)); im=im.resize((nw,nh),Image.LANCZOS)
        im.save(f'{OUT}/gfx/crop_{c}_{i}.png'); lines.append(f'crop_{c}_{i}.png')
        m=cm[f'crop_{c}_{i}']; row.append('{%d,%d}'%(round(m['ax']*SC),round(m['ay']*SC)))
    ca.append('{'+','.join(row)+'}')
open(f'{OUT}/gfx/crops.t3s','w').write('--atlas -f rgba -z auto\n'+'\n'.join(lines)+'\n')
h2=['#define NCROPS 3','typedef struct { const char *name; int sec, food, hall, seed; } Crop;','static const Crop CROPS[NCROPS]={{"Wheat",45,9,1,3},{"Corn",180,45,1,15},{"Berries",600,165,2,50}};',
 'static const short CROPANCH[NCROPS][3][2]={'+','.join(ca)+'};']
open(f'{OUT}/source/data.h','a').write('\n'.join(h2)+'\n')

# virus icons: 5 vectors x 6 colours
lines=[]
for v in range(5):
    for c in range(6):
        im=Image.open(f'/home/claude/sprites/vi_{v}_{c}.png').convert('RGBA'); im.save(f'{OUT}/gfx/vi_{v}_{c}.png'); lines.append(f'vi_{v}_{c}.png')
open(f'{OUT}/gfx/vicons.t3s','w').write('--atlas -f rgba -z auto\n'+'\n'.join(lines)+'\n')
h3=["typedef struct { const char *name; int unlock, radius; float trans, detect; } Vec;",
 'static const Vec VECS[5]={{"Contact",0,80,1.0f,0.8f},{"Airborne",60,135,0.9f,1.35f},{"Waterborne",210,100,1.0f,1.0f},{"Vermin",260,70,0.9f,0.9f},{"Spore burst",400,90,1.1f,1.15f}};',
 "typedef struct { const char *name; int unlock; } Trait;",
 'static const Trait TRAITS[5]={{"Spread",0},{"Lethality",0},{"Resilience",30},{"Stealth",90},{"Mutation",160}};',
 'static const char *PRE[12]={"Crimson","Pale","Gray","Velvet","Hollow","Amber","Ashen","Cobalt","Scarlet","Jade","Violet","Ember"};',
 'static const char *SUF[10]={"Fever","Blight","Wilt","Rot","Shiver","Cough","Pox","Bloom","Creep","Rash"};',
 'static const unsigned STRAIN_RGB[6]={0x7ED957,0xF062D0,0x45C4F5,0xFFB02E,0xB28DFF,0xFF6B6B};']
open(f'{OUT}/source/data.h','a').write('\n'.join(h3)+'\n')
