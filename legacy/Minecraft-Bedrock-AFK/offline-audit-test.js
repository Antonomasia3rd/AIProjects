'use strict'

// Only Node builtins + the two local side-effect-free modules below. No bot,
// relay, config, network/auth libraries, filesystem operations or timers run.
const assert = require('node:assert/strict')
const { EventEmitter } = require('node:events')
const World = require('./world')
const { createCaptureSink, openCaptureSink, sanitizeDiagnosticFields } = require('./relay-capture')
const { createShutdownCoordinator, installShutdownSignals } = require('./relay-shutdown')
let checks = 0
function check (value, label) { ++checks; assert.ok(value, label) }

function testTransactionalChunk () {
  let constructions = 0, constructorFailure = false
  class FakeVec3 { constructor (x, y, z) { Object.assign(this, { x, y, z }) } }
  class FakeColumn {
    constructor (position) {
      ++constructions
      if (constructorFailure) throw new Error('synthetic constructor failure')
      this.position = position; this.value = 'empty'
    }
    networkDecodeNoCache (payload) {
      this.value = 'partially decoded'
      if (payload.fail) throw new Error('synthetic malformed chunk')
      this.value = payload.value
    }
    getBlock (position) { return { name: `minecraft:${this.value}`, position } }
  }
  const world = new World({ createRegistry: () => ({ version: { majorVersion: 'test' } }),
    createChunkColumn: () => FakeColumn, Vec3: FakeVec3 })
  const packet = (x, payload, sub_chunk_count = 1) => ({ x, z: 2, sub_chunk_count, payload })
  check(world.loadLevelChunk(packet(1, { value: 'stone' })), 'valid initial chunk is published')
  const original = world.columns.get('1,2')
  const savedError = console.error; const errors = []
  console.error = (...args) => errors.push(args)
  try {
    check(!world.loadLevelChunk(packet(1, { fail: true })) && world.columns.get('1,2') === original &&
      world.blockAt(16, 0, 32) === 'stone' && world.loadedChunks === 1, 'failed replacement preserves readable old geometry and successful decode count')
    check(!world.loadLevelChunk(packet(9, { fail: true })) && !world.columns.has('9,2'), 'failed first decode cannot publish partially initialized terrain')
    const beforeSentinel = constructions
    check(!world.loadLevelChunk(packet(1, {}, -1)) && !world.loadLevelChunk(packet(1, {}, -2)) &&
      constructions === beforeSentinel && world.columns.get('1,2') === original, 'unsupported negative chunk sentinels preserve cache without allocation')
    constructorFailure = true
    check(!world.loadLevelChunk(packet(1, {})) && world.columns.get('1,2') === original, 'constructor failures preserve the old column')
    constructorFailure = false
  } finally { console.error = savedError }
  check(errors.length === 3, 'decode/acquisition failures remain observable without leaking fake data')
  check(world.loadLevelChunk(packet(1, { value: 'dirt' })) && world.columns.get('1,2') !== original &&
    world.blockAt(16, 0, 32) === 'dirt' && world.loadedChunks === 2, 'successful replacement commits atomically after decoding')
}

class FakeStream extends EventEmitter {
  constructor (options = {}) {
    super(); this.ready = true; this.lines = []; this.destroyed = false
    Object.assign(this, options)
  }
  write (line) {
    if (this.writeError) throw this.writeError
    this.lines.push(line)
    return this.ready
  }
  end (line) {
    if (line) this.lines.push(line)
    this.ended = true
    if (!this.holdClose) { this.emit('finish'); this.emit('close') }
  }
  destroy () { this.destroyed = true; if (!this.holdDestroy) this.emit('close') }
}

