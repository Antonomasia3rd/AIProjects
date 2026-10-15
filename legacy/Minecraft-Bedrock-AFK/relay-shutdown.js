'use strict'

// Injectable shutdown policy only. No process, relay, file or signal handlers
// are touched until the owner explicitly calls run(). The deadline is shared
// by relay shutdown and all capture-file closures.
function createShutdownCoordinator ({ stopRelay, closeCaptures, timeoutMs = 5000,
  setTimer = setTimeout, clearTimer = clearTimeout }) {
  if (typeof stopRelay !== 'function' || typeof closeCaptures !== 'function' ||
      !Number.isSafeInteger(timeoutMs) || timeoutMs < 1) throw new TypeError('Invalid shutdown options')
  let pending = null
  function run () {
    if (pending) return pending
    let resolve
    pending = new Promise(done => { resolve = done })
    const completed = { relay: false, captures: false }
    const succeeded = { relay: false, captures: false }
    const errors = []
    let settled = false, timer = null
    function finish (timedOut) {
      if (settled) return
      settled = true
      if (timer !== null) clearTimer(timer)
      resolve({ ok: !timedOut && errors.length === 0, timedOut,
        relayClosed: succeeded.relay, capturesClosed: succeeded.captures, errors: errors.slice() })
    }
    timer = setTimer(() => finish(true), timeoutMs)
    function observe (name, operation) {
      Promise.resolve().then(operation).then(() => {
        completed[name] = succeeded[name] = true
        if (completed.relay && completed.captures) finish(false)
      }, error => {
        completed[name] = true
        errors.push({ operation: name, message: String(error?.message || error) })
        if (completed.relay && completed.captures) finish(false)
      })
    }
    observe('relay', stopRelay)
    observe('captures', closeCaptures)
    return pending
  }
  return { run, isClosing: () => pending !== null }
}

function installShutdownSignals (emitter, handler) {
  // Keep listeners until exit/unsubscribe: removing a once-listener after the
  // first SIGINT would let a second SIGINT bypass the bounded flush entirely.
  emitter.on('SIGINT', handler)
  emitter.on('SIGTERM', handler)
  return () => { emitter.removeListener('SIGINT', handler); emitter.removeListener('SIGTERM', handler) }
}

module.exports = { createShutdownCoordinator, installShutdownSignals }
