// Import only dependency-injected runtime modules. No config/.env, pg, Discord,
// production entrypoint, real child, network, Startup or updater is loaded.
import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { Watchdog } from '../.offline-tests/runtime/watchdogCore.js';
import { LeaseElectorCore } from '../.offline-tests/runtime/leaseCore.js';
import { resolveNpmInvocation } from '../.offline-tests/runtime/npmRuntime.js';
import { removeConfirmedStaleSocket } from '../.offline-tests/runtime/staleSocket.js';
import { createStartupPlan, installStartupPlan, removeStartupPlan } from '../.offline-tests/runtime/startupPlan.js';
import { archiveJsonLines, cursorRows, publishArchive, ARCHIVE_BATCH_ROWS } from '../.offline-tests/runtime/archiveCore.js';
import { Writable } from 'node:stream';
import { gunzipSync } from 'node:zlib';

const flush = async () => { for (let i = 0; i < 30; ++i) await Promise.resolve(); };
class Clock {
  time = 0; serial = 0; timers = new Map();
  now = () => this.time;
  schedule = (callback, milliseconds) => {
    const timer = { id: ++this.serial, refed: true, unref() { this.refed = false; } };
    this.timers.set(timer, { at: this.time + milliseconds, callback }); return timer;
  };
  cancel = timer => this.timers.delete(timer);
  async advance(milliseconds) {
    const target = this.time + milliseconds;
    for (let guard = 0; guard < 10000; ++guard) {
      const first = [...this.timers.entries()].filter(([, value]) => value.at <= target).sort((a, b) => a[1].at - b[1].at)[0];
      if (!first) { this.time = target; await flush(); return; }
      this.time = Math.max(this.time, first[1].at); this.timers.delete(first[0]); first[1].callback(); await flush();
    }
    throw Error('Fake-clock loop exceeded its bounded work budget');
  }
}
class Child extends EventEmitter {
  pid = 123; connected = false; kills = []; messages = []; killed = false;
  kill(signal) { this.killed = true; this.kills.push(signal); return true; }
  send(message) { this.messages.push(message); }
}
function supervision() {
  const clock = new Clock(), children = [], exits = [];
  const watchdog = new Watchdog({ ...clock, now: clock.now, schedule: clock.schedule, cancel: clock.cancel,
    spawn() { const child = new Child(); children.push(child); return child; }, write() {}, exit: code => exits.push(code) });
  watchdog.start(); return { clock, children, exits, watchdog };
}
test('watchdog keeps the crash restart alive and deduplicates error/exit', async () => {
  const f = supervision(); f.children[0].emit('exit', 1);
  assert.equal(f.clock.timers.size, 1); assert.ok([...f.clock.timers.keys()][0].refed);
  await f.clock.advance(1999); assert.equal(f.children.length, 1);
  await f.clock.advance(1); assert.equal(f.children.length, 2); assert.deepEqual(f.exits, []);
  f.children[1].pid = undefined; f.children[1].emit('error', Error('fake spawn failure')); f.children[1].emit('exit', 1);
  assert.equal(f.clock.timers.size, 1); f.watchdog.stop('test'); assert.deepEqual(f.exits, [0]);
});
test('watchdog clean exit and self-update transfer never restart', () => {
  for (const code of [0, 75]) { const f = supervision(); f.children[0].emit('exit', code); assert.deepEqual(f.exits, [0]); assert.equal(f.clock.timers.size, 0); }
});
test('watchdog escalates based on actual exit, not killed flag', async () => {
  const f = supervision(); f.watchdog.stop('test'); assert.deepEqual(f.children[0].kills, ['SIGTERM']);
  await f.clock.advance(10000); assert.deepEqual(f.children[0].kills, ['SIGTERM', 'SIGKILL']);
  f.children[0].emit('exit', null); assert.deepEqual(f.exits, [0]); assert.equal(f.clock.timers.size, 0);
});
test('watchdog IPC requests graceful cleanup and cancels forced termination after exit', async () => {
  const f = supervision(); f.children[0].connected = true; f.watchdog.stop('test');
  assert.deepEqual(f.children[0].messages, [{ type: 'nifibot-shutdown' }]); assert.deepEqual(f.children[0].kills, []);
  f.children[0].emit('exit', 0); await f.clock.advance(10000); assert.deepEqual(f.children[0].kills, []); assert.deepEqual(f.exits, [0]);
});
test('a signal error on a live child does not create a second bot', async () => {
  const f = supervision(); f.children[0].emit('error', Error('fake signal error'));
  await f.clock.advance(60000); assert.equal(f.children.length, 1); assert.equal(f.clock.timers.size, 0);
  f.children[0].emit('exit', 0);
});
test('watchdog gives up after bounded consecutive failures', async () => {
  const f = supervision();
  for (let i = 0; i < 11; ++i) { f.children.at(-1).emit('exit', 1); if (i < 10) await f.clock.advance([2000, 5000, 10000, 30000, 60000][Math.min(i, 4)]); }
  assert.deepEqual(f.exits, [1]); assert.equal(f.children.length, 11);
});

