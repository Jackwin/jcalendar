const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../../tools/jcalendar-ble-config.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert.ok(script, 'BLE page script exists');

const elements = new Map();
const document = {
  getElementById(id) {
    if (!elements.has(id)) {
      elements.set(id, {value: '', textContent: '', disabled: false, replaceChildren() {}, append() {}});
    }
    return elements.get(id);
  },
};
const context = vm.createContext({document, navigator: {}, TextEncoder, TextDecoder, Uint8Array, Error});
vm.runInContext(script, context);

(async () => {
  const frames = [];
  context.c = {async writeValueWithResponse(frame) { frames.push(Uint8Array.from(frame)); }};
  // Top-level lexical `c` is distinct from a property on the VM global.
  vm.runInContext('c = globalThis.mockCharacteristic', Object.assign(context, {
    mockCharacteristic: context.c,
  }));
  const request = {op: 'set_panel', config: {version: 1, layout: 'tasks', tasks: [
    {text: '整理书包并检查明日课程安排', done: false},
    {text: '复习数学', done: false},
  ], courses: []}};
  await vm.runInContext('send(globalThis.mockRequest)', Object.assign(context, {mockRequest: request}));
  assert.ok(frames.length > 1, 'save is split into multiple frames');
  assert.ok(frames.every(frame => frame.length <= 20), 'each write fits default ATT MTU');
  assert.equal(frames[0][1] & 1, 1, 'first frame starts a request');
  assert.equal(frames.at(-1)[1] & 2, 2, 'last frame completes a request');
  const payload = Buffer.concat(frames.map(frame => Buffer.from(frame.subarray(2))));
  assert.deepEqual(JSON.parse(payload.toString()), request);
  console.log('BLE configuration framing: ok');
})().catch(error => { console.error(error); process.exitCode = 1; });
