/*
 * Minecraft-Bedrock-AFK v1.7.17 diagnostic preload
 *
 * PURPOSE
 * -------
 * Observe Bedrock death -> respawn lifecycle state without changing bot.js.
 * This intentionally DOES NOT change the v1.7.11/v1.7.16 respawn handshake,
 * PlayerAuthInput generation, physics, AI, or reconnect behaviour.
 *
 * Usage:
 *   node -r ./respawn-lifecycle-diag-preload.js bot.js
 *
 * Optional env:
 *   MC_RESPAWN_DIAG_MS=60000
 *   MC_RESPAWN_DIAG_DIR=./diag
 *   MC_RESPAWN_DIAG_CONSOLE=1
 */

'use strict'

const fs = require('fs')
const { openCaptureSink, sanitizeDiagnosticFields } = require('./relay-capture')
const path = require('path')

const DIAG_WINDOW_MS = Math.max(
  10000,
  Number.parseInt(process.env.MC_RESPAWN_DIAG_MS || '60000', 10) || 60000
)
const CONSOLE_ENABLED = process.env.MC_RESPAWN_DIAG_CONSOLE !== '0'
const OUTPUT_DIR = path.resolve(
  process.cwd(),
  process.env.MC_RESPAWN_DIAG_DIR || 'diag'
)

const IMPORTANT_INBOUND = new Set([
  'start_game',
  'play_status',
  'death_info',
  'entity_event',
  'respawn',
  'move_player',
  'correct_player_move_prediction',
  'set_health',
  'update_attributes',
  'update_abilities',
  'update_adventure_settings',
  'adventure_settings',
  'set_player_game_type',
  'update_player_game_type',
  'update_client_input_locks',
  'set_entity_data',
  'set_actor_data',
  'set_entity_motion',
  'set_actor_motion',
  'mob_effect',
  'set_spawn_position',
  'change_dimension'
])

const IMPORTANT_OUTBOUND = new Set([
  'respawn',
  'player_action',
  'set_local_player_as_initialized',
  'player_auth_input'
])

const STATE_CACHE_NAMES = new Set([
  'start_game',
  'play_status',
  'respawn',
  'move_player',
  'correct_player_move_prediction',
  'set_health',
  'update_attributes',
  'update_abilities',
  'update_adventure_settings',
  'adventure_settings',
  'set_player_game_type',
  'update_player_game_type',
  'update_client_input_locks',
  'set_entity_data',
  'set_actor_data',
  'set_entity_motion',
  'set_actor_motion',
  'mob_effect',
  'set_spawn_position',
  'change_dimension'
])

let installed = false
let clientSerial = 0

function nowIso () {
  return new Date().toISOString()
}

function sanitize (value, depth = 0, seen = new WeakSet()) {
  if (value === null || value === undefined) return value
  const t = typeof value
  if (t === 'bigint') return value.toString()
  if (t === 'number' || t === 'boolean' || t === 'string') return value
  if (t === 'function') return `[Function ${value.name || 'anonymous'}]`

  if (Buffer.isBuffer(value)) {
    const limit = Math.min(value.length, 64)
    return {
      __type: 'Buffer',
      length: value.length,
      hexPrefix: value.subarray(0, limit).toString('hex')
    }
  }

  if (depth >= 7) return '[MaxDepth]'
  if (typeof value !== 'object') return String(value)
  if (seen.has(value)) return '[Circular]'
  seen.add(value)

  if (Array.isArray(value)) {
    const max = 80
    const result = value.slice(0, max).map(v => sanitize(v, depth + 1, seen))
    if (value.length > max) result.push(`[+${value.length - max} more]`)
    return result
  }

  const result = {}
  const keys = Object.keys(value)
  const maxKeys = 120
  for (const key of keys.slice(0, maxKeys)) {
    try {
      result[key] = sanitize(value[key], depth + 1, seen)
    } catch (err) {
      result[key] = `[sanitize error: ${err && err.message ? err.message : err}]`
    }
  }
  if (keys.length > maxKeys) result.__truncatedKeys = keys.length - maxKeys
  return result
}

