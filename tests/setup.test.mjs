// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import test from 'node:test';
import vm from 'node:vm';

const html = readFileSync(
    new URL('../firmware/components/pace_services/setup.html', import.meta.url), 'utf8');
const script = html.match(/<script nonce="@TOKEN@">([\s\S]*?)<\/script>/)[1];

function page(language = 'de-DE', signed = true, saved = {configured: false}, loadFails = false) {
  const elements = new Map();
  const listeners = new Map();
  let clock = 100000;
  const element = id => {
    if (!elements.has(id))
      elements.set(id, {
        value: '',
        textContent: '',
        children: [],
        files: [],
        checked: false,
        setAttribute() {},
        get lastElementChild() { return this.children.at(-1); },
        replaceChildren(...children) { this.children = children; },
        append(...children) {
          this.children.push(...children);
        },
        remove() {}
      });
    return elements.get(id);
  };
  const nodes = [
    ...html.replace(/<style>[\s\S]*?<\/style>|<script[\s\S]*?<\/script>/g, '')
        .matchAll(/>([^<>]+)</g)
  ].map(match => ({textContent: match[1].trim(), parentElement: {tagName: 'SPAN'}}));
  const document = {
    documentElement: {},
    hidden: false,
    addEventListener: (name, callback) => listeners.set(name, callback),
    body: {},
    title: '',
    getElementById: element,
    createElement: () => ({
      children: [],
      setAttribute() {},
      append(...items) {
        this.children.push(...items);
      },
      remove() {}
    }),
    createTreeWalker: () => {
      let index = 0;
      return {nextNode: () => nodes[index++]};
    }
  };
  for (const [id, value] of Object.entries({
         secondaryColor: '#7251a6',
         tileEndpoint: 'https://tile.openstreetmap.org',
         trafficProvider: 'open',
         roadEndpoint: 'https://routing.openstreetmap.de/routed-car',
         transitEndpoint: 'https://api.transitous.org/api/v6/plan',
         profile: 'demo',
         cycle: 'h23',
         unit: 'C',
         nightStart: '22:00',
         nightEnd: '07:00'
       }))
    element(id).value = value;
  const calls = [], uploads = [];
  class Upload {
    constructor() {
      this.upload = {};
      this.headers = {};
      uploads.push(this);
    }
    open(method, path) {
      this.method = method;
      this.path = path;
    }
    setRequestHeader(key, value) {
      this.headers[key] = value;
    }
    send(file) {
      this.file = file;
    }
  }
  const context = vm.createContext({
    document,
    navigator: {language},
    Date: {now: () => clock},
    Intl,
    AbortSignal,
    URL,
    NodeFilter: {SHOW_TEXT: 4},
    XMLHttpRequest: Upload,
    confirm: () => true,
    fetch: async (path, options) => {
      calls.push({path, options});
      return {
        ok: !(path === '/settings' && loadFails),
        text: async () => 'Requested. Check the device screen for progress.',
        json: async () => path === '/settings' ? saved : ({
          version: '0.3.0-dev',
          network: 'connected',
          time: 'synchronized',
          calendar: 'Calendar synchronized',
          storage: 'ready',
          ota_available: signed,
          ota_source: false,
          ota_result: 'ESP_OK',
          updating: false
        })
      };
    }
  });
  vm.runInContext(script, context);
  const ready = vm.runInContext('initialSettings', context);
  return {element, document, nodes, context, calls, uploads, ready, listeners, advance: ms => { clock += ms; }};
}

test('German setup, English toggle, explicit language and optional update source', async () => {
  const p = page();
  await p.ready;
  await p.element('refresh').onclick();
  assert.equal(p.document.documentElement.lang, 'de');
  assert(p.nodes.some(node => node.textContent === 'PACE einrichten'));
  assert.equal(p.element('upload').disabled, false);
  assert.equal(p.element('update').disabled, true);
  p.element('updateSource').value = 'https://example.invalid/manifest.json';
  const config = vm.runInContext('config()', p.context);
  assert.equal(config.ui.language, 'de');
  assert.equal(config.updates.manifest_url, 'https://example.invalid/manifest.json');
  p.element('language').value = 'en';
  p.element('language').onchange();
  assert.equal(p.document.title, 'PACE setup');
  assert(p.nodes.some(node => node.textContent === 'Set up PACE'));
});

