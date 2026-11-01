'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(root, 'src/network/portal.html'), 'utf8');
const output = path.join(root, '.pio/portal-preview');
fs.mkdirSync(output, { recursive: true });

(async () => {
  const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
  try {
    for (const width of [320, 390, 900]) {
      const page = await browser.newPage({ viewport: { width, height: width === 900 ? 900 : 844 } });
      const errors = []; page.on('pageerror', error => errors.push(error.message));
      let state = 'ready', scans = 0, submitted = null, disconnected = false;
      const entries = [
        { ssid: 'Home-Solar', rssi: -43, secure: true },
        { ssid: '客厅无线网络', rssi: -65, secure: true },
        { ssid: 'Guest', rssi: -78, secure: false },
        { ssid: '<img src=x onerror=alert(1)>', rssi: -80, secure: true },
        { ssid: '12345678901234567890123456789012', rssi: -81, secure: true },
      ];
      await page.route('**/*', async route => {
        const url = new URL(route.request().url());
        if (url.pathname === '/') return route.fulfill({ contentType: 'text/html', body: html });
        if (disconnected) return route.fulfill({ status: 503, body: 'Closed' });
        let json;
        if (url.pathname === '/api/status') json = { state, scanning: false };
        else if (url.pathname === '/api/networks') json = { networks: entries, scanning: false };
        else if (url.pathname === '/api/scan') { scans++; json = {}; }
        else if (url.pathname === '/api/config') { submitted = route.request().postDataJSON(); state = 'connecting'; json = {}; }
        else return route.fulfill({ status: 404, body: '' });
        return route.fulfill({ contentType: 'application/json', body: JSON.stringify(json) });
      });
      await page.goto('http://192.168.4.1');
      await page.locator('.network').last().waitFor();
      assert.equal(await page.locator('.network').count(), 5);
      assert.equal(await page.locator('#networks img').count(), 0);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.screenshot({ path: path.join(output, `networks-${width}.png`), fullPage: true });
      await page.getByRole('button', { name: '重新扫描', exact: true }).click();
      await page.waitForFunction(() => !document.getElementById('refresh-button').disabled);
      assert.equal(scans, 1);
      await page.locator('.network').filter({ hasText: 'Home-Solar' }).click();
      assert.equal(await page.locator('#ssid').inputValue(), 'Home-Solar');
      await page.locator('#password').fill('short');
      await page.locator('#submit').click();
      await page.locator('#error').filter({ hasText: '密码需要' }).waitFor();
      assert.equal(submitted, null);
      await page.locator('#password').fill('test-secret');
      await page.getByRole('button', { name: '显示密码', exact: true }).click();
      assert.equal(await page.locator('#password').getAttribute('type'), 'text');
      await page.getByRole('button', { name: '隐藏密码', exact: true }).click();
      await page.locator('#submit').click();
      await page.locator('#status-title').filter({ hasText: '正在连接' }).waitFor();
      assert.deepEqual(submitted, { ssid: 'Home-Solar', password: 'test-secret' });
      assert.equal(await page.locator('#ssid').isDisabled(), true);
      await page.screenshot({ path: path.join(output, `connecting-${width}.png`), fullPage: true });
      state = 'connect_failed';
      await page.locator('#status-title').filter({ hasText: '连接失败' }).waitFor();
      await page.waitForFunction(() => !document.getElementById('ssid').disabled);
      await page.screenshot({ path: path.join(output, `failed-${width}.png`), fullPage: true });
      await page.locator('#ssid').fill('Manual-Network');
      await page.locator('#password').fill('another-secret');
      await page.locator('#submit').click();
      state = 'time_failed';
      await page.locator('#status-title').filter({ hasText: '配网成功' }).waitFor();
      assert.equal(await page.locator('#password').inputValue(), '');
      assert.match(await page.locator('#status-detail').textContent(), /12 小时/);
      await page.screenshot({ path: path.join(output, `success-${width}.png`), fullPage: true });
      disconnected = true;
      await page.locator('#footer-state').filter({ hasText: '热点已关闭' }).waitFor({ timeout: 12000 });
      assert.equal(await page.locator('#status-title').textContent(), '配网成功');
      assert.deepEqual(errors, []);
      await page.close();
    }
    console.log('PASS: 320/390/900px portal, scan, SSID selection/manual entry, password validation/show-hide, progress/failure/success, hotspot shutdown and escaped network names');
    console.log(`Screenshots: ${output}`);
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
