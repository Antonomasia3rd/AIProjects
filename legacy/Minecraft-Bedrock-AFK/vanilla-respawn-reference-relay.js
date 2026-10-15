/*
 * Minecraft-Bedrock-AFK v1.7.23 — Vanilla Reference Relay
 *
 * Passive MITM capture:
 *   official Minecraft Bedrock -> local Relay -> Nifirealm
 *
 * This script NEVER modifies/cancels/injects gameplay packets.
 * It records the real retail client's packet sequence around death/respawn so
 * it can be diffed against the AFK bot.
 *
 * Requires the existing project's bedrock-protocol dependency.
 */

'use strict'

const fs = require('fs')
const path = require('path')
const { createCaptureSink, sanitizeDiagnosticFields } = require('./relay-capture')
const { createShutdownCoordinator, installShutdownSignals } = require('./relay-shutdown')

const bedrock = require('bedrock-protocol')
const { Relay } = bedrock

const VERSION = process.env.MC_REF_VERSION || '1.26.51'
const LISTEN_HOST = process.env.MC_REF_LISTEN_HOST || '0.0.0.0'
const LISTEN_PORT = Number.parseInt(process.env.MC_REF_LISTEN_PORT || '19132', 10)
const TARGET_HOST = process.env.MC_REF_TARGET_HOST || 'Nifirealm.aternos.me'
const TARGET_PORT = Number.parseInt(process.env.MC_REF_TARGET_PORT || '24128', 10)
const POST_DEATH_MS = Math.max(
  8000,
  Number.parseInt(process.env.MC_REF_POST_DEATH_MS || '15000', 10) || 15000
)
const OUTPUT_DIR = path.resolve(process.cwd(), process.env.MC_REF_OUTPUT_DIR || 'diag')
const PROFILES_DIR = path.resolve(process.cwd(), process.env.MC_REF_PROFILES_DIR || '.relay-auth')
const MAX_CAPTURE_BYTES = Number(process.env.MC_REF_MAX_CAPTURE_BYTES || 16 * 1024 * 1024)

if (!Number.isFinite(LISTEN_PORT) || LISTEN_PORT < 1 || LISTEN_PORT > 65535) {
  throw new Error(`Invalid MC_REF_LISTEN_PORT: ${process.env.MC_REF_LISTEN_PORT}`)
}
if (!Number.isFinite(TARGET_PORT) || TARGET_PORT < 1 || TARGET_PORT > 65535) {
  throw new Error(`Invalid MC_REF_TARGET_PORT: ${process.env.MC_REF_TARGET_PORT}`)
}
if (!Number.isSafeInteger(MAX_CAPTURE_BYTES) || MAX_CAPTURE_BYTES < 65536 || MAX_CAPTURE_BYTES > 1024 * 1024 * 1024) {
  throw new Error('MC_REF_MAX_CAPTURE_BYTES must be an integer from 65536 through 1073741824')
}

fs.mkdirSync(PROFILES_DIR, { recursive: true })

function iso () {
  return new Date().toISOString()
}

function stamp () {
  return iso().replace(/[:.]/g, '-')
}

function safe (value) { return sanitizeDiagnosticFields(value) }

function runtimeOf (data) {
  return data?.runtime_entity_id ??
    data?.runtimeEntityId ??
    data?.runtime_id ??
    data?.runtimeId ??
    null
}

function packetName (name) {
  return String(name || '').replace(/^packet_/, '')
}

function statusIsSpawn (params) {
  const v = params?.status
  return v === 'player_spawn' || v === 3 || v === '3'
}

function eventIsDeath (params) {
  const v = params?.event_id
  return v === 'death_animation' || v === 3 || v === '3'
}

function extractSelfLifecycleMetadata (params) {
  const out = {}
  const metadata = params?.metadata
  if (!Array.isArray(metadata)) return out

  for (const m of metadata) {
    const key = m?.key
    if (
      key === 'player_has_died' ||
      key === 'flags' ||
      key === 'boundingbox_width' ||
      key === 'boundingbox_height'
    ) {
      out[key] = safe(m?.value)
    }
  }
  return out
}

function extractHealthAttributes (params) {
  const out = {}
  const attrs = params?.attributes
  if (!Array.isArray(attrs)) return out

  for (const a of attrs) {
    if (
      a?.name === 'minecraft:health' ||
      a?.name === 'minecraft:movement'
    ) {
      out[a.name] = {
        min: a.min,
        max: a.max,
        current: a.current,
        default: a.default
      }
    }
  }
  return out
}