const options = { enabled: true, renewMs: 100, failSafeMs: 1000, standbyMs: 50, operationMs: 200 };
function leaseFixture(overrides = {}) {
  const clock = new Clock(), released = []; let first = true;
  const store = { async acquire() { if (first) { first = false; return { epoch: '1' }; } return null; },
    async renew() { return true; }, async release(epoch) { released.push(epoch); }, ...overrides };
  const core = new LeaseElectorCore(store, options, clock); return { clock, store, core, released };
}
test('independent deadline destroys ownership while login is hung', async () => {
  const f = leaseFixture(); let signal, losses = 0;
  const run = f.core.run(async value => { signal = value; return new Promise(() => {}); }, async () => { ++losses; });
  await flush(); assert.ok(f.core.isLeader());
  await f.clock.advance(1000); assert.equal(f.core.isLeader(), false); assert.ok(signal.aborted); assert.equal(losses, 1);
  f.core.stop(); await run; assert.deepEqual(f.released, ['1']);
});
test('hung renewals cannot defer the independent lease deadline', async () => {
  const f = leaseFixture({ renew: () => new Promise(() => {}) }); let losses = 0;
  const run = f.core.run(async () => {}, async () => { ++losses; }); await flush();
  await f.clock.advance(1000); assert.equal(f.core.isLeader(), false); assert.equal(losses, 1);
  f.core.stop(); await run;
});
test('late renewal completion cannot resurrect expired ownership', async () => {
  let resolve; const f = leaseFixture({ renew: () => new Promise(value => { resolve = value; }) });
  const run = f.core.run(async () => {}, async () => {}); await flush(); await f.clock.advance(100);
  await f.clock.advance(1000); resolve(true); await flush(); assert.equal(f.core.isLeader(), false); assert.equal(f.core.getEpoch(), null);
  f.core.stop(); await run;
});
test('late acquire is released without starting any gateway', async () => {
  let resolve, acquired = 0, attempts = 0; const releases = [];
  const f = leaseFixture({ acquire: () => ++attempts === 1 ? new Promise(value => { resolve = value; }) : Promise.resolve(null),
    release: async epoch => { releases.push(epoch); } });
  const run = f.core.run(async () => { ++acquired; }, async () => {}); await flush(); await f.clock.advance(201);
  resolve({ epoch: 'late' }); await flush(); assert.equal(acquired, 0); assert.deepEqual(releases, ['late']);
  f.core.stop(); await run;
});
test('successful renewals extend from request start and stop cancels ownership', async () => {
  const f = leaseFixture(); let losses = 0;
  const run = f.core.run(async () => {}, async () => { ++losses; }); await flush(); await f.clock.advance(1500);
  assert.ok(f.core.isLeader()); assert.equal(losses, 0); f.core.stop(); await run;
  assert.equal(losses, 1); assert.equal(f.core.isLeader(), false); assert.equal(f.clock.timers.size, 0);
});
test('a monotonic jump observed before timer dispatch rejects an expired renewal', async () => {
  let resolve; const f = leaseFixture({ renew: () => new Promise(value => { resolve = value; }) });
  const run = f.core.run(async () => {}, async () => {}); await flush(); await f.clock.advance(100);
  f.clock.time = 1200; resolve(true); await flush(); assert.equal(f.core.isLeader(), false);
  f.core.stop(); await run;
});
test('shutdown aborts a pending login immediately without waiting for its promise', async () => {
  const f = leaseFixture(); let signal;
  const run = f.core.run(async value => { signal = value; return new Promise(() => {}); }, async () => {}); await flush();
  f.core.stop(); await run; assert.ok(signal.aborted); assert.equal(f.clock.timers.size, 0);
});

