const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { test } = require('node:test');
const vm = require('node:vm');

const ui = readFileSync('firmware/src/web/WebUi.h', 'utf8');
const script = ui.match(/<script>([\s\S]*?)<\/script>/)[1]
  .replace(/boot\(\);\s*$/, '');
const firmwareCapacity = Number(readFileSync('firmware/src/config/LightingConfig.h', 'utf8')
  .match(/MAX_LOGICAL_PIXELS = (\d+)/)[1]);
const firmwareMaxStrips = Number(readFileSync('firmware/src/config/LightingConfig.h', 'utf8')
  .match(/MAX_STRIPS = (\d+)/)[1]);
const fixture = {
  configVersion: 5, stripCount: 1, maxBrightness: 80,
  stripFadeInMs: 1680, nextStripStartProgress: 0.8,
  stripStartMode: 'cascade', startDelayMs: 2000, holdMs: 3000,
  fadeOutMs: 2640, fadeOutStyle: 'global', gamma: 2.2,
  enableWaterfall: true, waterfallDirection: 'top-to-bottom',
  waterfallSpeedPps: 40, enableLuxGate: false, luxThreshold: 15,
  strips: [{ pixelCount: 18, reversed: false }],
};

function createUi({ strips = fixture.strips, capacity = firmwareCapacity } = {}) {
  const ids = [...ui.matchAll(/id="([\w-]+)"/g)].map(match => match[1]);
  const geometryIds = strips.flatMap((_, index) => [`pixels-${index}`, `reverse-${index}`]);
  const elements = Object.fromEntries([...ids, ...geometryIds].map(id => [
    id, { value: '', checked: false, appendChild() {}, style: {},
      disabled: id === 'applyConfig' || id === 'saveConfig' },
  ]));
  strips.forEach((strip, index) => {
    elements[`pixels-${index}`].value = String(strip.pixelCount);
    elements[`reverse-${index}`].checked = strip.reversed;
  });
  const currentConfig = { ...fixture, stripCount: strips.length, strips };
  const requests = [];
  const statusResponse = {
    maxPixels: capacity, maxStrips: firmwareMaxStrips,
    sessionRemainingMs: 60000, hasLuxReading: false,
    stripCount: strips.length, totalPixels: strips.reduce((sum, strip) => sum + strip.pixelCount, 0),
    sensors: { leftPirActive: false, rightPirActive: false, bh1750Available: false },
    device: { uptimeMs: 1000, firmwareVersion: 'test', buildTimestamp: 'test', resetReason: 'test',
      freeHeapBytes: 100000, minimumFreeHeapBytes: 90000, sketchSizeBytes: 1200000,
      freeSketchSpaceBytes: 700000, apStations: 1 },
    calibration: { active: false },
  };
  const context = vm.createContext({
    fixture: currentConfig, capacity, stripLimit: firmwareMaxStrips,
    document: {
      getElementById: id => elements[id],
      createElement: () => ({}),
      addEventListener() {},
    },
    fetch: async (path, options) => {
      requests.push({ path, options });
      return { ok: true, json: async () => path === '/api/status' ? statusResponse : currentConfig };
    },
    setTimeout() {}, clearTimeout() {}, setInterval() {},
  });
  vm.runInContext(script, context);
  return { elements, requests, statusResponse, evaluate: code => vm.runInContext(code, context) };
}

test('all animation durations render in seconds and preserve millisecond configuration', () => {
  const { elements, evaluate } = createUi();
  evaluate('config = fixture; maxPixels = capacity; maxStrips = stripLimit; renderConfig()');
  assert.equal(elements.startDelay.value, 2);
  assert.equal(elements.fadeIn.value, 1.68);
  assert.equal(elements.hold.value, 3);
  assert.equal(elements.fadeOut.value, 2.64);
  assert.deepEqual(JSON.parse(evaluate('JSON.stringify(formConfig())')), fixture);

  for (const [id, field, maxSeconds] of [
    ['startDelay', 'startDelayMs', 60],
    ['fadeIn', 'stripFadeInMs', 600],
    ['hold', 'holdMs', 3600],
    ['fadeOut', 'fadeOutMs', 600],
  ]) {
    for (const [seconds, expectedMs] of [[0.001, 1], [1.001, 1001], [1.234, 1234], [2.5, 2500], [maxSeconds, maxSeconds * 1000]]) {
      elements[id].value = String(seconds);
      assert.equal(evaluate(`formConfig().${field}`), expectedMs);
    }
  }
  elements.startDelay.value = '0';
  assert.equal(evaluate('formConfig().startDelayMs'), 0);
});

