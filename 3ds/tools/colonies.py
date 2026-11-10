import json, base64, re, os
from playwright.sync_api import sync_playwright
SRCP='/home/claude/lazarus/web/index.html'
src=open(SRCP,encoding='utf-8').read()
i0=src.index('function drawCol3D(){'); i1=src.index('/* ================= boss fights: The Citadel')
fn=src[i0:i1]
m1=fn.index('  var list=B.bld.slice()'); m2=fn.index('  colProps(true,time);')
head=fn[:m1]                       # background: sky, lot, fence, props, ground, roads
lst=fn[m1:m2]                      # building loop
bg_fn=head.replace('function drawCol3D(){','function __drawBG(){')+'\n}\n'
bld_fn='function __drawBLD(){\n  var time=gtime;\n'+lst+'\n}\n'
a='function colShape(col,tw){'; assert src.count(a)==1
hook=bg_fn+bld_fn+'window.__HK={get B(){return B},set B(v){B=v},get ctx(){return ctx},set ctx(v){ctx=v},genColony:genColony,colShape:colShape,startOutbreak:startOutbreak,BP:BP,colIsoOn:colIsoOn,colIsoOff:colIsoOff,drawBG:__drawBG,drawBLD:__drawBLD,EK:EK,W:W,H:H};\n'
open('/home/claude/lazarus/web/t_colonies.html','w',encoding='utf-8').write(src.replace(a,hook+a))
PLAN=[('farm','plain'),('med','desert'),('farm','swamp'),('fort','plain'),('med','ruins'),('river','plain'),('fort','desert'),('med','swamp'),('farm','ruins'),('fort','ruins')]
JS='''async ([tier,kind,biome,seed])=>{
  var H=window.__HK, tw={seed:seed,tier:tier,x:3,y:3,id:'t'+tier,ck:kind,cb:biome};
  var col=H.genColony(tier,seed,null); col.seed=seed; col.name='Colony '+tier; H.colShape(col,tw);
  H.startOutbreak(col);
  var B=H.B; var out={name:col.name,tier:tier,kind:kind,biome:biome,lay:B.lay||col.lay,walled:!!B.walled,river:!!B.river,loot:col.loot,bld:[]};
  function cv(){ var c=document.createElement('canvas'); c.width=480; c.height=400; return c; }
  var c=cv(), g=c.getContext('2d'), old=H.ctx; H.ctx=g; H.colIsoOn(); H.drawBG(); H.colIsoOff();
  out.bg=c.toDataURL('image/png');
  var all=B.bld.slice();
  for(var i=0;i<all.length;i++){
    var b=all[i], sp=[];
    for(var st=0;st<2;st++){
      var cc=cv(), gg=cc.getContext('2d'); H.ctx=gg; B.bld=[b]; b.fallen=!!st;
      H.colIsoOn(); try{ H.drawBLD(); }catch(e){ H.colIsoOff(); out.err=String(e); }
      H.colIsoOff();
      var d=gg.getImageData(0,0,480,400).data,x0=480,y0=400,x1=-1,y1=-1;
      for(var y=0;y<400;y++)for(var x=0;x<480;x++){ if(d[(y*480+x)*4+3]>8){ if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y; } }
      if(x1<0){ sp.push(null); continue; }
      var t=document.createElement('canvas'); t.width=x1-x0+1; t.height=y1-y0+1; t.getContext('2d').drawImage(cc,-x0,-y0);
      sp.push({png:t.toDataURL('image/png'),x:x0,y:y0,w:t.width,h:t.height});
    }
    b.fallen=false;
    out.bld.push({k:b.k,x:b.x,y:b.y,r:b.r,hp:b.hp,max:b.max,den:b.den,loot:b.loot,wall:b.wall?1:0,gate:b.gate?1:0,sp:sp});
  }
  H.ctx=old; B.bld=all;
  return out;
}'''
os.makedirs('/home/claude/sprites/col',exist_ok=True)
meta=[]
with sync_playwright() as p:
    b=p.chromium.launch(executable_path='/opt/pw-browsers/chromium-1243/chrome-linux64/chrome',args=['--no-sandbox','--allow-file-access-from-files'])
    pg=b.new_page(viewport={'width':900,'height':700}); pg.add_init_script('window.__LZ_NOBOOT=1;')
    errs=[]; pg.on('pageerror',lambda e:errs.append(str(e)))
    pg.goto('file:///home/claude/lazarus/web/t_colonies.html'); pg.wait_for_timeout(1500)
    for t,(kind,biome) in enumerate(PLAN):
        r=pg.evaluate(JS,[t,kind,biome,1000+t*77])
        open(f'/home/claude/sprites/col/bg_{t}.png','wb').write(base64.b64decode(r['bg'].split(',')[1]))
        for j,bd in enumerate(r['bld']):
            for st in range(2):
                s=bd['sp'][st]
                if s: open(f'/home/claude/sprites/col/b_{t}_{j}_{st}.png','wb').write(base64.b64decode(s['png'].split(',')[1])); s['png']=None
        r['bg']=None; meta.append(r)
        print(t,kind,biome,len(r['bld']),r.get('err'))
    print('pageerrors',errs[:3]); b.close()
json.dump(meta,open('/home/claude/sprites/col/meta.json','w'))
