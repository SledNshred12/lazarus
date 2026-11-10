import base64
from playwright.sync_api import sync_playwright
src=open('/home/claude/lazarus/web/index.html',encoding='utf-8').read()
a='function colShape(col,tw){'; assert src.count(a)==1
open('/home/claude/lazarus/web/t_sprites.html','w',encoding='utf-8').write(src.replace(a,'window.__HK={vicon:vicon};\n'+a))
COLS=['#7ED957','#F062D0','#45C4F5','#FFB02E','#B28DFF','#FF6B6B']
VECS=['contact','airborne','waterborne','vermin','spore']
JS='''async ([v,col])=>{
  var svg=window.__HK.vicon(v,64,col).replace('<svg ','<svg xmlns="http://www.w3.org/2000/svg" ');
  var img=new Image(); img.src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);
  await new Promise(function(r){ img.onload=r; img.onerror=r; });
  var c=document.createElement('canvas'); c.width=64; c.height=64; c.getContext('2d').drawImage(img,0,0,64,64);
  return c.toDataURL('image/png');
}'''
with sync_playwright() as p:
    b=p.chromium.launch(executable_path='/opt/pw-browsers/chromium-1243/chrome-linux64/chrome',args=['--no-sandbox','--allow-file-access-from-files'])
    pg=b.new_page(); pg.add_init_script('window.__LZ_NOBOOT=1;'); pg.goto('file:///home/claude/lazarus/web/t_sprites.html'); pg.wait_for_timeout(1500)
    for vi,v in enumerate(VECS):
        for ci,c in enumerate(COLS):
            d=pg.evaluate(JS,[v,c]); open(f'/home/claude/sprites/vi_{vi}_{ci}.png','wb').write(base64.b64decode(d.split(',')[1]))
    b.close()
print('ok')