function testCaptureLifecycle () {
  const stream = new FakeStream({ ready: false, holdClose: true })
  const sink = createCaptureSink(stream, { now: () => 'fixture-clock' })
  check(sink.write('packet', { name: 'respawn', params: { state: 1 } }), 'backpressured write is counted as accepted exactly once')
  for (let record = 0; record < 10000; ++record) sink.write('packet', { name: 'player_auth_input', params: { tick: record } })
  check(stream.lines.length === 1 && sink.stats().droppedRecords === 10000, 'ten thousand blocked capture records create no userland or stream queue')
  stream.ready = true; stream.emit('drain')
  check(sink.write('packet', { name: 'respawn', params: { state: 2 } }) && stream.lines.length === 2, 'drain resumes diagnostics without replaying dropped packets')
  let completed = 0
  sink.close('capture-complete', {}, error => { assert.equal(error, null); ++completed })
  sink.close('capture-complete', {}, error => { assert.equal(error, null); ++completed })
  check(completed === 0 && stream.lines.length === 3, 'close has one bounded summary and does not claim release before stream close')
  stream.emit('finish')
  check(completed === 0, 'finish alone does not claim the underlying file has closed')
  stream.emit('close')
  check(completed === 2 && JSON.parse(stream.lines[2]).capture.droppedRecords === 10000 &&
    !sink.write('packet', {}), 'close completes each observer once and records capture loss honestly')
  const blocked = new FakeStream({ ready: false })
  const blockedSink = createCaptureSink(blocked)
  blockedSink.write('first', {})
  blockedSink.close()
  check(blocked.lines.length === 2 && blocked.ended, 'ending a blocked stream appends at most one bounded completion record')
  let errors = 0
  const failed = new FakeStream()
  const failedSink = createCaptureSink(failed, { onError: () => { ++errors } })
  check(failed.listenerCount('error') === 1, 'async stream error listener exists before the first record')
  failed.emit('error', new Error('synthetic open denied'))
  failed.emit('error', new Error('synthetic late failure'))
  check(errors === 1 && failed.destroyed && failedSink.stats().failed && !failedSink.write('packet', {}) && failed.lines.length === 0,
    'async open/write failures stop capture and late errors remain handled')
  const thrown = new FakeStream({ writeError: new Error('synthetic write failure') })
  const thrownSink = createCaptureSink(thrown)
  check(!thrownSink.write('packet', {}) && thrownSink.stats().failed && thrown.destroyed, 'synchronous write failure is contained in capture')
  const earlyClose = new FakeStream()
  const earlySink = createCaptureSink(earlyClose)
  earlyClose.emit('close')
  check(earlySink.stats().failed, 'premature stream close cannot be reported as successful capture')
  const bounded = new FakeStream()
  const boundedSink = createCaptureSink(bounded, { maxBytes: 1024, maxRecordBytes: 1024 })
  for (let index = 0; index < 100; ++index) boundedSink.write('packet', { value: 'x'.repeat(200) })
  check(boundedSink.stats().limited && bounded.ended && bounded.lines.reduce((sum, line) => sum + Buffer.byteLength(line), 0) <= 1024,
    'total capture bytes are bounded even when packets continue after the limit')
  const oversized = new FakeStream()
  const oversizedSink = createCaptureSink(oversized, { maxRecordBytes: 128 })
  check(!oversizedSink.write('x'.repeat(512), {}) && oversized.lines.length === 0 && oversizedSink.stats().droppedRecords === 1,
    'an oversized individual record is dropped before reaching the stream')
  oversizedSink.close()
}