function stableStringify (value) {
  const seen = new WeakSet()
  function norm (v) {
    if (v === null || v === undefined) return v
    if (typeof v === 'bigint') return v.toString()
    if (typeof v !== 'object') return v
    if (seen.has(v)) return '[Circular]'
    seen.add(v)
    if (Array.isArray(v)) return v.map(norm)
    const out = {}
    for (const k of Object.keys(v).sort()) out[k] = norm(v[k])
    return out
  }
  try {
    return JSON.stringify(norm(value))
  } catch (_) {
    return String(value)
  }
}

function unwrapInboundPacket (packet, meta) {
  let name = null
  let params = null

  if (meta && typeof meta === 'object' && typeof meta.name === 'string') {
    name = meta.name
    params = packet
  }

  if (!name && packet && packet.data && typeof packet.data.name === 'string') {
    name = packet.data.name
    params = packet.data.params !== undefined ? packet.data.params : packet.data
  }

  if (!name && packet && typeof packet.name === 'string') {
    name = packet.name
    params = packet.params !== undefined ? packet.params : packet
  }

  if (!name && packet && typeof packet.packetName === 'string') {
    name = packet.packetName
    params = packet.params !== undefined ? packet.params : packet
  }

  return {
    name: name ? String(name).replace(/^packet_/, '') : null,
    params: params === null ? packet : params
  }
}

function looksLikeDeathEvent (name, params) {
  if (name === 'death_info') return true
  if (name !== 'entity_event') return false
  const s = stableStringify(sanitize(params)).toLowerCase()
  return s.includes('death')
}

function getRespawnState (params) {
  if (!params || typeof params !== 'object') return undefined
  return params.state ?? params.respawn_state ?? params.respawnState
}

function getPlayStatus (params) {
  if (!params || typeof params !== 'object') return undefined
  return params.status ?? params.play_status ?? params.playStatus
}

function isPlayerSpawnStatus (params) {
  const v = getPlayStatus(params)
  if (typeof v === 'string') return v.toLowerCase().includes('player_spawn')
  return false
}

function inputDataContainsTeleportAck (params) {
  const value =
    params?.input_data ??
    params?.inputData ??
    params?.input_flags ??
    params?.inputFlags

  const s = stableStringify(sanitize(value)).toLowerCase()
  return (
    s.includes('handled_teleport') ||
    s.includes('received_server_data')
  )
}

function pickSelfRuntimeId (params) {
  if (!params || typeof params !== 'object') return null
  return (
    params.runtime_entity_id ??
    params.runtimeEntityId ??
    params.entity_runtime_id ??
    params.entityRuntimeId ??
    null
  )
}

function pickUniqueId (params) {
  if (!params || typeof params !== 'object') return null
  return (
    params.entity_unique_id ??
    params.entityUniqueId ??
    params.unique_entity_id ??
    params.uniqueEntityId ??
    null
  )
}

function makeLogger (serial) {
  const stamp = new Date().toISOString().replace(/[:.]/g, '-')
  const file = path.join(OUTPUT_DIR, `respawn-lifecycle-${stamp}-c${serial}.jsonl`)
  const capture = openCaptureSink(fs, file, { directory: OUTPUT_DIR })

  function write (type, data = {}) {
    const record = {
      ts: nowIso(),
      monoMs: Math.round(Number(process.hrtime.bigint() / 1000000n)),
      type,
      ...sanitizeDiagnosticFields(data)
    }
    capture.write(type, record)
    if (CONSOLE_ENABLED) {
      const prefix = '🩻 RESP-DIAG'
      if (type === 'inbound' || type === 'outbound') {
        console.log(prefix, type === 'inbound' ? '⬅' : '➡', record.name, record.data)
      } else if (type.startsWith('snapshot')) {
        console.log(prefix, type, record.label || '', record.changed || '')
      } else {
        console.log(prefix, type, record)
      }
    }
  }

  write('diag-start', {
    version: '1.7.17-diagnostic-overlay',
    outputFile: file,
    windowMs: DIAG_WINDOW_MS,
    pid: process.pid,
    cwd: process.cwd(),
    node: process.version
  })

  return { write, file, close: () => capture.close('capture-complete', { outputFile: file }) }
}

