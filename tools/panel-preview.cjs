// Local-only preview of the HTML embedded in the ESP32 firmware.
// Run: node tools/panel-preview.cjs
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '../src/panel_admin/panel_page.h'), 'utf8');
const embedded = source.match(/R"HTML\(([\s\S]*?)\)HTML";/)?.[1];
if (!embedded) throw new Error('Embedded panel HTML not found');
const page = embedded.replace('<header>',
  '<header><p style="padding:10px;border:1px solid #b54838;border-radius:10px;color:#8e3227">'
  + '本地预览：保存仅在此进程内生效，不会写入设备或刷新墨水屏。</p>');
let config = {
  version: 1,
  layout: 'dashboard',
  tasks: [{ text: '整理书包', done: false }, { text: '检查明日课程', done: false }],
  courses: [{ day: 1, text: '08:00 数学 A101' }, { day: 1, text: '10:00 英语 B203' }],
};

http.createServer((request, response) => {
  const send = (status, type, body) => {
    response.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
    response.end(body);
  };
  if (request.url === '/panel' || request.url === '/') {
    send(200, 'text/html; charset=utf-8', page);
  } else if (request.url === '/api/panel' && request.method === 'GET') {
    send(200, 'application/json; charset=utf-8', JSON.stringify(config));
  } else if (request.url === '/api/panel' && request.method === 'POST') {
    if (request.headers['x-panel-request'] !== '1') {
      send(403, 'application/json; charset=utf-8', '{"error":"缺少面板请求标记"}');
      return;
    }
    let body = '';
    request.on('data', chunk => {
      body += chunk;
      if (body.length > 3900) request.destroy();
    });
    request.on('end', () => {
      try {
        const value = JSON.parse(body);
        if (value.version !== 1 || !['dashboard', 'courses', 'tasks'].includes(value.layout)
            || !Array.isArray(value.tasks) || !Array.isArray(value.courses)) throw new Error();
        config = value;
        send(200, 'application/json; charset=utf-8', '{"saved":true}');
      } catch {
        send(400, 'application/json; charset=utf-8', '{"error":"配置格式无效"}');
      }
    });
  } else {
    send(404, 'text/plain; charset=utf-8', 'Not found');
  }
}).listen(8765, '127.0.0.1', () => {
  console.log('Panel preview: http://127.0.0.1:8765/panel');
});