test('manual sync carries CSRF and localized feedback', async () => {
  const p = page();
  await p.ready;
  await p.element('sync').onclick();
  const call = p.calls.find(call => call.path === '/action');
  assert.equal(call.options.headers['X-PACE-CSRF'], '@TOKEN@');
  assert.equal(JSON.parse(call.options.body).action, 'sync');
  assert.equal(
      p.element('message').textContent, 'Angefordert. Fortschritt auf dem Display prüfen.');
});

test('signed upload uses bounded binary content, CSRF and progress without hosting', async () => {
  const p = page();
  await p.ready;
  await p.element('refresh').onclick();
  p.element('firmwareFile').files = [{name: 'pace.bin', size: 1024}];
  p.element('upload').onclick();
  const upload = p.uploads[0];
  assert.equal(upload.path, '/update');
  assert.equal(upload.headers['Content-Type'], 'application/octet-stream');
  assert.equal(upload.headers['X-PACE-CSRF'], '@TOKEN@');
  assert.equal(upload.timeout, 370000);
  upload.upload.onprogress({lengthComputable: true, loaded: 512, total: 1024});
  assert.equal(p.element('uploadProgress').value, 50);
  p.element('firmwareFile').files = [{size: 3145729}];
  p.element('upload').onclick();
  assert.equal(p.uploads.length, 1);
  assert.match(p.element('message').textContent, /höchstens 3 MiB/);
});

test('unsigned development build clearly disables both OTA paths', async () => {
  const p = page('en-GB', false);
  await p.ready;
  await p.element('refresh').onclick();
  assert.equal(p.element('update').disabled, true);
  assert.equal(p.element('upload').disabled, true);
  assert.match(p.element('diagnostics').textContent, /not configured/);
});


test('Traffic setup bounds routes, validates inputs and serializes both travel modes', async () => {
  const p = page();
  await p.ready;
  p.element('traffic').checked = true;
  p.element('trafficProvider').value = 'google_routes';
  assert.throws(() => vm.runInContext('config()', p.context), /complete routes/);
  p.element('trafficKey').value = 'synthetic-key';
  p.element('ssid').value = 'TestNetwork';
  for (let i = 0; i < 5; i++) p.element('addRoute').onclick();
  assert.equal(p.element('routes').children.length, 4);
  for (const [i, row] of p.element('routes').children.entries()) {
    row.children[0].children[0].value = `Journey ${i + 1}`;
    row.children[1].children[0].value = 'Test Park';
    row.children[2].children[0].value = 'Test Office';
    row.children[3].children[0].value = i % 2 ? 'transit' : 'car';
  }
  const config = vm.runInContext('config()', p.context);
  assert.equal(config.traffic.routes[1].mode, 'transit');
  if (process.env.PACE_FEED_CHECK) {
    const directory = mkdtempSync(join(tmpdir(), 'pace-setup-'));
    try {
      const path = join(directory, 'settings.json');
      writeFileSync(path, JSON.stringify(config));
      const result =
          spawnSync(process.env.PACE_FEED_CHECK, ['--settings', path], {encoding: 'utf8'});
      assert.equal(result.status, 0, result.stdout + result.stderr);
    } finally {
      rmSync(directory, {recursive: true, force: true});
    }
  }
  p.element('traffic').checked = false;
  const disabled = vm.runInContext('config()', p.context);
  assert.equal(disabled.traffic.api_key, 'synthetic-key');
  assert.equal(disabled.traffic.routes.length, 4);
});


