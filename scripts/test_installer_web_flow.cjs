const assert=require('assert'),fs=require('fs');
const {chromium}=require('C:/Users/Sander Stripa/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
(async()=>{const browser=await chromium.connectOverCDP('http://127.0.0.1:'+(process.env.CDP_PORT||9229));const page=browser.contexts()[0].pages().find(p=>p.url()==='https://screenshot.setup/');assert(page);
 await page.locator('#settingsButton').click();await page.locator('#en').click();await page.locator('#dark').click();
 await page.waitForFunction(()=>document.documentElement.lang==='en'&&document.documentElement.dataset.theme==='dark');
 for(const id of ['desktop','startMenu']){await page.locator('#'+id).uncheck();await page.locator('#'+id).check();}
 const target=await page.locator('#path').inputValue();assert(target.includes('Screenshot'));
 await page.locator('#path').fill(target+'\\..\\Screenshot');await page.locator('#path').fill(target);await page.locator('#accept').click();
 await page.locator('#installed').waitFor({state:'visible',timeout:60000});await page.locator('#continue').click();
 await page.locator('#keyField').click();await page.waitForFunction(()=>document.querySelector('#keyField').textContent==='Press a key…');const input=await page.context().newCDPSession(page);await input.send('Input.dispatchKeyEvent',{type:'keyDown',key:'F23',code:'F23',windowsVirtualKeyCode:134,nativeVirtualKeyCode:134});await input.send('Input.dispatchKeyEvent',{type:'keyUp',key:'F23',code:'F23',windowsVirtualKeyCode:134,nativeVirtualKeyCode:134});
 await page.waitForFunction(()=>document.querySelector('#keyField').textContent==='F23');await page.locator('#next').click();await page.locator('#finish').waitFor({state:'visible',timeout:15000});
 const geometry=await page.evaluate(()=>Object.fromEntries(['#finish h1','#done','#launchRow'].map(s=>[s,document.querySelector(s).getBoundingClientRect().toJSON()])));
 assert.strictEqual(geometry['#finish h1'].top,131.75);assert.strictEqual(geometry['#done'].top,312.25);assert.strictEqual(geometry['#launchRow'].top,376.25);
 fs.mkdirSync('test-results/installer-0.7.1',{recursive:true});fs.writeFileSync('test-results/installer-0.7.1/flow.json',JSON.stringify({target,geometry,language:'en',theme:'dark',shortcut:'F23'},null,2));
 await page.locator('#done').click();await browser.close();console.log('PASS real Windows installation, shortcuts, inherited EN/dark, F23 activation and Done launch');
})().catch(e=>{console.error(e);process.exit(1)});
