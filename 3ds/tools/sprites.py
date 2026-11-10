import sys, json, base64, os
from playwright.sync_api import sync_playwright
SRC='file:///home/claude/lazarus/web/t_sprites.html'
OUT='/home/claude/sprites'
types=sys.argv[1].split(',')
levels=[int(x) for x in sys.argv[2].split(',')]
JS='''([type,lv])=>{
  var c=document.createElement('canvas'); c.width=700; c.height=700;
  var g=c.getContext('2d'); var H=window.__HK; var old=H.ctx; H.ctx=g;
  var o=H.P(0,0,0); g.setTransform(1,0,0,1,350-o.x,420-o.y);
  var b={id:'t',type:type,lv:lv,x:0,y:0,st:0};
  try{ H.drawBuilding(b,0,false); }catch(e){ H.ctx=old; return {err:String(e)}; }
  H.ctx=old;
  var d=g.getImageData(0,0,700,700).data, x0=700,y0=700,x1=-1,y1=-1;
  for(var y=0;y<700;y++)for(var x=0;x<700;x++){ if(d[(y*700+x)*4+3]>8){ if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; } }
  if(x1<0) return {err:'empty'};
  var t=document.createElement('canvas'); t.width=x1-x0+1; t.height=y1-y0+1;
  t.getContext('2d').drawImage(c,-x0,-y0);
  return {png:t.toDataURL('image/png'),w:t.width,h:t.height,ax:350-x0,ay:420-y0};
}'''
with sync_playwright() as p:
    b=p.chromium.launch(executable_path='/opt/pw-browsers/chromium-1243/chrome-linux64/chrome',args=['--no-sandbox','--allow-file-access-from-files'])
    pg=b.new_page(viewport={'width':1200,'height':800})
    pg.add_init_script('window.__LZ_NOBOOT=1;')
    errs=[]
    pg.on('pageerror',lambda e:errs.append(str(e)))
    pg.goto(SRC); pg.wait_for_timeout(1500)
    meta={}
    for t in types:
        for lv in levels:
            r=pg.evaluate(JS,[t,lv])
            if 'err' in r: print(t,lv,'ERR',r['err']); continue
            open(f'{OUT}/{t}_{lv}.png','wb').write(base64.b64decode(r['png'].split(',')[1]))
            meta[f'{t}_{lv}']={'w':r['w'],'h':r['h'],'ax':r['ax'],'ay':r['ay']}
            print(t,lv,r['w'],r['h'])
    json.dump(meta,open(f'{OUT}/meta_{sys.argv[3] if len(sys.argv)>3 else "x"}.json','w'))
    print('pageerrors',errs[:3])
    b.close()