test('open routing defaults, coordinates, private contact and server validation', async () => {
  const p = page('en-GB');
  await p.ready;
  p.element('ssid').value = 'TestNetwork';
  p.element('traffic').checked = true;
  p.element('addRoute').onclick();
  const row = p.element('routes').children[0];
  row.children[0].children[0].value = 'Test commute';
  row.children[1].children[0].value = '48, 8';
  row.children[2].children[0].value = '48.05, 8.2';
  let config = vm.runInContext('config()', p.context);
  assert.equal(config.traffic.provider, 'open');
  assert.equal(config.traffic.api_key, '');
  row.children[3].children[0].value = 'transit';
  assert.throws(() => vm.runInContext('config()', p.context), /contact/);
  p.element('trafficContact').value = 'https://example.org';
  config = vm.runInContext('config()', p.context);
  if (process.env.PACE_FEED_CHECK) {
    const directory = mkdtempSync(join(tmpdir(), 'pace-open-setup-'));
    try {
      const path = join(directory, 'settings.json');
      writeFileSync(path, JSON.stringify(config));
      const result =
          spawnSync(process.env.PACE_FEED_CHECK, ['--settings', path], {encoding: 'utf8'});
      assert.equal(result.status, 0, result.stdout + result.stderr);
    } finally {
      rmSync(directory, {recursive: true, force: true});
    }
  }
  row.children[1].children[0].value = '91, 8';
  assert.throws(() => vm.runInContext('config()', p.context), /latitude/);
  row.children[1].children[0].value = '48, 8';
  p.element('roadEndpoint').value = 'http://example.org';
  assert.throws(() => vm.runInContext('config()', p.context), /HTTPS/);
  p.element('roadEndpoint').value = 'https://example.org?key=secret';
  assert.throws(() => vm.runInContext('config()', p.context), /HTTPS/);
  p.element('roadEndpoint').value = 'https://example.org/route';
  p.element('trafficContact').value = 'test\r\nInjected: x';
  assert.throws(() => vm.runInContext('config()', p.context), /contact/);
});


function savedFixture() {
  const c = JSON.parse(readFileSync(new URL('../docs/device.example.json', import.meta.url), 'utf8'));
  c.wifi.password = null;
  c.calendar = {transport_profile: 'generic_https', endpoint: null, device_token: null};
  c.updates.manifest_url = null;
  c.preferences = {time_zone: 'Asia/Kathmandu', locale: 'en-GB', hour_cycle: 'h12', temperature_unit: 'F'};
  c.ui = {...c.ui, language: 'en', night_blanking_enabled: true, night_start: 1230, night_end: 375,
    secondary_color: 0x123456, category_styles: [{id: 'example', rgb: 0xabcdef}]};
  c.weather = {enabled: true, location_label: 'Test place', latitude: 0, longitude: 12.5};
  c.traffic = {...c.traffic, enabled: true, provider: 'google_routes', api_key: null,
    routes: [{name: 'Test journey', origin: 'Test A', destination: 'Test B', mode: 'transit'}]};
  return c;
}

test('saved settings load without exposing secrets and one-field edits preserve the rest', async () => {
  const saved = savedFixture();
  const p = page('de-DE', true, saved);
  assert.equal(p.element('settingsFields').disabled, true);
  await p.ready;
  assert.equal(p.element('settingsFields').disabled, false);
  assert.equal(p.document.documentElement.lang, 'en');
  assert.equal(p.element('ssid').value, saved.wifi.ssid);
  assert.equal(p.element('zone').value, 'Asia/Kathmandu');
  assert.equal(p.element('latitude').value, '0');
  assert.equal(p.element('nightStart').value, '20:30');
  assert.equal(p.element('nightEnd').value, '06:15');
  for (const id of ['password', 'secret', 'endpoint', 'updateSource', 'trafficKey']) {
    assert.equal(p.element(id).value, '');
    assert.match(p.element(id).placeholder, /leave blank to keep/);
  }
  const read = p.calls.find(call => call.path === '/settings');
  assert.equal(read.options.headers['X-PACE-CSRF'], '@TOKEN@');
  assert.equal(read.options.cache, 'no-store');
  p.element('unit').value = 'C';
  const edited = JSON.parse(JSON.stringify(vm.runInContext('config()', p.context)));
  const expected = structuredClone(saved);
  expected.preferences.temperature_unit = 'C';
  assert.deepEqual(edited, expected);
  await p.element('loadSettings').onclick();
  assert.equal(p.element('categories').children.length, 1);
  assert.equal(p.element('routes').children.length, 1);
});