test('direct Windows launch resolves installed npm without npm environment', () => {
  const expected = 'C:\\Program Files\\nodejs\\node_modules\\npm\\bin\\npm-cli.js';
  const value = resolveNpmInvocation('win32', 'C:\\Program Files\\nodejs\\node.exe', {}, candidate => candidate === expected, ['run', 'build']);
  assert.deepEqual(value, { file: 'C:\\Program Files\\nodejs\\node.exe', args: [expected, 'run', 'build'] });
});
test('npm resolution supports explicit/roaming installations and fails clearly when absent', () => {
  const explicit = 'D:\\tools\\npm-cli.js';
  assert.equal(resolveNpmInvocation('win32', 'C:\\node.exe', { NIFIBOT_NPM_CLI: explicit }, value => value === explicit, ['ci']).args[0], explicit);
  const roaming = 'C:\\profile\\npm\\node_modules\\npm\\bin\\npm-cli.js';
  assert.equal(resolveNpmInvocation('win32', 'C:\\node.exe', { APPDATA: 'C:\\profile', npm_execpath: 'missing' }, value => value === roaming, ['ci']).args[0], roaming);
  assert.throws(() => resolveNpmInvocation('win32', 'C:\\node.exe', {}, () => false, []), /NIFIBOT_NPM_CLI/);
  assert.deepEqual(resolveNpmInvocation('linux', '/bin/node', {}, () => false, ['ci']), { file: 'npm', args: ['ci'] });
});
test('stale socket recovery never removes active/uncertain/replaced/non-socket paths', async () => {
  for (const state of ['active', 'unknown', 'refused']) for (const changed of [false, true]) {
    let stats = 0, removed = 0;
    const result = await removeConfirmedStaleSocket({ stat: async () => ({ socket: true, device: 1, inode: ++stats > 1 && changed ? 2 : 1 }),
      probe: async () => state, unlink: async () => { ++removed; } });
    assert.equal(removed, state === 'refused' && !changed ? 1 : 0); assert.equal(result, removed === 1);
  }
  let probes = 0;
  assert.equal(await removeConfirmedStaleSocket({ stat: async () => ({ socket: false, device: 1, inode: 1 }),
    probe: async () => { ++probes; return 'refused'; }, unlink: async () => assert.fail('must not unlink a regular file') }), false);
  assert.equal(probes, 0);
});
test('voice standby preparation is bounded and aborted before any lease is acquired', async () => {
  const clock = new Clock(); let acquired = 0, signal, destroyed = 0;
  const core = new LeaseElectorCore({ acquire: async () => { ++acquired; return null; }, renew: async () => true, release: async () => {} },
    { ...options, prepareMs: 300, beforeAcquire: async value => { signal = value; value.addEventListener('abort', () => ++destroyed); return new Promise(() => {}); } }, clock);
  const run = core.run(async () => {}, async () => {}); await flush(); await clock.advance(300);
  assert.ok(signal.aborted); assert.equal(destroyed, 1); assert.equal(acquired, 0);
  core.stop(); await run; assert.equal(clock.timers.size, 0);
});
test('voice refresh/join work cannot outlive the independent ownership deadline', async () => {
  const f = leaseFixture(); let resolve, signal, losses = 0, joins = 0;
  const run = f.core.run(async () => {}, async () => { ++losses; }, async value => {
    signal = value; await new Promise(ready => { resolve = ready; }); if (!value.aborted) ++joins;
  });
  await flush(); await f.clock.advance(100); assert.ok(signal && !signal.aborted);
  await f.clock.advance(1000); assert.ok(signal.aborted); assert.equal(losses, 1);
  resolve(); await flush(); assert.equal(joins, 0); assert.equal(f.core.isLeader(), false);
  f.core.stop(); await run;
});
test('voice no-target preparation stays standby without acquiring or joining', async () => {
  const clock = new Clock(); let acquired = 0, joins = 0;
  const core = new LeaseElectorCore({ acquire: async () => { ++acquired; return null; }, renew: async () => true, release: async () => {} },
    { ...options, beforeAcquire: async () => false }, clock);
  const run = core.run(async () => { ++joins; }, async () => {}); await flush(); await clock.advance(300);
  assert.equal(acquired, 0); assert.equal(joins, 0); core.stop(); await run;
});
test('Startup filenames distinguish punctuation/case/truncation and project copies', () => {
  const ids = ['A/B', 'A?B', 'a/b', 'α', 'β', 'X'.repeat(100) + 'A', 'X'.repeat(100) + 'B'];
  const names = ids.map(id => createStartupPlan(id, 'C:\\bot', 'C:\\node.exe').targetName.toLowerCase());
  assert.equal(new Set(names).size, ids.length);
  assert.notEqual(createStartupPlan('node', 'C:\\bot', 'C:\\node.exe').targetName, createStartupPlan('node', 'D:\\bot', 'C:\\node.exe').targetName);
  assert.equal(createStartupPlan('node', 'C:\\BOT\\', 'C:\\old-node.exe').targetName, createStartupPlan('node', 'c:\\bot', 'C:\\new-node.exe').targetName);
});
test('Startup batch supports percent/exclamation/metacharacters/Unicode without embedding NODE_ID', () => {
  const plan = createStartupPlan('node & unexpected-command', 'C:\\Test %USERPROFILE% !bang! & caret^ (α)', 'C:\\Node %SDK%\\node.exe');
  assert.match(plan.contents, /DisableDelayedExpansion/); assert.match(plan.contents, /chcp 65001/);
  assert.ok(plan.contents.includes('%%USERPROFILE%%')); assert.ok(plan.contents.includes('%%SDK%%'));
  assert.ok(plan.contents.includes('!bang! & caret^ (α)')); assert.ok(!plan.contents.includes('unexpected-command'));
});
function startupStore(files = new Map()) {
  const operations = [];
  return { files, operations, read: async name => files.get(name) ?? null,
    writeAtomic: async (name, contents) => { operations.push('write'); files.set(name, contents); },
    remove: async name => { operations.push('remove'); files.delete(name); } };
}
test('Startup install migrates only an exact owned legacy entry after successful publication', async () => {
  const plan = createStartupPlan('node', 'C:\\bot', 'C:\\node.exe'), store = startupStore(new Map([[plan.legacyName, plan.legacyContents]]));
  await installStartupPlan(plan, store); assert.deepEqual(store.operations, ['write', 'remove']);
  assert.equal(store.files.get(plan.targetName), plan.contents); assert.ok(!store.files.has(plan.legacyName));
  assert.ok(await removeStartupPlan(plan, store)); assert.ok(!store.files.has(plan.targetName));
});
test('Startup foreign entries and colliding legacy launchers are preserved', async () => {
  const a = createStartupPlan('A/B', 'C:\\bot', 'C:\\node.exe'), b = createStartupPlan('A?B', 'C:\\bot', 'C:\\node.exe');
  assert.equal(a.legacyName, b.legacyName);
  const store = startupStore(new Map([[a.legacyName, b.legacyContents]]));
  await installStartupPlan(a, store); assert.equal(store.files.get(a.legacyName), b.legacyContents);
  await removeStartupPlan(a, store); assert.equal(store.files.get(a.legacyName), b.legacyContents);
  store.files.set(a.targetName, 'unrelated launcher');
  await assert.rejects(installStartupPlan(a, store), /another entry/);
  assert.equal(await removeStartupPlan(a, store), false); assert.equal(store.files.get(a.targetName), 'unrelated launcher');
});
test('failed Startup publication preserves the existing legacy launcher', async () => {
  const plan = createStartupPlan('node', 'C:\\bot', 'C:\\node.exe'), store = startupStore(new Map([[plan.legacyName, plan.legacyContents]]));
  store.writeAtomic = async () => { throw Error('fake disk failure'); };
  await assert.rejects(installStartupPlan(plan, store), /fake disk failure/);
  assert.equal(store.files.get(plan.legacyName), plan.legacyContents); assert.ok(!store.files.has(plan.targetName));
});
test('archive cursor fetches one bounded page at a time without preloading', async () => {
  let calls = 0;
  const rows = cursorRows(async sql => {
    assert.equal(sql, `FETCH FORWARD ${ARCHIVE_BATCH_ROWS} FROM archive_events`); ++calls;
    return { rows: calls <= 2 ? Array.from({ length: ARCHIVE_BATCH_ROWS }, (_, id) => ({ id })) : [] };
  }, 'archive_events');
  assert.equal(calls, 0); await rows.next(); assert.equal(calls, 1);
  for (let i = 1; i < ARCHIVE_BATCH_ROWS; ++i) await rows.next(); assert.equal(calls, 1);
  await rows.next(); assert.equal(calls, 2); await rows.return();
});
function archiveOutput({ failWrite = false, failCommit = false } = {}) {
  const files = new Map([['final', Buffer.from('previous archive')]]), operations = [];
  return { files, operations, create(name) {
    const chunks = []; return new Writable({ write(chunk, _encoding, callback) {
      if (failWrite) { callback(Error('fake output failure')); return; } chunks.push(Buffer.from(chunk)); callback();
    }, final(callback) { files.set(name, Buffer.concat(chunks)); callback(); } });
  }, async commitSnapshot() { operations.push('commit'); if (failCommit) throw Error('fake commit failure'); },
  async rename(from, to) { operations.push('rename'); files.set(to, files.get(from)); files.delete(from); },
  async remove(name) { operations.push('remove'); files.delete(name); } };
}
test('archive publication streams valid JSONL and atomically replaces only after snapshot commit', async () => {
  const events = (async function* () { for (let id = 0; id < 1000; ++id) yield { id }; })();
  const messages = (async function* () { yield { message_id: 'message' }; })();
  const output = archiveOutput();
  await publishArchive(archiveJsonLines({ recordType: 'archive_metadata', eventCount: 1000, messageCount: 1 }, events, messages), 'temporary', 'final', output);
  const records = gunzipSync(output.files.get('final')).toString('utf8').trimEnd().split('\n').map(line => JSON.parse(line));
  assert.equal(records.length, 1002); assert.equal(records[0].eventCount, 1000); assert.equal(records[1].recordType, 'event');
  assert.equal(records.at(-1).recordType, 'message'); assert.deepEqual(output.operations, ['commit', 'rename']);
});
test('archive write/commit failures leave an existing published archive intact', async () => {
  for (const options of [{ failWrite: true }, { failCommit: true }]) {
    const output = archiveOutput(options);
    await assert.rejects(publishArchive((async function* () { yield 'row\n'; })(), 'temporary', 'final', output), /fake/);
    assert.equal(output.files.get('final').toString(), 'previous archive'); assert.ok(!output.files.has('temporary'));
    assert.ok(!output.operations.includes('rename'));
  }
});