function testRedaction () {
  let loginReads = 0
  const raw = { name: 'login', get params () { ++loginReads; return { jwt: 'SYNTHETIC_LOGIN_SECRET' } } }
  const redacted = sanitizeDiagnosticFields(raw)
  check(loginReads === 0 && redacted.params === '[Redacted]', 'login payload is omitted without inspecting authentication data')
  const preload = sanitizeDiagnosticFields({ name: 'login', get data () { ++loginReads; return 'SYNTHETIC_LOGIN_SECRET' } })
  check(loginReads === 0 && preload.data === '[Redacted]', 'preload data-shaped login payloads use the same redaction without reading them')
  const packet = { name: 'player_auth_input', params: { tick: 42n, input_data: ['up', 'sprint'],
    access_token: 'SYNTHETIC_ACCESS_SECRET', auth_data: 'SYNTHETIC_AUTH_SECRET',
    nested: { authorization: 'Bearer SYNTHETIC_BEARER_SECRET', identity_data: 'SYNTHETIC_ID_SECRET' },
    bytes: Buffer.from('SYNTHETIC_BUFFER_SECRET') } }
  const clean = sanitizeDiagnosticFields(packet)
  const encoded = JSON.stringify(clean)
  check(clean.params.tick === '42' && clean.params.input_data[1] === 'sprint' && !encoded.includes('SYNTHETIC'),
    'gameplay auth-input metadata survives while credential fields and binary contents are omitted')
  check(packet.params.access_token === 'SYNTHETIC_ACCESS_SECRET' && packet.params.input_data[1] === 'sprint',
    'redaction copies diagnostics without mutating forwarded protocol objects')
  const message = sanitizeDiagnosticFields({ error: 'Bearer secret-token and eyJhbGciOiJub25lIn0.eyJzdWIiOiJTRUNSRVQifQ.signature and access_token=secret-query' }).error
  check(!message.includes('secret-token') && !message.includes('eyJ') && !message.includes('secret-query'), 'common inline bearer/JWT/query secrets are redacted in diagnostic errors')
  const recursive = { entries: Array.from({ length: 1000 }, () => 'x'.repeat(10000)) }; recursive.self = recursive
  check(JSON.stringify(sanitizeDiagnosticFields(recursive)).length < 20000, 'recursive or large diagnostic data has a shared traversal/string budget')
  const stream = new FakeStream()
  const sink = createCaptureSink(stream)
  sink.write('serverbound', raw)
  check(loginReads === 0 && !stream.lines[0].includes('SYNTHETIC'), 'actual capture writer applies login redaction before serialization')
  sink.close()
}

