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

function createUi({ strips = fixture.strips, capacity = firmwareCapacity, clipboard = null, secureContext = false, copyWorks = true, storage = null } = {}) {
  const ids = [...ui.matchAll(/id="([\w-]+)"/g)].map(match => match[1]);
  const geometryIds = strips.flatMap((_, index) => [`pixels-${index}`, `reverse-${index}`]);
  const elements = Object.fromEntries([...ids, ...geometryIds].map(id => [
    id, { value: '', checked: false, appendChild() {}, style: {},
      focus() {}, select() { this.selected = true; }, setSelectionRange() {},
      disabled: id === 'applyConfig' || id === 'saveConfig' || id === 'diagCopy' },
  ]));
  strips.forEach((strip, index) => {
    elements[`pixels-${index}`].value = String(strip.pixelCount);
    elements[`reverse-${index}`].checked = strip.reversed;
  });
  const currentConfig = { ...fixture, stripCount: strips.length, strips };
  const requests = [];
  const clipboardWrites = [];
  const downloads = [];
  const blobs = [];
  const timers = [];
  const saved = new Map();
  const localStorage = storage || {
    getItem: key => saved.get(key) || null,
    setItem: (key, value) => saved.set(key, value),
    removeItem: key => saved.delete(key),
  };
  const reportResponse = {
    schemaVersion: 1, firmwareVersion: '0.2.5', captureId: 1,
    context: 'one-click-diagnostic', overwrittenEntries: 0, wireMeasured: false,
    frames: { count: 42, fadeOutIncreaseFrames: 0 },
    captureConfig: { ...currentConfig, gamma: 1, stripFadeInMs: 3000, holdMs: 2000, fadeOutMs: 5000 },
    events: [{ atMs: 1000, kind: 'STATE', message: 'ON -> FADING_OUT' }],
    coverage: { rmtTelemetryPresent: true, allPreparedFramesObserved: true,
      phaseSummariesTruncated: false, eventLogTruncated: false },
  };
  const statusResponse = {
    maxPixels: capacity, maxStrips: firmwareMaxStrips,
    sessionRemainingMs: 60000, hasLuxReading: false,
    stripCount: strips.length, totalPixels: strips.reduce((sum, strip) => sum + strip.pixelCount, 0),
    sensors: { leftPirActive: false, rightPirActive: false, bh1750Available: false },
    device: { uptimeMs: 1000, firmwareVersion: 'test', buildTimestamp: 'test', resetReason: 'test',
      freeHeapBytes: 100000, minimumFreeHeapBytes: 90000, sketchSizeBytes: 1200000,
      freeSketchSpaceBytes: 700000, apStations: 1 },
    calibration: { active: false },
    animationTestActive: false,
    diagnostics: { phase: 'OFF', frames: 42, errors: 0 },
  };
  const context = vm.createContext({
    fixture: currentConfig, capacity, stripLimit: firmwareMaxStrips,
    document: {
      getElementById: id => elements[id],
      createElement: () => ({ click() { downloads.push({ href: this.href, filename: this.download }); }, remove() {} }),
      body: { appendChild() {} },
      addEventListener() {},
      execCommand: command => {
        if (command === 'copy' && copyWorks) clipboardWrites.push(elements.diagReport.value);
        return copyWorks;
      },
    },
    window: { isSecureContext: secureContext }, navigator: { clipboard },
    localStorage, Blob,
    URL: { createObjectURL: blob => { blobs.push(blob); return 'blob:test'; }, revokeObjectURL() {} },
    fetch: async (path, options) => {
      requests.push({ path, options });
      return { ok: true, json: async () => structuredClone(path === '/api/status' ? statusResponse : path === '/api/diagnostics' ? reportResponse : currentConfig) };
    },
    setTimeout: (fn, delay) => { timers.push({ fn, delay }); return timers.length; }, clearTimeout() {}, setInterval() {},
  });
  vm.runInContext(script, context);
  return { elements, requests, statusResponse, reportResponse, clipboardWrites, downloads, blobs, localStorage, evaluate: code => vm.runInContext(code, context) };
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

test('every strip renders an animation button with its own one-based number', async () => {
  const app = createUi({ strips: Array.from({ length: 5 }, () => ({ pixelCount: 36, reversed: true })) });
  const rows = [];
  app.elements.strips.appendChild = element => rows.push(element.innerHTML);
  await app.evaluate('boot()');
  assert.equal(rows.length, 5);
  rows.forEach((html, index) => {
    assert.match(html, new RegExp(`onclick="testStripAnimation\\(${index + 1}\\)"`));
    assert.match(html, /Test with animation/);
    assert.match(html, new RegExp(`onclick="testStrip\\(${index + 1}\\)"`));
  });
});

test('single-strip animation applies current form settings in RAM before targeting the requested strip', async () => {
  const app = createUi({ strips: Array.from({ length: 5 }, () => ({ pixelCount: 36, reversed: true })) });
  await app.evaluate('boot()');
  app.elements['pixels-2'].value = '54';
  app.elements.brightness.value = '20';
  app.elements.gamma.value = '1.8';
  app.elements.fadeIn.value = '2.64';
  app.elements.fadeOut.value = '5';
  app.elements.hold.value = '2';
  app.elements.waterfallEnabled.checked = true;
  app.elements.waterfallDirection.value = 'bottom-to-top';
  app.elements.waterfallSpeed.value = '53';
  await app.evaluate('testStripAnimation(3)');
  const writes = app.requests.filter(request => request.options?.method === 'PUT' || request.options?.method === 'POST');
  assert.deepEqual(writes.map(request => request.path), ['/api/config', '/api/animation/test']);
  const applied = JSON.parse(writes[0].options.body);
  assert.equal(applied.strips[2].pixelCount, 54);
  assert.equal(applied.strips[2].reversed, true);
  assert.equal(applied.maxBrightness, 20);
  assert.equal(applied.gamma, 1.8);
  assert.equal(applied.stripFadeInMs, 2640);
  assert.equal(applied.fadeOutMs, 5000);
  assert.equal(applied.holdMs, 2000);
  assert.equal(applied.enableWaterfall, true);
  assert.equal(applied.waterfallDirection, 'bottom-to-top');
  assert.equal(applied.waterfallSpeedPps, 53);
  assert.deepEqual(JSON.parse(writes[1].options.body), { direction: 'left', strip: 3 });
  assert.match(app.elements.diagCapture.textContent, /strip #3/);
  assert.equal(app.requests.some(request => request.path === '/api/save' || request.path === '/api/reset'), false);
});

test('invalid geometry blocks the single-strip animation before any device write', async () => {
  const app = createUi();
  await app.evaluate('boot()');
  app.elements['pixels-0'].value = String(firmwareCapacity + 1);
  await app.evaluate('testStripAnimation(1)');
  assert.equal(app.requests.some(request => request.options?.method === 'PUT' || request.path === '/api/animation/test'), false);
  assert.match(app.elements.notice.textContent, /capacity exceeded/);
});

test('whole-chain animation buttons keep the existing request format', async () => {
  const app = createUi();
  await app.evaluate('boot()');
  await app.evaluate("testAnimation('right')");
  const request = app.requests.find(item => item.path === '/api/animation/test');
  assert.deepEqual(JSON.parse(request.options.body), { direction: 'right' });
  assert.match(app.elements.diagCapture.textContent, /all strips/);
});

test('single-strip progress and the automatically loaded report identify the same strip', async () => {
  const app = createUi();
  await app.evaluate('boot()');
  await app.evaluate('testStripAnimation(1)');
  app.statusResponse.animationTestActive = true;
  app.statusResponse.diagnostics.testStripNumber = 1;
  app.statusResponse.diagnostics.phase = 'FADING_OUT';
  await app.evaluate('refreshStatus()');
  assert.match(app.elements.diagCapture.textContent, /strip #1.*Fade-out/);
  app.statusResponse.animationTestActive = false;
  app.reportResponse.testStripNumber = 1;
  await app.evaluate('refreshStatus()');
  assert.equal(JSON.parse(app.elements.diagReport.value).testStripNumber, 1);
  assert.match(app.elements.diagCapture.textContent, /strip #1/);
  assert.equal(app.elements.diagDownload.disabled, false);
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

test('one diagnostic click preserves the form, avoids config writes, and loads the completed report', async () => {
  const { elements, requests, statusResponse, evaluate, clipboardWrites } = createUi();
  await evaluate('boot()');
  const before = evaluate('JSON.stringify(formConfig())');
  await evaluate('runDiagnostic()');
  assert.equal(requests.filter(request => request.path === '/api/diagnostics/run').length, 1);
  assert.equal(requests.some(request => request.options?.method === 'PUT' || request.path === '/api/save'), false);
  assert.equal(evaluate('JSON.stringify(formConfig())'), before);
  assert.equal(elements.diagCopy.disabled, true);
  assert.match(elements.diagCapture.textContent, /48 seconds/);
  statusResponse.animationTestActive = true;
  statusResponse.diagnostics.phase = 'REPEAT_HIGH';
  await evaluate('refreshStatus()');
  assert.match(elements.diagCapture.textContent, /Steady 80.*repeated frames/);
  await evaluate('refreshDiagnostics()');
  assert.equal(requests.some(request => request.path === '/api/diagnostics'), false);
  await evaluate('markFlicker()');
  assert.equal(requests.some(request => request.path === '/api/diagnostics/mark'), true);
  statusResponse.animationTestActive = false;
  await evaluate('refreshStatus()');
  assert.equal(requests.filter(request => request.path === '/api/diagnostics').length, 1);
  assert.equal(elements.diagCopy.disabled, false);
  assert.equal(JSON.parse(elements.diagReport.value).wireMeasured, false);
  await evaluate('copyDiagnostics()');
  assert.equal(clipboardWrites[0], elements.diagReport.value);
  assert.equal(elements.diagReport.selected, true);
});

test('secure clipboard API copies the loaded report', async () => {
  const copied = [];
  const { elements, evaluate } = createUi({
    secureContext: true, clipboard: { writeText: async text => copied.push(text) },
  });
  await evaluate('boot(); refreshDiagnostics()');
  await evaluate('copyDiagnostics()');
  assert.equal(copied[0], elements.diagReport.value);
});

test('blocked HTTP clipboard leaves the report selected for manual copying', async () => {
  const { elements, evaluate } = createUi({ copyWorks: false });
  await evaluate('refreshDiagnostics()');
  await evaluate('copyDiagnostics()');
  assert.equal(elements.diagReport.selected, true);
  assert.match(elements.notice.textContent, /copy it manually/);
});

test('clearing the RAM report never saves or resets configuration', async () => {
  const { requests, evaluate } = createUi();
  await evaluate('clearDiagnostics()');
  assert.equal(requests[0].path, '/api/diagnostics/clear');
  assert.equal(requests[1].path, '/api/diagnostics');
  assert.equal(requests.some(request => request.path === '/api/save' || request.path === '/api/reset'), false);
});

test('loaded report can be downloaded without any additional device request', async () => {
  const { evaluate, requests, downloads, blobs, elements } = createUi();
  await evaluate('refreshDiagnostics()');
  assert.equal(elements.diagDownload.disabled, false);
  const count = requests.length;
  evaluate('downloadDiagnostics()');
  assert.equal(requests.length, count);
  assert.equal(downloads[0].filename, 'ledbox-0.2.5-capture-1.json');
  assert.equal(await blobs[0].text(), elements.diagReport.value);
});

test('browser restores a cached report and clearly labels it as a saved copy', async () => {
  const first = createUi();
  await first.evaluate('refreshDiagnostics()');
  const second = createUi({ storage: first.localStorage });
  await second.evaluate('boot()');
  assert.equal(second.elements.diagReport.value, first.elements.diagReport.value);
  assert.match(second.elements.diagCapture.textContent, /Saved in this browser/);
  assert.equal(second.elements.diagCopy.disabled, false);
  assert.equal(second.elements.diagDownload.disabled, false);
});

test('unavailable browser storage keeps the loaded report available for download', async () => {
  const storage = {
    getItem() { throw new Error('blocked'); },
    setItem() { throw new Error('blocked'); },
  };
  const { evaluate, elements } = createUi({ storage });
  await evaluate('refreshDiagnostics()');
  assert.match(elements.notice.textContent, /Browser storage unavailable/);
  assert.equal(elements.diagCopy.disabled, false);
  assert.equal(elements.diagDownload.disabled, false);
});

test('missing telemetry is labelled incomplete even when the software fade counter is zero', async () => {
  const { evaluate, elements, reportResponse } = createUi();
  reportResponse.coverage.rmtTelemetryPresent = false;
  await evaluate('refreshDiagnostics()');
  assert.match(elements.diagCapture.textContent, /INCOMPLETE software telemetry/);
  assert.match(elements.diagCapture.className, /bad-text/);
});

test('returning to an already finished diagnostic fetches its frozen device report', async () => {
  const { evaluate, requests, statusResponse } = createUi();
  statusResponse.diagnostics.frozen = true;
  await evaluate('boot()');
  assert.equal(requests.filter(request => request.path === '/api/diagnostics').length, 1);
});

test('low output stages identify each level and whether DATA is being repeated', async () => {
  const { evaluate, elements, statusResponse } = createUi();
  await evaluate('boot()');
  statusResponse.animationTestActive = true;
  for (const level of [1, 2, 3, 4, 5, 6, 7]) {
    statusResponse.diagnostics.phase = `HOLD_${level}`;
    await evaluate('refreshStatus()');
    assert.ok(elements.diagCapture.textContent.includes(`Low ${level} · no refresh`));
    statusResponse.diagnostics.phase = `REPEAT_${level}`;
    await evaluate('refreshStatus()');
    assert.ok(elements.diagCapture.textContent.includes(`Low ${level} · repeated frames`));
  }
});
