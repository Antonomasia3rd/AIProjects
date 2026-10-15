/*
 * Minecraft-Bedrock-AFK v1.7.20 safe ready-handshake replay lab
 *
 * Hypothesis:
 *   The respawn packet handshake is correct, but after the final PlayerAction
 *   the server never receives the same "client is ready/active" lifecycle
 *   signals that made the initial join usable.
 *
 * This preload first records the successful initial-join outbound readiness
 * sequence, then after an actual death + final wire-6 PlayerAction it replays
 * ONLY the safe subset:
 *
 *   - EndLoadingScreen (protocol 2193 value 1), if observed initially
 *   - Interact mouse_over_entity, if observed initially
 *   - SetLocalPlayerAsInitialized, if observed initially
 *
 * It intentionally NEVER replays StartLoadingScreen (value 0 on protocol 2193),
 * because Mojang documents that unexpected StartLoadingScreen may disconnect
 * the client.
 *
 * bot.js remains unchanged.
 */

'use strict'

const fs = require('fs')
const { openCaptureSink } = require('./relay-capture')
const path = require('path')

const OUTPUT_DIR = path.resolve(process.cwd(), process.env.MC_RESPAWN_LAB_DIR || 'diag')
const OBSERVE_MS = Math.max(5000, Number.parseInt(process.env.MC_RESPAWN_OBSERVE_MS || '10000', 10) || 10000)
const REPLAY_BASE_DELAY_MS = Math.max(0, Number.parseInt(process.env.MC_RESPAWN_READY_REPLAY_DELAY_MS || '80', 10) || 80)
const REPLAY_STEP_MS = 40

let clientSerial = 0

function iso () { return new Date().toISOString() }

function safe (value, depth = 0, seen = new WeakSet()) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return value.toString()
  if (['string', 'number', 'boolean'].includes(typeof value)) return value
  if (Buffer.isBuffer(value)) {
    return { type: 'Buffer', length: value.length, hexPrefix: value.subarray(0, 64).toString('hex') }
  }
  if (typeof value !== 'object') return String(value)
  if (depth >= 7) return '[MaxDepth]'
  if (seen.has(value)) return '[Circular]'
  seen.add(value)
  if (Array.isArray(value)) return value.slice(0, 120).map(v => safe(v, depth + 1, seen))
  const out = {}
  for (const k of Object.keys(value).slice(0, 180)) {
    try { out[k] = safe(value[k], depth + 1, seen) } catch (_) {}
  }
  return out
}

function clonePacket (value) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return BigInt(value)
  if (Array.isArray(value)) return value.map(clonePacket)
  if (Buffer.isBuffer(value)) return Buffer.from(value)
  if (typeof value === 'object') {
    const out = {}
    for (const [k, v] of Object.entries(value)) out[k] = clonePacket(v)
    return out
  }
  return value
}

function unwrapInbound (packet, meta) {
  if (meta && typeof meta.name === 'string') {
    return { name: meta.name.replace(/^packet_/, ''), data: packet }
  }
  if (packet?.data && typeof packet.data.name === 'string') {
    return {
      name: packet.data.name.replace(/^packet_/, ''),
      data: packet.data.params ?? packet.data
    }
  }
  if (typeof packet?.name === 'string') {
    return {
      name: packet.name.replace(/^packet_/, ''),
      data: packet.params ?? packet
    }
  }
  return { name: null, data: packet }
}

function runtimeOf (data) {
  return data?.runtime_entity_id ?? data?.runtimeEntityId ??
    data?.runtime_id ?? data?.runtimeId ?? null
}

function normalizedLoadingType (value) {
  if (typeof value === 'string') {
    const s = value.toLowerCase()
    if (s.includes('end')) return 1
    if (s.includes('start')) return 0
  }
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}