async function flushMicrotasks () { for (let index = 0; index < 6; ++index) await Promise.resolve() }
function deferred () { let resolve, reject; const promise = new Promise((yes, no) => { resolve = yes; reject = no }); return { promise, resolve, reject } }
function fakeClock () {
  const timers = new Map(); let next = 0
  return { setTimer: callback => { timers.set(++next, callback); return next },
    clearTimer: id => timers.delete(id), timers,
    expire: () => { const callbacks = [...timers.values()]; timers.clear(); for (const callback of callbacks) callback() } }
}
async function testFileCreationAndPhysicalClose () {
  let errors = 0, created = 0
  const failedDirectory = openCaptureSink({ mkdirSync () { throw new Error('synthetic directory denial') },
    createWriteStream () { ++created } }, 'unused', { directory: 'fake-only', onError: () => { ++errors } })
  check((await failedDirectory.whenClosed()).failed && !failedDirectory.write('packet', {}) && errors === 1 && created === 0,
    'failed diagnostic directory creation disables only capture without attempting file open')
  const failedFile = openCaptureSink({ createWriteStream () { throw new Error('synthetic open failure') } },
    'unused', { onError: () => { ++errors } })
  check((await failedFile.whenClosed()).failed && errors === 2, 'synchronous file creation errors use the same contained failed sink')
  const malformedStream = new FakeStream()
  openCaptureSink({ createWriteStream: () => malformedStream }, 'unused', { maxRecordBytes: 0, onError: () => { ++errors } })
  check(malformedStream.destroyed && malformedStream.listenerCount('error') === 1 && errors === 3,
    'failed sink setup closes its already-created stream and handles late stream errors')
  const pending = new FakeStream({ holdDestroy: true })
  const sink = createCaptureSink(pending)
  let physicallyClosed = false
  const closed = sink.whenClosed().then(state => { physicallyClosed = true; return state })
  pending.emit('error', new Error('synthetic asynchronous disk failure'))
  await flushMicrotasks()
  check(!physicallyClosed && pending.destroyed, 'an I/O error requests destruction but does not claim descriptor closure before close event')
  pending.emit('close')
  check((await closed).failed && physicallyClosed, 'physical close settles observers with the preceding failure preserved')
}
async function testShutdown () {
  const clock = fakeClock(), relay = deferred()
  const stream = new FakeStream({ holdClose: true })
  const sink = createCaptureSink(stream)
  let relayCalls = 0, captureCalls = 0, resolved = false
  const coordinator = createShutdownCoordinator({ ...clock, stopRelay: () => { ++relayCalls; return relay.promise },
    closeCaptures: () => { ++captureCalls; sink.close(); return sink.whenClosed() } })
  const shutdown = coordinator.run()
  check(shutdown === coordinator.run() && coordinator.isClosing(), 'repeated signal requests share one shutdown operation')
  const signals = new EventEmitter(); let signalCalls = 0
  const unsubscribe = installShutdownSignals(signals, () => { ++signalCalls; coordinator.run() })
  signals.emit('SIGINT'); signals.emit('SIGINT'); signals.emit('SIGTERM')
  check(signalCalls === 3 && signals.listenerCount('SIGINT') === 1, 'repeated SIGINT and SIGTERM stay subscribed during the bounded flush')
  unsubscribe()
  check(signals.listenerCount('SIGINT') === 0 && signals.listenerCount('SIGTERM') === 0, 'injected shutdown signal subscriptions can be removed without touching the real process')
  shutdown.then(() => { resolved = true })
  await flushMicrotasks()
  check(relayCalls === 1 && captureCalls === 1 && stream.ended, 'relay stop and capture close each begin once')
  relay.resolve()
  stream.emit('finish')
  await flushMicrotasks()
  check(!resolved, 'a closed relay plus finished capture still waits for the actual capture close event')
  stream.emit('close')
  const result = await shutdown
  check(result.ok && result.relayClosed && result.capturesClosed && !result.timedOut && clock.timers.size === 0,
    'successful shutdown requires both closures and removes the deadline timer')

  const captureClock = fakeClock(), held = deferred()
  const captureTimeout = createShutdownCoordinator({ ...captureClock, timeoutMs: 5, stopRelay: () => {}, closeCaptures: () => held.promise })
  const timed = captureTimeout.run()
  await flushMicrotasks(); captureClock.expire()
  const timeoutResult = await timed
  check(!timeoutResult.ok && timeoutResult.timedOut && timeoutResult.relayClosed && !timeoutResult.capturesClosed,
    'hung capture flush reaches the shared deadline without a false success claim')
  held.reject(new Error('synthetic late capture failure'))
  await flushMicrotasks()
  check(timeoutResult.errors.length === 0 && timeoutResult.timedOut,
    'late completion/rejection is observed without mutating an already-reported timeout')

  const relayClock = fakeClock(), stuckRelay = deferred()
  const relayTimeout = createShutdownCoordinator({ ...relayClock, stopRelay: () => stuckRelay.promise, closeCaptures: () => {} })
  const relayPending = relayTimeout.run()
  await flushMicrotasks(); relayClock.expire()
  const relayResult = await relayPending
  check(relayResult.timedOut && !relayResult.relayClosed && relayResult.capturesClosed,
    'a hung relay cannot prevent the bounded capture/shutdown result')
  stuckRelay.resolve(); await flushMicrotasks()

  const failedClock = fakeClock(); let siblingClosed = false
  const failed = createShutdownCoordinator({ ...failedClock, stopRelay: () => { throw new Error('synthetic relay close failure') },
    closeCaptures: () => { siblingClosed = true } })
  const failedResult = await failed.run()
  check(!failedResult.ok && !failedResult.timedOut && siblingClosed && failedResult.capturesClosed &&
    failedResult.errors[0].operation === 'relay' && failedClock.timers.size === 0,
    'thrown relay errors do not skip sibling capture cleanup or masquerade as a clean exit')
}
async function main () {
  testTransactionalChunk()
  testCaptureLifecycle()
  testRedaction()
  await testFileCreationAndPhysicalClose()
  await testShutdown()
  console.log(`${checks} offline world/capture/shutdown checks passed; no bot, relay, auth, data files or network operations`)
}
main().catch(error => { console.error(error); process.exitCode = 1 })