function summarizeAuthInput (p) {
  return {
    pitch: p?.pitch,
    yaw: p?.yaw,
    head_yaw: p?.head_yaw,
    position: p?.position,
    move_vector: p?.move_vector,
    motion: p?.motion,
    input_data: p?.input_data,
    input_mode: p?.input_mode,
    play_mode: p?.play_mode,
    interaction_model: p?.interaction_model,
    interact_rotation: p?.interact_rotation,
    tick: p?.tick,
    delta: p?.delta,
    analogue_move_vector: p?.analogue_move_vector,
    camera_orientation: p?.camera_orientation,
    raw_move_vector: p?.raw_move_vector,
    block_action: p?.block_action
  }
}

// Huge/noisy clientbound data is represented by name/timing only.
// Lifecycle/control packets retain payloads.
const FULL_CLIENTBOUND = new Set([
  'start_game',
  'play_status',
  'respawn',
  'set_health',
  'update_attributes',
  'set_entity_data',
  'entity_event',
  'death_info',
  'move_player',
  'correct_player_move_prediction',
  'set_entity_motion',
  'update_client_input_locks',
  'player_list',
  'set_spawn_position',
  'change_dimension',
  'network_settings',
  'serverbound_loading_screen',
  'disconnect',
  'packet_violation_warning',
  'update_abilities',
  'adventure_settings',
  'set_commands_enabled',
  'set_difficulty'
])

const FULL_SERVERBOUND_ALWAYS = new Set([
  'respawn',
  'player_action',
  'serverbound_loading_screen',
  'interact',
  'set_local_player_as_initialized',
  'request_chunk_radius',
  'client_cache_status',
  'resource_pack_client_response',
  'command_request',
  'text',
  'modal_form_response',
  'item_stack_request',
  'inventory_transaction',
  'player_hotbar',
  'client_to_server_handshake',
  'login'
])

let sessionSerial = 0
let shuttingDown = false
const activeCaptures = new Set()

const relay = new Relay({
  version: VERSION,
  host: LISTEN_HOST,
  port: LISTEN_PORT,

  // Keep downstream authentication enabled: this is a real retail-client reference.
  offline: false,

  destination: {
    host: TARGET_HOST,
    port: TARGET_PORT,
    transport: 'raknet',
    offline: false
  },

  profilesFolder: PROFILES_DIR,
  forceSingle: true,
  enableChunkCaching: false,
  omitParseErrors: false,
  logging: false,

  onMsaCode: (data) => {
    console.log('')
    console.log('🔐 Upstream Microsoft/Xbox sign-in required.')
    if (data?.message) {
      console.log(data.message)
    } else {
      if (data?.verification_uri || data?.verificationUri) {
        console.log('Open:', data.verification_uri || data.verificationUri)
      }
      if (data?.user_code || data?.userCode) {
        console.log('Code:', data.user_code || data.userCode)
      }
    }
    console.log(`Auth cache: ${PROFILES_DIR}`)
    console.log('')
  }
})

relay.on('error', err => {
  console.error('❌ RELAY error:', safe(err?.stack || String(err)))
})