function attachDiagnostics (client, options = {}) {
  if (!client || typeof client.on !== 'function') return client
  if (client.__respawnLifecycleDiagAttached) return client
  Object.defineProperty(client, '__respawnLifecycleDiagAttached', {
    value: true,
    configurable: false,
    enumerable: false,
    writable: false
  })

  const serial = ++clientSerial
  const log = makeLogger(serial)

  let selfRuntimeId = null
  let selfUniqueId = null
  let captureUntil = 0
  let deathAt = 0
  let readyAt = 0
  let baselineCache = null
  let playerAuthInputSeen = 0
  let playerAuthInputLogged = 0
  let lastAuthLogAt = 0
  let lifecycleSequence = 0

  const stateCache = new Map()
  const packetCounts = new Map()
  const firstNamesInWindow = new Set()
  const timers = new Set()

  function inWindow () {
    return Date.now() <= captureUntil
  }

  function extendWindow (reason) {
    const wasInactive = !inWindow()
    captureUntil = Math.max(captureUntil, Date.now() + DIAG_WINDOW_MS)
    if (wasInactive) firstNamesInWindow.clear()
    log.write('window-extend', { reason, captureUntil: new Date(captureUntil).toISOString() })
  }

  function cacheState (name, params) {
    if (!STATE_CACHE_NAMES.has(name)) return
    stateCache.set(name, {
      at: nowIso(),
      data: sanitize(params)
    })
  }

  function stateObject () {
    return Object.fromEntries([...stateCache.entries()])
  }

  function diffFromBaseline () {
    if (!baselineCache) return []
    const current = stateObject()
    const keys = new Set([...Object.keys(baselineCache), ...Object.keys(current)])
    const changed = []
    for (const key of [...keys].sort()) {
      if (stableStringify(baselineCache[key]) !== stableStringify(current[key])) {
        changed.push(key)
      }
    }
    return changed
  }

  function snapshot (label) {
    log.write('snapshot-state', {
      label,
      sinceDeathMs: deathAt ? Date.now() - deathAt : null,
      sinceReadyMs: readyAt ? Date.now() - readyAt : null,
      selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
      selfUniqueId: selfUniqueId === null ? null : String(selfUniqueId),
      changed: diffFromBaseline(),
      state: stateObject(),
      packetCounts: Object.fromEntries([...packetCounts.entries()].sort())
    })
  }

  function scheduleSnapshot (delayMs, label) {
    const timer = setTimeout(() => {
      timers.delete(timer)
      snapshot(label)
    }, delayMs)
    if (typeof timer.unref === 'function') timer.unref()
    timers.add(timer)
  }

  function beginDeathWindow (reason, params) {
    lifecycleSequence++
    deathAt = Date.now()
    readyAt = 0
    baselineCache = stateObject()
    packetCounts.clear()
    firstNamesInWindow.clear()
    extendWindow(reason)
    log.write('death-trigger', {
      lifecycleSequence,
      reason,
      data: sanitize(params),
      cachedStateKeys: Object.keys(baselineCache)
    })
    snapshot('pre-death-baseline')
    scheduleSnapshot(1000, 'death+1s')
    scheduleSnapshot(3000, 'death+3s')
  }

  function notePacketNameDuringWindow (name) {
    if (!inWindow() || !name) return
    packetCounts.set(name, (packetCounts.get(name) || 0) + 1)
    if (!firstNamesInWindow.has(name)) {
      firstNamesInWindow.add(name)
      log.write('first-packet-name-in-window', { name })
    }
  }

  function shouldLogMovePlayer (params) {
    if (!inWindow()) return false
    const rid = pickSelfRuntimeId(params)
    if (selfRuntimeId !== null && rid !== null && String(rid) !== String(selfRuntimeId)) return false

    const mode = params?.mode ?? params?.position_mode ?? params?.positionMode
    if (mode !== undefined && mode !== 0 && mode !== 'normal') return true

    // Keep a low-rate sample of normal self move_player packets.
    const now = Date.now()
    if (!shouldLogMovePlayer.last || now - shouldLogMovePlayer.last >= 1000) {
      shouldLogMovePlayer.last = now
      return true
    }
    return false
  }
  shouldLogMovePlayer.last = 0

  function onInbound (packet, meta) {
    const { name, params } = unwrapInboundPacket(packet, meta)
    if (!name) return

    notePacketNameDuringWindow(name)

    if (name === 'start_game') {
      const rid = pickSelfRuntimeId(params)
      if (rid !== null) selfRuntimeId = rid
      const uid = pickUniqueId(params)
      if (uid !== null) selfUniqueId = uid
    }

    if (looksLikeDeathEvent(name, params) && !inWindow()) {
      beginDeathWindow(name, params)
    } else if (looksLikeDeathEvent(name, params)) {
      extendWindow(name)
      log.write('death-trigger-extra', { name, data: sanitize(params) })
    }

    if (name === 'respawn') {
      extendWindow(`respawn-state-${String(getRespawnState(params))}`)
    }

    if (name === 'play_status' && isPlayerSpawnStatus(params)) {
      extendWindow('play_status-player_spawn')
    }

    cacheState(name, params)

    let logThis = IMPORTANT_INBOUND.has(name)
    if (name === 'move_player') logThis = shouldLogMovePlayer(params)

    if (logThis) {
      log.write('inbound', {
        name,
        inRespawnWindow: inWindow(),
        sinceDeathMs: deathAt ? Date.now() - deathAt : null,
        sinceReadyMs: readyAt ? Date.now() - readyAt : null,
        data: sanitize(params)
      })
    }

    if (name === 'respawn') {
      const state = getRespawnState(params)
      if (state === 1 || state === 'ready_to_spawn' || state === 'readyToSpawn') {
        readyAt = Date.now()
        log.write('respawn-ready-seen', { state, data: sanitize(params) })
        snapshot('server-ready-to-spawn')
        scheduleSnapshot(250, 'ready+250ms')
        scheduleSnapshot(1000, 'ready+1s')
        scheduleSnapshot(3000, 'ready+3s')
        scheduleSnapshot(5000, 'ready+5s')
        scheduleSnapshot(10000, 'ready+10s')
        scheduleSnapshot(20000, 'ready+20s')
        scheduleSnapshot(40000, 'ready+40s')
        scheduleSnapshot(55000, 'ready+55s')
      }
    }
  }

  // Generic packet event is documented by bedrock-protocol and avoids depending
  // on exact packet handler names inside bot.js.
  client.on('packet', onInbound)

  // Also listen for the most important named events as a fallback for versions
  // where the generic 'packet' event shape differs. Duplicate suppression is
  // intentionally not attempted for named events; they are tagged separately.
  for (const name of IMPORTANT_INBOUND) {
    client.on(name, params => {
      if (name === 'move_player' && !shouldLogMovePlayer(params)) return
      // If generic packet is working, cache/logging there is preferred.
      // Named-event fallback is only emitted when it carries useful lifecycle state.
      if (name === 'respawn' || name === 'update_abilities' ||
          name === 'update_adventure_settings' || name === 'adventure_settings' ||
          name === 'set_player_game_type' || name === 'update_player_game_type' ||
          name === 'update_client_input_locks' || name === 'set_health' ||
          name === 'update_attributes' || name === 'move_player') {
        log.write('named-event-observed', {
          name,
          inRespawnWindow: inWindow(),
          data: sanitize(params)
        })
      }
    })
  }

  function wrapOutbound (methodName) {
    const original = client[methodName]
    if (typeof original !== 'function') return

    client[methodName] = function wrappedOutbound (name, params, ...rest) {
      try {
        const normalizedName = String(name || '').replace(/^packet_/, '')
        if (IMPORTANT_OUTBOUND.has(normalizedName)) {
          if (normalizedName === 'player_auth_input') {
            playerAuthInputSeen++
            const now = Date.now()
            const mustLog =
              inWindow() &&
              (
                playerAuthInputSeen <= 12 ||
                playerAuthInputSeen % 20 === 0 ||
                now - lastAuthLogAt >= 1000 ||
                inputDataContainsTeleportAck(params)
              )

            if (mustLog) {
              playerAuthInputLogged++
              lastAuthLogAt = now
              log.write('outbound', {
                via: methodName,
                name: normalizedName,
                inRespawnWindow: inWindow(),
                playerAuthInputSeen,
                playerAuthInputLogged,
                sinceDeathMs: deathAt ? now - deathAt : null,
                sinceReadyMs: readyAt ? now - readyAt : null,
                data: sanitize(params)
              })
            }
          } else {
            log.write('outbound', {
              via: methodName,
              name: normalizedName,
              inRespawnWindow: inWindow(),
              sinceDeathMs: deathAt ? Date.now() - deathAt : null,
              sinceReadyMs: readyAt ? Date.now() - readyAt : null,
              data: sanitize(params)
            })
          }
        }
      } catch (err) {
        log.write('diag-error', {
          where: `wrapOutbound:${methodName}`,
          error: err && err.stack ? err.stack : String(err)
        })
      }

      return original.call(this, name, params, ...rest)
    }
  }

  wrapOutbound('queue')
  wrapOutbound('write')

  const shutdown = reason => {
    try {
      snapshot(`shutdown:${reason}`)
      log.write('diag-stop', {
        reason,
        outputFile: log.file,
        playerAuthInputSeen,
        playerAuthInputLogged
      })
      log.close()
    } catch (_) {}
  }

  client.once('close', () => shutdown('client-close'))
  client.once('end', () => shutdown('client-end'))

  log.write('client-attached', {
    serial,
    options: sanitize({
      host: options.host,
      port: options.port,
      version: options.version,
      protocolVersion: options.protocolVersion,
      offline: options.offline,
      transport: options.transport,
      delayedInit: options.delayedInit,
      autoInitPlayer: options.autoInitPlayer
    })
  })

  return client
}

function install () {
  if (installed) return
  installed = true

  let bedrock
  try {
    bedrock = require('bedrock-protocol')
  } catch (err) {
    console.error('[RESP-DIAG] Failed to require bedrock-protocol:', err)
    return
  }

  if (!bedrock || typeof bedrock.createClient !== 'function') {
    console.error('[RESP-DIAG] bedrock-protocol.createClient not found; diagnostic preload not installed.')
    return
  }

  if (bedrock.createClient.__respawnLifecycleDiagWrapped) return

  const originalCreateClient = bedrock.createClient

  function createClientWithDiagnostics (options, ...rest) {
    const client = originalCreateClient.call(this, options, ...rest)
    return attachDiagnostics(client, options || {})
  }

  Object.defineProperty(createClientWithDiagnostics, '__respawnLifecycleDiagWrapped', {
    value: true
  })

  bedrock.createClient = createClientWithDiagnostics

  console.log(
    `🩻 Respawn lifecycle diagnostic overlay v1.7.17 loaded ` +
    `(window=${DIAG_WINDOW_MS}ms, dir=${OUTPUT_DIR})`
  )
}

install()

module.exports = {
  attachDiagnostics,
  unwrapInboundPacket,
  sanitize
}