function stripsWithTotal({ count, total }) {
  return Array.from({ length: count }, (_, index) => ({
    pixelCount: Math.floor(total / count) + (index < total % count ? 1 : 0),
    reversed: index % 2 !== 0,
  }));
}

for (const [count, total] of [[4, 180], [5, 181], [5, 270], [8, 360], [8, 361], [8, firmwareCapacity]]) {
  test(`${count} strips / ${total} pixels are accepted using firmware capacity`, async () => {
    const { elements, evaluate, requests } = createUi({ strips: stripsWithTotal({ count, total }) });
    await evaluate('boot()');
    assert.equal(elements.geometrySummary.textContent, `${count} strips · ${total} / ${firmwareCapacity} logical pixels`);
    assert.match(elements.geometryCapacity.textContent, new RegExp(`^${firmwareCapacity - total} logical pixels available`));
    assert.equal(elements.applyConfig.disabled, false);
    assert.equal(elements.saveConfig.disabled, false);
    assert.equal(await evaluate('applyWorking(true)'), true);
    const update = requests.find(request => request.options?.method === 'PUT');
    const candidate = JSON.parse(update.options.body);
    assert.equal(candidate.stripCount, count);
    assert.equal(candidate.strips.reduce((sum, strip) => sum + strip.pixelCount, 0), total);
  });
}

for (const [name, strips, message] of [
  ['over capacity', stripsWithTotal({ count: 8, total: firmwareCapacity + 1 }), /capacity exceeded by 1/],
  ['empty strip', [{ pixelCount: 0, reversed: false }], /Strip #1.*at least 1/],
  ['fractional length', [{ pixelCount: 1.5, reversed: false }], /whole logical pixel/],
  ['too many strips', stripsWithTotal({ count: firmwareMaxStrips + 1, total: 180 }), /Configure between 1 and/],
]) {
  test(`${name} is blocked before sending configuration`, async () => {
    const { elements, evaluate, requests } = createUi({ strips });
    await evaluate('boot()');
    assert.match(elements.geometryCapacity.textContent, message);
    assert.equal(elements.applyConfig.disabled, true);
    assert.equal(elements.saveConfig.disabled, true);
    assert.equal(await evaluate('applyWorking(true)'), false);
    assert.equal(requests.some(request => request.options?.method === 'PUT'), false);
    assert.match(elements.notice.textContent, message);
  });
}

test('capacity comes from status without a UI fallback', async () => {
  const { elements, evaluate, requests } = createUi({ capacity: 1024 });
  assert.equal(evaluate('maxPixels'), null);
  assert.equal(evaluate('maxStrips'), null);
  assert.equal(elements.applyConfig.disabled, true);
  await evaluate('boot()');
  assert.equal(evaluate('maxPixels'), 1024);
  assert.equal(elements.geometrySummary.textContent, '1 strips · 18 / 1024 logical pixels');
  assert.match(elements.geometryCapacity.textContent, /^1006 logical pixels available/);
  assert.equal(requests.some(request => request.path === '/api/status'), true);
});

test('unavailable firmware capacity keeps configuration disabled', async () => {
  const { elements, evaluate, requests } = createUi({ capacity: null });
  await evaluate('boot()');
  assert.equal(evaluate('maxPixels'), null);
  assert.equal(elements.applyConfig.disabled, true);
  assert.equal(elements.saveConfig.disabled, true);
  assert.match(elements.notice.textContent, /Firmware capacity unavailable/);
  assert.match(elements.geometryCapacity.textContent, /Firmware capacity unavailable/);
  assert.equal(requests.some(request => request.options?.method === 'PUT'), false);
});

test('editing lengths updates capacity and validation without losing unsaved geometry', async () => {
  const { elements, evaluate } = createUi();
  await evaluate('boot()');
  elements['pixels-0'].value = String(firmwareCapacity + 1);
  evaluate('geometryChanged()');
  assert.equal(elements.applyConfig.disabled, true);
  elements['pixels-0'].value = '181';
  await evaluate('refreshStatus()');
  assert.equal(elements.applyConfig.disabled, false);
  assert.equal(elements.geometrySummary.textContent, `1 strips · 181 / ${firmwareCapacity} logical pixels`);
  assert.equal(elements.total.textContent, 181);
});