relay.on('connect', player => {
  if (shuttingDown) return // Relay shutdown owns connections; create no new capture files.
  const serial = ++sessionSerial
  const file = path.join(
    OUTPUT_DIR,
    `vanilla-respawn-reference-${stamp()}-c${serial}.jsonl`
  )
  let closed = false
  let selfRuntimeId = null
  let startGameAt = 0
  let initialSpawnAt = 0
  let deathAt = 0
  let deathTrigger = null
  let closeTimer = null
  let lastPreDeathPaiLog = 0
  let recordCount = 0
  const packetCounts = {
    serverbound: Object.create(null),
    clientbound: Object.create(null)
  }
  let capture
  function clearCloseTimer () {
    if (closeTimer) clearTimeout(closeTimer)
    closeTimer = null
  }
  try {
    fs.mkdirSync(OUTPUT_DIR, { recursive: true })
    const stream = fs.createWriteStream(file, { flags: 'wx', highWaterMark: 64 * 1024 })
    capture = createCaptureSink(stream, {
      maxBytes: MAX_CAPTURE_BYTES,
      onError: err => {
        closed = true; clearCloseTimer()
        console.error('⚠️ Capture stopped after an I/O error; relay forwarding continues:', safe(err?.message || String(err)))
      },
      onLimit: stats => {
        closed = true; clearCloseTimer()
        console.warn('⚠️ Capture byte limit reached; relay forwarding continues.', stats)
      }
    })
  } catch (err) {
    console.error('⚠️ Capture could not be created; relay forwarding continues:', safe(err?.message || String(err)))
    return
  }

  function logLine (type, fields = {}) {
    if (closed) return
    if (capture.write(type, fields)) recordCount++
  }

  function relMs () {
    return deathAt ? Date.now() - deathAt : null
  }

  function closeCapture (reason) {
    if (closed) return

    closed = true
    clearCloseTimer()
    capture.close('capture-complete', {
      reason,
      records: recordCount,
      selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
      startGameAt: startGameAt || null,
      initialSpawnAt: initialSpawnAt || null,
      deathAt: deathAt || null,
      deathTrigger,
      packetCounts
    }, (error, stats) => {
      if (error) return // The stream error handler already reported failure.
      console.log('')
      console.log(`📁 VANILLA-REF capture selesai dan file sudah UNLOCKED:`)
      console.log(`   ${file}`)
      console.log(`   submitted records: ${stats.acceptedRecords}; dropped diagnostics: ${stats.droppedRecords}`)
      console.log('   Tinjau data pribadi sebelum membagikan JSONL. Relay boleh ditutup dengan Ctrl+C.')
      console.log('')
    })
  }
  const activeCapture = { capture, close: closeCapture }
  activeCaptures.add(activeCapture)
  capture.whenClosed().then(() => activeCaptures.delete(activeCapture))

  function beginDeath(trigger, params) {
    if (closed || deathAt) return
    deathAt = Date.now()
    deathTrigger = trigger

    logLine('death-window-start', {
      trigger,
      selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
      params
    })

    console.log('')
    console.log(`💀 VANILLA-REF death detected: ${trigger}`)
    console.log(`   capturing every serverbound packet for ${POST_DEATH_MS} ms...`)

    closeTimer = setTimeout(() => {
      closeCapture('post-death-window-complete')
    }, POST_DEATH_MS)
  }

  function isSelf (params) {
    const rid = runtimeOf(params)
    return (
      selfRuntimeId !== null &&
      rid !== null &&
      String(rid) === String(selfRuntimeId)
    )
  }

  logLine('capture-start', {
    version: '1.7.23-vanilla-reference-relay',
    protocolVersionName: VERSION,
    listen: `${LISTEN_HOST}:${LISTEN_PORT}`,
    destination: `${TARGET_HOST}:${TARGET_PORT}`,
    postDeathMs: POST_DEATH_MS,
    connectionAddress: player?.connection?.address ?? null
  })

  console.log('')
  console.log(`🔬 VANILLA-REF downstream connected: ${player?.connection?.address || 'unknown'}`)
  console.log(`   capture → ${file}`)

  player.on('serverbound', ({ name, params }) => {
    if (closed) return
    name = packetName(name)
    packetCounts.serverbound[name] = (packetCounts.serverbound[name] || 0) + 1

    const now = Date.now()
    const afterDeath = Boolean(deathAt)

    // Critical: after death, record every outgoing packet name. This catches
    // an unexpected lifecycle packet even if we do not yet know its semantics.
    let payload = null

    if (name === 'player_auth_input') {
      if (afterDeath) {
        payload = summarizeAuthInput(params)
      } else if (now - lastPreDeathPaiLog >= 1000) {
        lastPreDeathPaiLog = now
        payload = summarizeAuthInput(params)
      } else {
        // Keep packet counts but avoid bloating the pre-death file.
        return
      }
    } else if (afterDeath || FULL_SERVERBOUND_ALWAYS.has(name)) {
      payload = params
    } else {
      // Before death only preserve lifecycle/control packets.
      return
    }

    logLine('serverbound', {
      afterDeathMs: relMs(),
      name,
      params: payload
    })

    if (afterDeath) {
      const compact = [
        'respawn',
        'player_action',
        'serverbound_loading_screen',
        'interact',
        'set_local_player_as_initialized',
        'play_status'
      ]
      if (compact.includes(name)) {
        console.log(`➡️ VANILLA ${name}`, safe(payload))
      }
    }
  })

  player.on('clientbound', ({ name, params }) => {
    if (closed) return
    name = packetName(name)
    packetCounts.clientbound[name] = (packetCounts.clientbound[name] || 0) + 1

    if (name === 'start_game') {
      const rid = runtimeOf(params)
      if (rid !== null) selfRuntimeId = rid
      startGameAt = Date.now()

      logLine('clientbound', {
        afterDeathMs: relMs(),
        name,
        params
      })

      console.log(`🆔 VANILLA-REF runtime=${String(selfRuntimeId)}`)
      return
    }

    // Detect real death from multiple independent server signals.
    if (name === 'entity_event' && isSelf(params) && eventIsDeath(params)) {
      beginDeath('self entity_event/death_animation', params)
    } else if (name === 'death_info') {
      beginDeath('death_info', params)
    } else if (name === 'set_health' && Number(params?.health) <= 0) {
      beginDeath('set_health<=0', params)
    } else if (name === 'update_attributes' && isSelf(params)) {
      const attrs = extractHealthAttributes(params)
      const h = attrs['minecraft:health']?.current
      if (h !== undefined && Number(h) <= 0) {
        beginDeath('self update_attributes health<=0', attrs)
      }
    } else if (name === 'set_entity_data' && isSelf(params)) {
      const meta = extractSelfLifecycleMetadata(params)
      if (Number(meta.player_has_died) === 1) {
        beginDeath('self metadata player_has_died=1', meta)
      }
    }

    if (name === 'play_status' && statusIsSpawn(params)) {
      if (!initialSpawnAt) {
        initialSpawnAt = Date.now()
        console.log('🌍 VANILLA-REF initial player_spawn observed; use server console to /kill this player once.')
      } else if (deathAt) {
        console.log(`✅ VANILLA-REF post-death player_spawn at +${relMs()} ms`)
      }
    }

    // After death record every incoming packet name, but only preserve full
    // payloads for lifecycle/control packets to avoid chunk/NBT explosions.
    if (deathAt) {
      logLine('clientbound', {
        afterDeathMs: relMs(),
        name,
        params: FULL_CLIENTBOUND.has(name) ? params : null
      })
    } else if (FULL_CLIENTBOUND.has(name)) {
      logLine('clientbound', {
        afterDeathMs: null,
        name,
        params
      })
    }

    if (!deathAt) return

    if (name === 'respawn') {
      console.log(`⬅️ VANILLA respawn +${relMs()}ms`, safe(params))
    } else if (name === 'set_health') {
      console.log(`❤️ VANILLA set_health +${relMs()}ms`, safe(params))
    } else if (name === 'update_attributes' && isSelf(params)) {
      const attrs = extractHealthAttributes(params)
      if (Object.keys(attrs).length) {
        console.log(`❤️ VANILLA self attributes +${relMs()}ms`, attrs)
      }
    } else if (name === 'set_entity_data' && isSelf(params)) {
      const meta = extractSelfLifecycleMetadata(params)
      if (Object.keys(meta).length) {
        console.log(`🏷️ VANILLA self metadata +${relMs()}ms`, meta)
      }
    } else if (name === 'entity_event' && isSelf(params)) {
      console.log(`🎭 VANILLA self entity_event +${relMs()}ms`, safe(params))
    }
  })

  player.once('close', () => {
    if (!closed) closeCapture('downstream-close')
  })
  player.once('end', () => {
    if (!closed) closeCapture('downstream-end')
  })

  if (player.upstream?.once) {
    player.upstream.once('error', err => {
      logLine('upstream-error', { error: err?.stack || String(err) })
      console.error('❌ Upstream error:', safe(err?.message || String(err)))
    })
    player.upstream.once('close', () => {
      logLine('upstream-close', {})
    })
  }
})