test('changing a network or adapter requires its credential; explicit clear is separate from keep', async () => {
  const p = page('en-GB', true, savedFixture());
  await p.ready;
  p.element('ssid').value = 'Different test network';
  assert.throws(() => vm.runInContext('config()', p.context), /password for the new network/);
  p.element('openNetwork').checked = true;
  assert.equal(vm.runInContext('config()', p.context).wifi.password, '');
  p.element('endpoint').value = 'https://example.invalid/new-feed';
  assert.throws(() => vm.runInContext('config()', p.context), /pairing secret/);
  p.element('secret').value = 'a'.repeat(43);
  p.element('clearUpdateSource').checked = true;
  const edited = vm.runInContext('config()', p.context);
  assert.equal(edited.calendar.device_token, 'a'.repeat(43));
  assert.equal(edited.updates.manifest_url, '');
  assert.equal(p.element('clearTrafficKeyRow').hidden, false);
  p.element('traffic').checked = false;
  p.element('clearTrafficKey').checked = true;
  assert.equal(vm.runInContext('config()', p.context).traffic.api_key, '');
});

test('failed settings reads cannot submit defaults over saved settings', async () => {
  const p = page('en-GB', true, {}, true);
  await p.ready;
  assert.equal(p.element('settingsFields').disabled, true);
  assert.equal(p.element('loadSettings').hidden, false);
  assert.throws(() => vm.runInContext('config()', p.context), /Could not load/);
  p.element('setup').onsubmit({preventDefault() {}});
  assert.equal(p.calls.filter(call => call.path === '/settings' && call.options.method === 'POST').length, 0);
});

test('real interaction renews setup with CSRF, throttled; idle and hidden tabs do not', async () => {
  const p = page('en-GB');
  await p.ready;
  const count = () => p.calls.filter(call => call.path === '/activity').length;
  assert.equal(count(), 0);
  p.listeners.get('input')();
  p.listeners.get('pointerdown')();
  p.listeners.get('keydown')();
  assert.equal(count(), 1);
  const request = p.calls.find(call => call.path === '/activity');
  assert.equal(request.options.method, 'POST');
  assert.equal(request.options.headers['X-PACE-CSRF'], '@TOKEN@');
  p.advance(30000);
  assert.equal(count(), 1);
  p.listeners.get('input')();
  assert.equal(count(), 2);
  p.advance(30000);
  p.document.hidden = true;
  p.listeners.get('input')();
  assert.equal(count(), 2);
  assert.match(html, /15 minutes without activity/);
  assert.doesNotMatch(html, /10 minutes|10 Minuten/);
});


test('coordinate examples match accepted route pairs and separate weather fields', async () => {
  const p = page('en-GB');
  await p.ready;
  p.element('traffic').checked = true;
  p.element('addRoute').onclick();
  const row = p.element('routes').children[0];
  row.children[0].children[0].value = 'Example journey';
  for (const index of [1, 2]) {
    assert.match(row.children[index].children[0].placeholder, /48\.1234, 8\.5678/);
    row.children[index].children[0].value = '48.1234, 8.5678';
  }
  assert.equal(vm.runInContext('config()', p.context).traffic.routes.length, 1);
  row.children[1].children[0].value = '48,1234, 8,5678';
  assert.throws(() => vm.runInContext('config()', p.context), /latitude, longitude/);
  row.children[1].children[0].value = '-48.1234, -8.5678';
  p.element('weather').checked = true;
  p.element('location').value = 'Example place';
  p.element('latitude').value = '48.1234';
  p.element('longitude').value = '8.5678';
  const c = vm.runInContext('config()', p.context);
  assert.equal(c.weather.latitude, 48.1234);
  assert.equal(c.weather.longitude, 8.5678);
  for (const invalid of ['48,1234', '91', '48.1234, 8.5678', '']) {
    p.element('latitude').value = invalid;
    assert.throws(() => vm.runInContext('config()', p.context), /separate valid latitude/);
  }
  p.element('language').value = 'de';
  p.element('language').onchange();
  assert.match(row.children[1].children[0].placeholder, /Koordinaten/);
  assert(p.nodes.some(node => /Dezimalpunkt.*Dezimalkomma/.test(node.textContent)));
});
