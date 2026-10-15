'use strict'

// Side-effect-free diagnostic adapter. Construction observes only the injected
// stream; it never loads bedrock-protocol, opens a file/socket or authenticates.
const AUTH_PACKETS = new Set(['login', 'client_to_server_handshake', 'server_to_client_handshake'])
const SECRET_KEYS = new Set(['auth', 'authentication', 'chain', 'chaindata', 'clientdata',
  'identity', 'identitypublickey', 'privatekey', 'publickey', 'devicecode', 'usercode',
  'cookie', 'cookies', 'credential', 'credentials', 'secret', 'clientsecret'])

function isSecretKey (key) {
  const normalized = key.toLowerCase().replace(/[^a-z0-9]/g, '')
  return SECRET_KEYS.has(normalized) || /^(auth|login|identity)/.test(normalized) || /token|password|authorization|certificate|jwt/.test(normalized)
}

function cleanText (value, budget, limit = 2048) {
  const maximum = Math.max(0, Math.min(limit, budget.characters))
  const text = value.slice(0, maximum)
    .replace(/\beyJ[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]*){0,2}/g, '[Redacted JWT]')
    .replace(/\bBearer\s+[^\s,;]+/gi, 'Bearer [Redacted]')
    .replace(/\b(access[_-]?token|refresh[_-]?token|id[_-]?token|password|client[_-]?secret|authorization)\s*[:=]\s*[^\s;&]+/gi, '$1=[Redacted]')
  budget.characters -= maximum
  return text + (value.length > maximum ? '[Truncated]' : '')
}

function sanitizeDiagnosticFields (fields, maximumCharacters = 8192) {
  const budget = { characters: maximumCharacters, nodes: 2048 }
  const seen = new WeakSet()
  const packet = String(fields?.name || '').replace(/^packet_/, '').toLowerCase()
  function visit (value, depth) {
    if (--budget.nodes < 0 || budget.characters <= 0) return '[Budget exceeded]'
    if (value === null || value === undefined) return value
    if (typeof value === 'bigint') return cleanText(value.toString(), budget, 256)
    if (typeof value === 'string') return cleanText(value, budget)
    if (typeof value === 'number' || typeof value === 'boolean') return value
    if (Buffer.isBuffer(value)) return { __type: 'Buffer', length: value.length }
    if (typeof value !== 'object') return `[${typeof value}]`
    if (depth >= 8) return '[MaxDepth]'
    if (seen.has(value)) return '[Circular]'
    seen.add(value)
    if (Array.isArray(value)) {
      const result = []
      for (let index = 0; index < Math.min(value.length, 160) && budget.nodes > 0 && budget.characters > 0; ++index) {
        result.push(visit(value[index], depth + 1))
      }
      if (result.length < value.length) result.push('[Truncated items]')
      return result
    }
    const result = Object.create(null)
    const keys = Object.keys(value)
    for (const key of keys.slice(0, 220)) {
      if (budget.nodes <= 0 || budget.characters <= 0) { result.__truncated = true; break }
      const label = cleanText(key, budget, 128)
      if (isSecretKey(key) || (depth === 0 && (key === 'params' || key === 'data') && AUTH_PACKETS.has(packet))) {
        result[label] = '[Redacted]'
        continue // Never inspect a login payload or credential getter.
      }
      try { result[label] = visit(value[key], depth + 1) } catch (_) { result[label] = '[Unreadable]' }
    }
    if (keys.length > 220) result.__truncatedKeys = keys.length - 220
    return result
  }
  return visit(fields, 0)
}

