// Compare the pinned UI with/without Screenshot's overlay: appearance must not
// move controls or change the shared installer's layout and typography.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const core=path.resolve('.connect-installer-source/installer-core');
const {chromium}=require(path.join(core,'node_modules/playwright'));
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 try{
 const page=await browser.newPage({viewport:{width:840,height:540}});
 await page.setContent(fs.readFileSync(path.join(core,'core/web/index.html'),'utf8').replace(/<script[\s\S]*?<\/script>/g,'').replace(/<link[^>]*>/g,''));
 await page.addStyleTag({content:fs.readFileSync(path.join(core,'core/web/style.css'),'utf8')});
 const overlay=fs.readFileSync('scripts/installer-appearance.css','utf8');
 const out='test-results/installer-appearance';fs.mkdirSync(out,{recursive:true});
 const results=[];
 for(const theme of ['light','dark'])for(const stage of ['welcome','settings','installing','finish','hotkey','confirm']){
 await page.evaluate(({theme,stage})=>{
 document.documentElement.dataset.theme=theme;document.body.dataset.stage=stage;
 document.querySelectorAll('section').forEach(e=>e.hidden=e.id!==stage);
 document.querySelectorAll('button').forEach(e=>{if(!e.textContent)e.textContent=e.id});
 document.querySelector('#light').setAttribute('aria-pressed',String(theme==='light'));
 document.querySelector('#dark').setAttribute('aria-pressed',String(theme==='dark'));
 },{theme,stage});
 const geometry=()=>page.locator('.shell,header,main,footer,section,button,input,h1,h2,.ribbon,.switch').evaluateAll(es=>es.map(e=>({id:e.id,rect:e.getBoundingClientRect().toJSON()})));
 const before=await geometry();const style=await page.addStyleTag({content:overlay});
 assert.deepEqual(await geometry(),before,theme+' '+stage+' geometry');
 assert.equal(await page.locator('body').evaluate(e=>getComputedStyle(e).backgroundColor),theme==='light'?'rgb(246, 246, 248)':'rgb(8, 9, 11)');
 await page.locator('#close').hover();
 const hover=await page.locator('#close').evaluate(e=>getComputedStyle(e,'::before').backgroundColor);
 assert.notEqual(hover,'rgba(0, 0, 0, 0)');
 await page.mouse.down();
 assert.notEqual(await page.locator('#close').evaluate(e=>getComputedStyle(e,'::before').backgroundColor),hover);
 await page.mouse.up();
 await page.screenshot({path:path.join(out,theme+'-'+stage+'.png')});
 await style.evaluate(e=>e.remove());results.push({theme,stage,geometry:true,palette:true,captionFeedback:true});
 }
 fs.writeFileSync(path.join(out,'checks.json'),JSON.stringify(results,null,2));
 console.log('PASS: both palettes, caption feedback, unchanged geometry across 12 theme/stage combinations');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
