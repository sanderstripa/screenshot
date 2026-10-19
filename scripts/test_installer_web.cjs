// Run against the actual Windows WebView2 installer with an explicitly enabled CDP port.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'C:/Users/Sander Stripa/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const properties=['fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','borderTopWidth','borderTopColor','backgroundColor','color','opacity','transform','zoom','filter','backdropFilter','willChange','height','paddingTop','paddingRight','paddingBottom','paddingLeft'];
async function styles(page,selector){return page.locator(selector).first().evaluate((el,keys)=>Object.fromEntries(keys.map(k=>[k,getComputedStyle(el)[k]])),properties)}
(async()=>{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:'+(process.env.CDP_PORT||9227));
 const page=browser.contexts()[0].pages().find(p=>p.url()==='https://screenshot.setup/');assert(page,'Actual installer page missing');
 const output=path.resolve('test-results/installer-0.7.1');fs.mkdirSync(output,{recursive:true});
 await page.waitForFunction(()=>document.querySelector('#version').textContent.includes('0.7.1'));
 await page.evaluate(()=>document.fonts.ready);if(await page.locator('#settingsButton').isVisible())await page.locator('#settingsButton').click();
 await page.locator('#settings').waitFor({state:'visible'});
 const ref=JSON.parse(fs.readFileSync('test-results/vpn-reference.json','utf8'));
 const chrome=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});const reference=await chrome.newPage({viewport:{width:840,height:540}});
 await reference.route('https://vpn.reference/**',async route=>{
  const url=new URL(route.request().url());if(url.pathname.endsWith('Montserrat.ttf'))return route.fulfill({body:fs.readFileSync('assets/fonts/Montserrat.ttf'),contentType:'font/ttf'});
  if(url.pathname==='/style.css')return route.fulfill({body:ref.css,contentType:'text/css'});
  if(url.pathname==='/')return route.fulfill({body:ref.html.replace(/<script[\s\S]*?<\/script>/g,''),contentType:'text/html'});
  return route.fulfill({body:'',status:404});
 });
 await reference.goto('https://vpn.reference/');
 await reference.evaluate(()=>{document.body.dataset.stage='settings';document.querySelector('#welcome').hidden=true;document.querySelector('#settings').hidden=false;for(const [id,text] of Object.entries({languageLabel:'Язык',themeLabel:'Тема',pathLabel:'Папка установки',browse:'Выбрать…',settingsTitle:'Настройки установки',light:'Светлая',dark:'Тёмная',desktopLabel:'Ярлык на рабочем столе',menuLabel:'Ярлык в меню «Пуск»',extensionHint:'Расширение нужно загрузить вручную в Chrome. Установщик подготовит папку и скопирует путь.',accept:'Принять и установить'}))document.getElementById(id).textContent=text;document.querySelector('#ru').setAttribute('aria-pressed','true');document.querySelector('#light').setAttribute('aria-pressed','true');});
 await reference.evaluate(()=>document.fonts.ready);
 const report={};
 for(const theme of ['light','dark']){
  await page.locator('#'+theme).click();await page.waitForFunction(t=>document.documentElement.dataset.theme===t,theme);
  await reference.evaluate(t=>{document.documentElement.dataset.theme=t;document.querySelector('#light').setAttribute('aria-pressed',String(t==='light'));document.querySelector('#dark').setAttribute('aria-pressed',String(t==='dark'));},theme);
  report[theme]={};for(const selector of ['#pathLabel','#path','#browse','#languageLabel','#themeLabel','#ru','#en','#light','#dark','.switch-row','.switch']){const actual=await styles(page,selector),expected=await styles(reference,selector);assert.deepStrictEqual(actual,expected,theme+' '+selector);report[theme][selector]=actual;}
  await page.screenshot({path:path.join(output,'settings-'+theme+'.png')});
 }
 const y=await page.locator('#accept').evaluate(e=>e.getBoundingClientRect().top);assert.strictEqual(y,387,'Accept vertical anchor');
 // Repeated native state updates must preserve the input node, selection and geometry.
 const stable=await page.locator('#path').evaluate(async e=>{e.focus();e.setSelectionRange(2,5);const before=e.getBoundingClientRect().toJSON();for(let i=0;i<20;i++)window.chrome.webview.postMessage('ready\n');await new Promise(r=>setTimeout(r,300));return {same:e===document.querySelector('#path'),selection:[e.selectionStart,e.selectionEnd],before,after:e.getBoundingClientRect().toJSON()}});
 assert(stable.same);assert.deepStrictEqual(stable.selection,[2,5]);assert.deepStrictEqual(stable.before,stable.after);report.inputStability=stable;
 fs.writeFileSync(path.join(output,'parity.json'),JSON.stringify(report,null,2));
 await chrome.close();await browser.close();console.log('PASS actual WebView2: exact CONNECT computed styles in both themes; accept anchor; stable path node and selection');
})().catch(e=>{console.error(e);process.exit(1)});