function createCaptureSink (stream, options = {}) {
  const maxBytes = options.maxBytes ?? 16 * 1024 * 1024
  const maxRecordBytes = options.maxRecordBytes ?? 64 * 1024
  if (!Number.isSafeInteger(maxBytes) || maxBytes < 128 ||
      !Number.isSafeInteger(maxRecordBytes) || maxRecordBytes < 128) throw new TypeError('Invalid capture byte limits')
  let blocked = false, ending = false, failed = false, finished = false, limited = false, streamClosed = false
  let acceptedRecords = 0, submittedBytes = 0, droppedRecords = 0
  let completion = null
  const closeObservers = []
  let failureError = null
  const now = options.now || (() => new Date().toISOString())
  const stats = () => ({ acceptedRecords, submittedBytes, droppedRecords, blocked, ending, failed, limited })
  function notify (callback, ...args) { try { callback?.(...args) } catch (_) {} }
  function complete (error) {
    if (!completion) return
    const callback = completion; completion = null
    notify(callback, error, stats())
  }
  function fail (error) {
    if (failed) return
    failed = true; ending = true; failureError = error
    notify(options.onError, error, stats())
    complete(error)
    try { if (!stream.destroyed) stream.destroy() } catch (_) {}
  }
  // Async open/write errors must be observed before the first record. Keep
  // this listener after close too, so a late stream error cannot kill relay.
  stream.on('error', fail)
  stream.on('drain', () => { if (!ending && !failed) blocked = false })
  stream.on('finish', () => { finished = true })
  stream.on('close', () => {
    streamClosed = true
    if (!finished && !failed) fail(new Error('Capture stream closed before finishing'))
    else if (!failed) complete(null)
    for (const resolve of closeObservers.splice(0)) resolve({ failed, error: failureError })
  })
  function encode (type, fields, extra = {}) {
    return JSON.stringify({ ...sanitizeDiagnosticFields(fields, Math.min(8192, maxRecordBytes / 8)),
      ...extra, ts: now(), type: String(type).slice(0, 128) }) + '\n'
  }
  function close (type = 'capture-complete', fields = {}, callback) {
    if (ending || failed) {
      if (failed || streamClosed) notify(callback, failed ? new Error('Capture failed') : null, stats())
      else if (callback) {
        const previous = completion
        completion = (error, state) => { notify(previous, error, state); notify(callback, error, state) }
      }
      return false
    }
    ending = true; completion = callback || null
    try {
      // At most one bounded final record can follow a backpressured record.
      // No application packet is paused or retained in a userland queue.
      const line = encode(type, fields, { capture: stats() })
      const bytes = Buffer.byteLength(line)
      if (bytes <= maxRecordBytes && submittedBytes + bytes <= maxBytes) {
        ++acceptedRecords; submittedBytes += bytes
        stream.end(line)
      } else stream.end()
      return true
    } catch (error) { fail(error); return false }
  }
  function write (type, fields = {}) {
    if (ending || failed) return false
    if (blocked) { ++droppedRecords; return false }
    try {
      const line = encode(type, fields)
      const bytes = Buffer.byteLength(line)
      if (bytes > maxRecordBytes) { ++droppedRecords; return false }
      if (submittedBytes + bytes > maxBytes) {
        limited = true; ++droppedRecords
        close('capture-limit', { reason: 'maximum-capture-bytes' })
        notify(options.onLimit, stats())
        return false
      }
      const ready = stream.write(line)
      if (failed) return false
      ++acceptedRecords; submittedBytes += bytes; blocked = !ready
      return true
    } catch (error) { fail(error); return false }
  }
  function whenClosed () {
    return streamClosed ? Promise.resolve({ failed, error: failureError }) : new Promise(resolve => closeObservers.push(resolve))
  }
  return { write, close, stats, whenClosed }
}

function openCaptureSink (fs, file, options = {}) {
  const configured = {
    onError: error => console.error('Capture logging stopped:', sanitizeDiagnosticFields(String(error?.message || error))),
    onLimit: state => console.warn('Capture byte limit reached; diagnostic records stopped.', state),
    ...options
  }
  let stream
  try {
    if (options.directory) fs.mkdirSync(options.directory, { recursive: true })
    stream = fs.createWriteStream(file, { flags: 'wx', highWaterMark: 64 * 1024 })
    return createCaptureSink(stream, configured)
  }
  catch (error) {
    if (stream) { try { stream.on('error', () => {}); stream.destroy() } catch (_) {} }
    try { configured.onError?.(error) } catch (_) {}
    const stats = () => ({ failed: true, ending: true, acceptedRecords: 0, submittedBytes: 0, droppedRecords: 0 })
    return { write: () => false, close: (_type, _fields, callback) => { callback?.(error, stats()); return false },
      stats, whenClosed: () => Promise.resolve({ failed: true, error }) }
  }
}

module.exports = { createCaptureSink, openCaptureSink, sanitizeDiagnosticFields }
