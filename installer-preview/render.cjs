const {chromium}=require('C:/Users/Sander Stripa/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const path=require('path');
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:840,height:640},deviceScaleFactor:1});
 for(const theme of ['light','dark'])for(const stage of ['welcome','settings','installing','installed','hotkey','confirm','ready','error']){
  await page.goto('file:///'+path.resolve(__dirname,'index.html').replaceAll('\\','/')+`?stage=${stage}&theme=${theme}`);
  await page.evaluate(()=>document.fonts.ready);await page.locator('.shell img').evaluateAll(imgs=>Promise.all(imgs.map(i=>i.decode())));
  await page.locator('.shell').screenshot({path:path.resolve(__dirname,`screens/${theme}-${stage}.png`)});
  const overflow=await page.locator('.shell').evaluate(e=>e.scrollWidth>e.clientWidth||e.scrollHeight>e.clientHeight);
  if(overflow)throw Error(`Overflow ${theme} ${stage}`);
 }
 await page.goto('file:///'+path.resolve(__dirname,'index.html').replaceAll('\\','/')+'?stage=settings');
 await page.locator('[data-lang=en]').click();if(!await page.locator('h2').filter({hasText:'Installation settings'}).count())throw Error('Language');
 await page.locator('button[data-theme=dark]').click();if(await page.locator('html').getAttribute('data-theme')!=='dark')throw Error('Theme');
 for(const option of ['desktop','startMenu']){
  await page.locator(`[data-option=${option}]`).uncheck();
  await page.locator('button[data-theme=light]').click();
  if(await page.locator(`[data-option=${option}]`).isChecked())throw Error(`Lost toggle state ${option}`);
 }
 await page.locator('#review [data-next=ready]').click();
 if(!await page.locator('.primary').filter({hasText:'Done'}).count())throw Error('Missing Done action');
 await page.locator('[data-option=launch]').uncheck();await page.locator('[data-language]').click();
 if(await page.locator('[data-option=launch]').isChecked())throw Error('Lost launch state');
 console.log('PASS 16 previews; theme/RU-EN; no overflow; shortcut and launch switches retain state');await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
