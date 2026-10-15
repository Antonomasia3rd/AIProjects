'use strict'

const fs = require('fs')
const path = require('path')

function safeJsonValue (value) {
  try {
    return JSON.parse(JSON.stringify(value, (_key, item) => {
      if (typeof item === 'bigint') return item.toString()
      if (item instanceof Error) {
        return {
          name: item.name,
          message: item.message,
          code: item.code,
          stack: item.stack
        }
      }
      return item
    }))
  } catch {
    return String(value)
  }
}

function packetName (packet) {
  return packet?.data?.name || packet?.name || packet?.packet?.name || packet?.metadata?.name || '<unknown>'
}

function errorSummary (error) {
  if (!error) return null
  return {
    at: Date.now(),
    name: error.name || 'Error',
    message: error.message || String(error),
    code: error.code ?? null,
    errno: error.errno ?? null,
    syscall: error.syscall ?? null,
    stack: error.stack || null
  }
}

function classifyReport (report, now) {
  const local = report.localDisconnect
  if (local) {
    if (local.source === 'respawn-timeout') return 'local-respawn-timeout'
    if (local.source === 'shutdown') return 'local-shutdown'
    return 'local-disconnect'
  }

  const recentKick = report.lastKick && now - report.lastKick.at <= 15000
  if (recentKick) return 'server-kick'

  const recentError = report.lastError && now - report.lastError.at <= 15000
  if (recentError) return 'transport-or-protocol-error'

  const recentViolation = report.lastViolation && now - report.lastViolation.at <= 15000
  if (recentViolation) return 'server-protocol-violation'

  if (report.lastServerActivityAgeMs !== null && report.lastServerActivityAgeMs >= report.pingTimeoutMs) {
    return 'server-silence-or-timeout-suspected'
  }

  if (report.state?.dead) return 'close-while-dead-or-respawning'
  return 'remote-close-or-transport-unknown'
}

class DisconnectBlackBox {
  constructor (options = {}) {
    this.startedAt = Date.now()
    this.username = options.username || null
    this.host = options.host || null
    this.port = options.port || null
    this.requestedTransport = options.requestedTransport || 'auto'
    this.pingTimeoutMs = Number(options.pingTimeoutMs || 0)
    this.logFile = options.logFile || null
    this.maxLogBytes = Number(options.maxLogBytes || 1024 * 1024)
    this.maxPacketHistory = Math.max(4, Number(options.maxPacketHistory || 24))
    this.phase = 'created'
    this.phaseTimes = { created: this.startedAt }
    this.lastServerActivityAt = 0
    this.lastPacket = null
    this.recentPackets = []
    this.lastHeartbeatAt = 0
    this.lastKick = null
    this.lastError = null
    this.lastViolation = null
    this.localDisconnect = null
    this.reported = false
  }

  markPhase (phase) {
    const now = Date.now()
    this.phase = phase
    this.phaseTimes[phase] = now
  }

  recordPacket (packet) {
    const now = Date.now()
    const name = packetName(packet)
    this.lastServerActivityAt = now
    this.lastPacket = { at: now, name }
    this.recentPackets.push({ at: now, name })
    while (this.recentPackets.length > this.maxPacketHistory) this.recentPackets.shift()
  }

  recordHeartbeat () {
    this.lastHeartbeatAt = Date.now()
  }

  recordKick (reason) {
    this.lastKick = { at: Date.now(), reason: safeJsonValue(reason) }
  }

  recordError (error) {
    this.lastError = errorSummary(error)
  }

  recordViolation (packet) {
    this.lastViolation = {
      at: Date.now(),
      packet: safeJsonValue(packet)
    }
  }

  markLocalDisconnect (reason, source = 'bot-code') {
    if (this.localDisconnect) return
    this.localDisconnect = {
      at: Date.now(),
      source,
      reason: reason || null
    }
  }

  buildReport (state = {}, closeArgs = []) {
    const now = Date.now()
    const toAge = at => at ? now - at : null
    const report = {
      schema: 1,
      at: new Date(now).toISOString(),
      username: this.username,
      endpoint: `${this.host}:${this.port}`,
      requestedTransport: this.requestedTransport,
      finalTransport: state.finalTransport || null,
      sessionUptimeMs: now - this.startedAt,
      phase: this.phase,
      phaseAgeMs: toAge(this.phaseTimes[this.phase]),
      connectedForMs: toAge(this.phaseTimes.connect),
      joinedForMs: toAge(this.phaseTimes.join),
      spawnedForMs: toAge(this.phaseTimes.spawn),
      pingTimeoutMs: this.pingTimeoutMs,
      lastServerActivityAgeMs: toAge(this.lastServerActivityAt),
      lastHeartbeatAgeMs: toAge(this.lastHeartbeatAt),
      lastPacket: this.lastPacket ? { name: this.lastPacket.name, ageMs: toAge(this.lastPacket.at) } : null,
      recentPackets: this.recentPackets.map(item => ({ name: item.name, ageMs: toAge(item.at) })),
      localDisconnect: this.localDisconnect ? { ...this.localDisconnect, ageMs: toAge(this.localDisconnect.at) } : null,
      lastKick: this.lastKick ? { ...this.lastKick, ageMs: toAge(this.lastKick.at) } : null,
      lastError: this.lastError ? { ...this.lastError, ageMs: toAge(this.lastError.at) } : null,
      lastViolation: this.lastViolation ? { ...this.lastViolation, ageMs: toAge(this.lastViolation.at) } : null,
      closeArgs: safeJsonValue(closeArgs),
      state: safeJsonValue(state)
    }
    report.classification = classifyReport(report, now)
    return report
  }

  report (state = {}, closeArgs = []) {
    if (this.reported) return null
    this.reported = true
    const report = this.buildReport(state, closeArgs)

    console.error('📦 DISCONNECT BLACK BOX', report)

    if (this.logFile) {
      try {
        this.rotateIfNeeded()
        fs.mkdirSync(path.dirname(this.logFile), { recursive: true })
        fs.appendFileSync(this.logFile, `${JSON.stringify(report)}\n`, 'utf8')
        console.error(`📝 Disconnect report disimpan: ${this.logFile}`)
      } catch (error) {
        console.error(`⚠️ Gagal menyimpan disconnect black-box: ${error.message}`)
      }
    }

    return report
  }

  rotateIfNeeded () {
    if (!this.logFile || !Number.isFinite(this.maxLogBytes) || this.maxLogBytes <= 0) return
    let stat
    try {
      stat = fs.statSync(this.logFile)
    } catch (error) {
      if (error.code === 'ENOENT') return
      throw error
    }
    if (stat.size < this.maxLogBytes) return

    const previous = `${this.logFile}.1`
    try { fs.rmSync(previous, { force: true }) } catch {}
    fs.renameSync(this.logFile, previous)
  }
}

function createDisconnectBlackBox (options) {
  return new DisconnectBlackBox(options)
}

module.exports = {
  DisconnectBlackBox,
  createDisconnectBlackBox,
  packetName,
  classifyReport
}
