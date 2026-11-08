// Run with: node test/browser/panel_admin.cjs (requires playwright + Chromium).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const source = fs.readFileSync(path.join(__dirname, '../../src/panel_admin/panel_page.h'), 'utf8');
const html = source.match(/R"HTML\(([\s\S]*?)\)HTML";/)?.[1];
assert.ok(html, 'embedded panel HTML not found');

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const width of [375, 430, 1280]) {
      const page = await browser.newPage({ viewport: { width, height: 850 } });
      const initial = {
        version: 1, layout: 'dashboard',
        tasks: [{ text: '<img src=x onerror=alert(1)>', done: false }],
        courses: [{ day: 2, text: '09:00 数学 A101' }],
      };
      let saved;
      await page.route('http://panel.test/**', async route => {
        const request = route.request();
        if (request.url().endsWith('/api/panel') && request.method() === 'GET') {
          return route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(initial) });
        }
        if (request.url().endsWith('/api/panel') && request.method() === 'POST') {
          saved = { headers: request.headers(), body: JSON.parse(request.postData()) };
          return route.fulfill({ status: 200, contentType: 'application/json',
            body: JSON.stringify({ saved: true, layout: saved.body.layout }) });
        }
        return route.fulfill({ status: 200, contentType: 'text/html', body: html });
      });
      await page.goto('http://panel.test/panel');
      await page.waitForFunction(() => document.getElementById('taskCount').textContent === '1 / 12');
      assert.equal(await page.locator('#dashboardPanel').isVisible(), true);
      await page.locator('input[name=layout][value=tasks]').check();
      assert.equal(await page.locator('#taskPanel').isVisible(), true);
      assert.equal(await page.locator('#dashboardPanel').isVisible(), false);
      assert.equal(await page.locator('#taskList img').count(), 0, 'task text must not become HTML');
      assert.ok(await page.locator('#taskList').innerText().then(text => text.includes('<img')));
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true,
        `horizontal overflow at ${width}px`);

      await page.locator('#taskList input[type=checkbox]').check();
      await page.locator('#taskText').fill('整理书包');
      await page.locator('#taskForm button').click();
      await page.locator('input[name=layout][value=courses]').check();
      assert.equal(await page.locator('#coursePanel').isVisible(), true);
      assert.equal(await page.locator('#taskPanel').isVisible(), false);
      await page.getByRole('button', { name: '周二' }).click();
      assert.ok((await page.locator('#courseList').innerText()).includes('数学'));
      await page.locator('#courseText').fill('10:00 英语');
      await page.locator('#courseForm button').click();
      await page.locator('#save').click();
      await page.waitForFunction(() => document.getElementById('message').textContent.includes('已保存“今日课程”'));
      assert.equal(saved.headers['x-panel-request'], '1');
      assert.equal(saved.body.layout, 'courses');
      assert.equal(saved.body.tasks[0].done, true);
      assert.equal(saved.body.tasks[1].text, '整理书包');
      assert.equal(saved.body.courses[1].day, 2);
      await page.close();
      console.log(`panel admin: ${width}px OK`);
    }
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