function installOnClient (client, options) {
  if (!client || client.__respawnReadyReplayLab) return client
  Object.defineProperty(client, '__respawnReadyReplayLab', { value: true })

  const serial = ++clientSerial
  const stamp = iso().replace(/[:.]/g, '-')
  const file = path.join(OUTPUT_DIR, `respawn-ready-replay-${stamp}-c${serial}.jsonl`)
  const capture = openCaptureSink(fs, file, { directory: OUTPUT_DIR })

  let closed = false
  function record (type, data = {}) {
    if (closed) return
    capture.write(type, data)
  }

  function say (...args) {
    console.log('🧩 RESP-READY', ...args)
  }

  function closeLog (reason) {
    if (closed) return
    closed = true
    capture.close('capture-complete', { reason, outputFile: file }, error => {
      if (!error) say(`📁 capture selesai dan file sudah UNLOCKED: ${file}`)
    })
  }

  let selfRuntimeId = null
  let startGameSeen = false
  let initialSpawnComplete = false
  let initialReadyPackets = []

  let deathActive = false
  let deathAt = 0
  let readyAt = 0
  let finalizerAt = 0
  let replayStarted = false
  let aliveEvidence = null
  let observeTimer = null
  const replayTimers = new Set()

  const originalQueue = typeof client.queue === 'function' ? client.queue : null
  const originalWrite = typeof client.write === 'function' ? client.write : null

  function isSelfRuntime (data) {
    const rid = runtimeOf(data)
    return selfRuntimeId !== null && rid !== null && String(rid) === String(selfRuntimeId)
  }

  function finishAlive (why, data) {
    if (!deathActive || aliveEvidence) return
    aliveEvidence = why
    record('alive-evidence', {
      why,
      afterDeathMs: Date.now() - deathAt,
      afterReadyMs: readyAt ? Date.now() - readyAt : null,
      afterFinalizerMs: finalizerAt ? Date.now() - finalizerAt : null,
      data
    })
    say(`✅ SERVER ALIVE evidence: ${why}`)
    if (observeTimer) clearTimeout(observeTimer)
    for (const t of replayTimers) clearTimeout(t)
    replayTimers.clear()
    closeLog(`success:${why}`)
  }

  function armObserveTimeout () {
    if (observeTimer) clearTimeout(observeTimer)
    observeTimer = setTimeout(() => {
      if (!aliveEvidence) {
        record('no-alive-evidence', {
          afterDeathMs: Date.now() - deathAt,
          afterReadyMs: readyAt ? Date.now() - readyAt : null,
          afterFinalizerMs: finalizerAt ? Date.now() - finalizerAt : null
        })
        say(`❌ tidak ada server-alive evidence ${OBSERVE_MS} ms setelah finalizer`)
      }
      closeLog(aliveEvidence ? `success:${aliveEvidence}` : 'timeout-no-alive-evidence')
    }, OBSERVE_MS)
  }

  function normalizeReplayPacket (name, data) {
    const p = clonePacket(data)
    if (name === 'set_local_player_as_initialized') {
      p.runtime_entity_id = selfRuntimeId
    }
    return p
  }

  function getSafeReplaySequence () {
    const safePackets = []

    for (const item of initialReadyPackets) {
      if (item.name === 'serverbound_loading_screen') {
        const type = normalizedLoadingType(item.data?.type)
        // Protocol 2193: Start=0, End=1. Never replay Start.
        if (type === 1) {
          safePackets.push({
            name: item.name,
            data: item.data,
            reason: 'initial-join EndLoadingScreen'
          })
        }
      } else if (item.name === 'interact') {
        const action = item.data?.action_id
        if (action === 'mouse_over_entity' || action === 4 || action === '4') {
          safePackets.push({
            name: item.name,
            data: item.data,
            reason: 'initial-join interact mouse_over_entity'
          })
        }
      } else if (item.name === 'set_local_player_as_initialized') {
        safePackets.push({
          name: item.name,
          data: item.data,
          reason: 'initial-join local-player initialized'
        })
      }
    }

    // De-duplicate identical conceptual readiness packets while preserving order.
    const seen = new Set()
    return safePackets.filter(item => {
      const key =
        item.name === 'serverbound_loading_screen' ? `${item.name}:end` :
        item.name === 'interact' ? `${item.name}:mouse_over_entity` :
        item.name
      if (seen.has(key)) return false
      seen.add(key)
      return true
    })
  }

  function sendDirect (preferredVia, name, data) {
    if (preferredVia === 'write' && typeof originalWrite === 'function') {
      return originalWrite.call(client, name, data)
    }
    if (typeof originalQueue === 'function') {
      return originalQueue.call(client, name, data)
    }
    if (typeof originalWrite === 'function') {
      return originalWrite.call(client, name, data)
    }
    throw new Error('No outbound method available')
  }

  function replayReadyHandshake () {
    if (!deathActive || replayStarted || !finalizerAt) return
    replayStarted = true

    const seq = getSafeReplaySequence()
    record('ready-replay-plan', {
      initialCapturedCount: initialReadyPackets.length,
      safeReplayCount: seq.length,
      sequence: seq
    })

    say(`🔁 safe ready-handshake replay: ${seq.length} packet(s)`)

    if (seq.length === 0) {
      say('⚠️ initial join tidak menunjukkan EndLoadingScreen/interact/init yang bisa direplay')
      say('   Lab akan tetap mengamati server state tanpa mengirim packet tambahan.')
      return
    }

    seq.forEach((item, index) => {
      const delay = REPLAY_BASE_DELAY_MS + index * REPLAY_STEP_MS
      const timer = setTimeout(() => {
        replayTimers.delete(timer)
        if (!deathActive || aliveEvidence) return

        const packet = normalizeReplayPacket(item.name, item.data)
        record('ready-replay-out', {
          index,
          delayMs: delay,
          afterFinalizerMs: Date.now() - finalizerAt,
          name: item.name,
          reason: item.reason,
          data: packet
        })

        try {
          sendDirect(item.via, item.name, packet)
          say(`➡️ replay ${item.name} (${item.reason})`)
        } catch (err) {
          record('ready-replay-error', {
            index,
            name: item.name,
            error: err?.stack || String(err)
          })
          say(`❌ replay ${item.name} gagal: ${err?.message || err}`)
        }
      }, delay)

      replayTimers.add(timer)
    })
  }

  function onInbound (packet, meta) {
    const { name, data } = unwrapInbound(packet, meta)
    if (!name) return

    if (name === 'start_game') {
      startGameSeen = true
      const rid = runtimeOf(data)
      if (rid !== null) selfRuntimeId = rid
      record('start-game', {
        selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
        currentTick: data?.current_tick,
        engine: data?.engine
      })
      return
    }

    if (name === 'play_status' && data?.status === 'player_spawn') {
      if (!initialSpawnComplete) {
        initialSpawnComplete = true
        record('initial-spawn-complete', {
          capturedReadyPackets: initialReadyPackets
        })
        say(`🌍 initial spawn complete; captured ${initialReadyPackets.length} readiness packet(s)`)
        for (const p of initialReadyPackets) {
          say(`   baseline: ${p.name}`, safe(p.data))
        }
      } else if (deathActive) {
        finishAlive('play_status:player_spawn', data)
      }
      return
    }

    if (!initialSpawnComplete) return

    if (name === 'entity_event' && isSelfRuntime(data)) {
      if (data?.event_id === 'death_animation') {
        deathActive = true
        deathAt = Date.now()
        readyAt = 0
        finalizerAt = 0
        replayStarted = false
        aliveEvidence = null
        if (observeTimer) clearTimeout(observeTimer)
        for (const t of replayTimers) clearTimeout(t)
        replayTimers.clear()

        record('death', { selfRuntimeId: String(selfRuntimeId), data })
        say(`💀 actual self death detected (runtime=${selfRuntimeId})`)
      } else if (deathActive && data?.event_id === 'respawn') {
        finishAlive('entity_event:respawn', data)
      }
      return
    }

    if (!deathActive) return

    if (name === 'respawn') {
      record('respawn-in', {
        afterDeathMs: Date.now() - deathAt,
        data
      })
      const state = data?.state
      if (state === 1 || state === 'ready_to_spawn' || state === 'readyToSpawn') {
        readyAt = Date.now()
        say(`🟢 ReadyToSpawn at +${readyAt - deathAt} ms`)
      }
      return
    }

    if (name === 'set_entity_data' && isSelfRuntime(data)) {
      const interesting = {}
      for (const m of data?.metadata || []) {
        if (m?.key === 'player_has_died') interesting.player_has_died = m.value
        if (m?.key === 'boundingbox_width') interesting.boundingbox_width = m.value
        if (m?.key === 'boundingbox_height') interesting.boundingbox_height = m.value
        if (m?.key === 'flags') {
          interesting.flags = {
            invisible: m.value?.invisible,
            moving: m.value?.moving,
            no_ai: m.value?.no_ai,
            has_collision: m.value?.has_collision,
            affected_by_gravity: m.value?.affected_by_gravity
          }
        }
      }

      if (Object.keys(interesting).length) {
        record('self-metadata', {
          afterDeathMs: Date.now() - deathAt,
          tick: data?.tick,
          metadata: interesting
        })

        if (interesting.player_has_died !== undefined) {
          say(`🏷️ player_has_died=${interesting.player_has_died}`)
          if (Number(interesting.player_has_died) === 0) {
            finishAlive('self-metadata:player_has_died=0', interesting)
          }
        }
        if (interesting.flags?.invisible !== undefined) {
          say(`👁️ self invisible=${interesting.flags.invisible}`)
        }
      }
      return
    }

    if (name === 'update_attributes' && isSelfRuntime(data)) {
      const attrs = {}
      for (const a of data?.attributes || []) {
        if (a?.name === 'minecraft:health' || a?.name === 'minecraft:movement') {
          attrs[a.name] = a.current
        }
      }
      if (Object.keys(attrs).length) {
        record('self-attributes', {
          afterDeathMs: Date.now() - deathAt,
          tick: data?.tick,
          attrs
        })
        if (attrs['minecraft:health'] !== undefined) {
          say(`❤️ server self health=${attrs['minecraft:health']}`)
          if (Number(attrs['minecraft:health']) > 0) {
            finishAlive('update_attributes:health>0', attrs)
          }
        }
      }
      return
    }

    if (name === 'set_health') {
      record('set-health', { afterDeathMs: Date.now() - deathAt, data })
      say(`❤️ SetHealth=${data?.health}`)
      if (Number(data?.health) > 0) finishAlive('set_health>0', data)
      return
    }

    if (name === 'correct_player_move_prediction') {
      const dx = Number(data?.delta?.x || 0)
      const dz = Number(data?.delta?.z || 0)
      const horizontal = Math.hypot(dx, dz)
      record('movement-feedback', {
        afterDeathMs: Date.now() - deathAt,
        afterFinalizerMs: finalizerAt ? Date.now() - finalizerAt : null,
        tick: data?.tick,
        position: data?.position,
        delta: data?.delta,
        horizontal
      })
    }
  }

  client.on('packet', onInbound)

  function wrapOutbound (methodName, original) {
    if (typeof original !== 'function') return

    client[methodName] = function wrapped (name, params, ...rest) {
      const normalized = String(name || '').replace(/^packet_/, '')

      try {
        // Record successful initial-join readiness traffic.
        if (startGameSeen && !initialSpawnComplete) {
          if (
            normalized === 'serverbound_loading_screen' ||
            normalized === 'interact' ||
            normalized === 'set_local_player_as_initialized'
          ) {
            initialReadyPackets.push({
              name: normalized,
              via: methodName,
              at: Date.now(),
              data: clonePacket(params)
            })
            record('initial-ready-out', {
              via: methodName,
              name: normalized,
              data: params
            })
          }
        }

        if (initialSpawnComplete && deathActive) {
          if (normalized === 'respawn') {
            record('respawn-out', {
              afterDeathMs: Date.now() - deathAt,
              via: methodName,
              data: params
            })
          }

          if (
            normalized === 'player_action' &&
            String(runtimeOf(params)) === String(selfRuntimeId) &&
            (params?.action === 'stop_sleeping' || params?.action === 6 || params?.action === '6')
          ) {
            const now = Date.now()
            record('finalizer-out', {
              afterDeathMs: now - deathAt,
              afterReadyMs: readyAt ? now - readyAt : null,
              via: methodName,
              data: params
            })

            if (!finalizerAt) {
              finalizerAt = now
              say(`➡️ final wire-6 observed (${readyAt ? now - readyAt : 'n/a'} ms after ReadyToSpawn)`)
              replayReadyHandshake()
              armObserveTimeout()
            }
          }

          if (normalized === 'set_local_player_as_initialized') {
            record('bot-reinit-out', {
              afterDeathMs: Date.now() - deathAt,
              afterFinalizerMs: finalizerAt ? Date.now() - finalizerAt : null,
              via: methodName,
              data: params
            })
          }
        }
      } catch (err) {
        record('observer-error', {
          where: `outbound:${methodName}`,
          error: err?.stack || String(err)
        })
      }

      return original.call(this, name, params, ...rest)
    }
  }

  wrapOutbound('queue', originalQueue)
  wrapOutbound('write', originalWrite)

  function shutdown (reason) {
    if (observeTimer) clearTimeout(observeTimer)
    for (const t of replayTimers) clearTimeout(t)
    replayTimers.clear()

    record('stop', {
      reason,
      initialSpawnComplete,
      initialReadyPackets,
      deathActive,
      aliveEvidence
    })
    closeLog(`shutdown:${reason}`)
  }

  client.once('close', () => shutdown('close'))
  client.once('end', () => shutdown('end'))

  record('start', {
    version: '1.7.20-safe-ready-replay-lab',
    outputFile: file,
    observeMs: OBSERVE_MS,
    replayBaseDelayMs: REPLAY_BASE_DELAY_MS,
    options: {
      host: options?.host,
      port: options?.port,
      version: options?.version,
      protocolVersion: options?.protocolVersion,
      offline: options?.offline,
      autoInitPlayer: options?.autoInitPlayer
    }
  })

  say('v1.7.20 loaded; recording initial successful ready-handshake')
  say(`compact one-shot log → ${file}`)
  return client
}

function install () {
  let bp
  try {
    bp = require('bedrock-protocol')
  } catch (err) {
    console.error('RESP-READY: cannot require bedrock-protocol:', err)
    return
  }

  if (!bp || typeof bp.createClient !== 'function') {
    console.error('RESP-READY: createClient() unavailable')
    return
  }

  if (bp.createClient.__respawnReadyReplayWrapped) return

  const originalCreateClient = bp.createClient

  function wrappedCreateClient (options, ...rest) {
    return installOnClient(originalCreateClient.call(this, options, ...rest), options)
  }

  Object.defineProperty(wrappedCreateClient, '__respawnReadyReplayWrapped', {
    value: true
  })

  bp.createClient = wrappedCreateClient
}

install()
