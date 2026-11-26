const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const target = path.join(process.env.RUNNER_TEMP, 'Screenshot RE integration');
const setup = path.resolve('Screenshot-1.8.6-RE-Setup-x64.exe');
const artifacts = 'installer-review';
fs.mkdirSync(artifacts, { recursive: true });

async function run(exe, args, port, test) {
  const child = spawn(exe, args, {
    env: { ...process.env, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: '--remote-debugging-port=' + port,
      WEBVIEW2_USER_DATA_FOLDER: path.join(process.env.RUNNER_TEMP, 'screenshot-webview-' + port) },
    stdio: 'ignore',
  });
  try {
    let browser;
    for (let i = 0; i < 60 && !browser; i++) {
      try { browser = await chromium.connectOverCDP('http://127.0.0.1:' + port, {timeout: 1000}); }
      catch { await sleep(250); }
    }
    assert(browser, 'WebView2 did not start');
    const context = browser.contexts()[0];
    const page = context.pages()[0] || await context.waitForEvent('page');
    await page.locator('#primary').waitFor();
    await test(page);
    assert.equal(await Promise.race([
      new Promise(resolve => child.once('exit', resolve)),
      sleep(10000).then(() => child.exitCode),
    ]), 0, 'Installer failed to close');
  } finally {
    if (child.exitCode === null) child.kill();
  }
}

async function main() {
  await run(setup, [], 9235, async page => {
    await page.screenshot({path: artifacts + '/welcome.png'});
    await page.locator('#settingsButton').click();
    await page.locator('#en').click();
    await page.locator('#dark').click();
    await page.locator('#path').fill(target);
    await page.screenshot({path: artifacts + '/settings-dark-en.png'});
    await page.locator('#accept').click();
    await page.locator('#hotkey').waitFor({timeout: 90000});
    assert.deepEqual(fs.readFileSync(path.join(target, 'Screenshot.exe')), fs.readFileSync('rust/setup/payload/Screenshot.exe'));
    await page.locator('#keyField').click();
    await page.keyboard.press('PrintScreen');
    await page.locator('#hotkeyNext').click();
    await page.locator('#confirm').waitFor();
    await page.screenshot({path: artifacts + '/print-screen.png'});
    await page.locator('#hotkeyCancel').click();
    await page.locator('#keyField').click();
    await page.keyboard.press('F23');
    await page.locator('#hotkeyNext').click();
    await page.locator('#finish').waitFor();
    await page.screenshot({path: artifacts + '/finish.png'});
    await page.locator('#done').click();
  });
  const exe = path.join(target, 'Screenshot.exe');
  assert.match(execFileSync('reg.exe', ['query', 'HKCU\\Software\\SanderStripa\\Screenshot', '/v', 'HotkeyVK'], {encoding: 'utf8'}), /0x86/i);
  await run(path.join(target, 'Uninstall.exe'), ['--uninstall'], 9236, async page => {
    await page.locator('#primary').click();
    await page.locator('#finish').waitFor();
    assert(!fs.existsSync(exe), 'Uninstaller did not remove Screenshot.exe');
    await page.locator('#done').click();
  });
  console.log('PASS install, payload identity, Print Screen consent, F23, launch, uninstall');
}
main().then(() => process.exit(0), error => { console.error(error); process.exit(1); });
