import json, base64
from playwright.sync_api import sync_playwright
import subprocess
src=open('/home/claude/lazarus/web/index.html',encoding='utf-8').read()
a='function colShape(col,tw){'; assert src.count(a)==1
open('/home/claude/lazarus/web/t_sprites.html','w',encoding='utf-8').write(src.replace(a,'window.__HK={get ctx(){return ctx},set ctx(v){ctx=v},P:P,drawPlant:drawPlant};\n'+a))
JS='''([crop,pr])=>{
  var c=document.createElement('canvas'); c.width=300; c.height=300; var g=c.getContext('2d'); var H=window.__HK; var old=H.ctx; H.ctx=g;
  var o=H.P(0,0,0); g.setTransform(1,0,0,1,150-o.x,200-o.y);
  for(var r=0;r<4;r++)for(var cc=0;cc<4;cc++){ var pp=H.P(.25+cc*.5,.25+r*.5,3); H.drawPlant(crop,pp.x,pp.y,pr,0); }
  H.ctx=old;
  var d=g.getImageData(0,0,300,300).data,x0=300,y0=300,x1=-1,y1=-1;
  for(var y=0;y<300;y++)for(var x=0;x<300;x++){ if(d[(y*300+x)*4+3]>8){ if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y; } }
  var t=document.createElement('canvas'); t.width=x1-x0+1; t.height=y1-y0+1; t.getContext('2d').drawImage(c,-x0,-y0);
  return {png:t.toDataURL('image/png'),ax:150-x0,ay:200-y0};
}'''
meta={}
with sync_playwright() as p:
    b=p.chromium.launch(executable_path='/opt/pw-browsers/chromium-1243/chrome-linux64/chrome',args=['--no-sandbox','--allow-file-access-from-files'])
    pg=b.new_page(); pg.add_init_script('window.__LZ_NOBOOT=1;'); pg.goto('file:///home/claude/lazarus/web/t_sprites.html'); pg.wait_for_timeout(1500)
    for crop in ['wheat','corn','berries']:
        for i,pr in enumerate([0.25,0.6,1.0]):
            r=pg.evaluate(JS,[crop,pr])
            open(f'/home/claude/sprites/crop_{crop}_{i}.png','wb').write(base64.b64decode(r['png'].split(',')[1]))
            meta[f'crop_{crop}_{i}']={'ax':r['ax'],'ay':r['ay']}
    b.close()
json.dump(meta,open('/home/claude/sprites/meta_crops.json','w')); print(meta)