console.log('')
console.log('🔬 Minecraft-Bedrock-AFK v1.7.23 — Vanilla Reference Relay')
console.log('------------------------------------------------------------')
console.log(`Listen      : ${LISTEN_HOST}:${LISTEN_PORT}`)
console.log(`Destination : ${TARGET_HOST}:${TARGET_PORT}`)
console.log(`Version     : ${VERSION}`)
console.log(`Auth cache  : ${PROFILES_DIR}`)
console.log('')
console.log('This relay is PASSIVE: it does not cancel/edit/inject gameplay packets.')
console.log('Open official Minecraft and add/join:')
console.log(`  Server Address: 127.0.0.1`)
console.log(`  Port          : ${LISTEN_PORT}`)
console.log('')
console.log('After you enter the world, kill that vanilla player once from the server console.')
console.log('')

Promise.resolve(relay.listen())
  .then(() => {
    console.log(`✅ Relay listening on UDP ${LISTEN_PORT}`)
  })
  .catch(err => {
    console.error('❌ Could not start relay:', safe(err?.stack || String(err)))
    process.exitCode = 1
  })

const shutdownCoordinator = createShutdownCoordinator({
  stopRelay: () => relay.close(),
  closeCaptures: async () => {
    const active = [...activeCaptures]
    for (const entry of active) entry.close('relay-shutdown')
    const states = await Promise.all(active.map(entry => entry.capture.whenClosed()))
    if (states.some(state => state.failed)) throw new Error('One or more capture files closed after an I/O failure')
  }
})
async function shutdown () {
  if (!shuttingDown) console.log('\nStopping relay and flushing capture files...')
  shuttingDown = true
  const result = await shutdownCoordinator.run()
  if (!result.ok) console.error('⚠️ Relay shutdown was incomplete or failed; capture flush is not confirmed:', safe(result))
  process.exit(result.ok ? 0 : 1)
}

installShutdownSignals(process, shutdown)
