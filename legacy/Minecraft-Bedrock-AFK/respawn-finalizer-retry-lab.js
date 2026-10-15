/*
 * Minecraft-Bedrock-AFK v1.7.18 finalizer-retry lab
 *
 * Goal:
 *   Test one narrow hypothesis from the v1.7.17 capture:
 *   the server reaches ReadyToSpawn, receives the first standalone
 *   PlayerAction wire-6 finalizer, but never clears the server-side
 *   death state.
 *
 * This preload:
 *   - does NOT change bot.js
 *   - does NOT use PlayerAuthInput.block_action
 *   - does NOT send action wire 7
 *   - waits for the bot's existing final PlayerAction, then re-sends
 *     the exact same serialized-intent packet up to two times only if
 *     no server "alive/respawn complete" evidence appears.
 *   - keeps console output intentionally compact.
 */

'use strict'

const fs = require('fs')
const { openCaptureSink } = require('./relay-capture')
const path = require('path')

const RETRY_DELAYS_MS = [1200, 2600]
const FAIL_REPORT_MS = 4200
const OUTPUT_DIR = path.resolve(process.cwd(), process.env.MC_RESPAWN_LAB_DIR || 'diag')

let clientSerial = 0

function iso () { return new Date().toISOString() }

function safe (value, depth = 0, seen = new WeakSet()) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return value.toString()
  if (['string', 'number', 'boolean'].includes(typeof value)) return value
  if (Buffer.isBuffer(value)) return { type: 'Buffer', length: value.length, hex: value.subarray(0, 64).toString('hex') }
  if (typeof value !== 'object') return String(value)
  if (depth >= 6) return '[MaxDepth]'
  if (seen.has(value)) return '[Circular]'
  seen.add(value)
  if (Array.isArray(value)) return value.slice(0, 80).map(v => safe(v, depth + 1, seen))
  const out = {}
  for (const k of Object.keys(value).slice(0, 120)) out[k] = safe(value[k], depth + 1, seen)
  return out
}

function clonePacket (value) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return BigInt(value)
  if (Array.isArray(value)) return value.map(clonePacket)
  if (typeof value === 'object') {
    if (Buffer.isBuffer(value)) return Buffer.from(value)
    const out = {}
    for (const [k, v] of Object.entries(value)) out[k] = clonePacket(v)
    return out
  }
  return value
}

function unwrap (packet, meta) {
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

function uniqueOf (data) {
  return data?.entity_unique_id ?? data?.entityUniqueId ??
    data?.unique_entity_id ?? data?.uniqueEntityId ?? null
}

function actionLooksLikeWire6Compat (action) {
  // Current mapper calls wire 6 "stop_sleeping"; v1.7.16 deliberately uses
  // that token as a compatibility shim because protocol 2193 defines wire 6
  // as Respawn.
  return action === 'stop_sleeping' || action === 6 || action === '6'
}

function installOnClient (client, options) {
  if (!client || client.__respawnFinalizerRetryLab) return client
  Object.defineProperty(client, '__respawnFinalizerRetryLab', { value: true })

  const serial = ++clientSerial
  const stamp = iso().replace(/[:.]/g, '-')
  const file = path.join(OUTPUT_DIR, `respawn-finalizer-lab-${stamp}-c${serial}.jsonl`)
  const capture = openCaptureSink(fs, file, { directory: OUTPUT_DIR })

  function record (type, data = {}) {
    capture.write(type, data)
  }

  function say (...args) {
    console.log('🧬 RESP-LAB', ...args)
  }

  let selfRuntimeId = null
  let selfUniqueId = null
  let initialSpawnComplete = false

  let deathActive = false
  let deathAt = 0
  let readyAt = 0
  let firstFinalizerAt = 0
  let completionAt = 0
  let completionEvidence = []
  let retryCount = 0
  let cachedFinalizer = null
  let cachedSender = null
  let generation = 0
  const timers = new Set()

  const originalQueue = typeof client.queue === 'function' ? client.queue : null
  const originalWrite = typeof client.write === 'function' ? client.write : null

  function isSelfRuntime (data) {
    const rid = runtimeOf(data)
    return selfRuntimeId !== null && rid !== null && String(rid) === String(selfRuntimeId)
  }

  function isSelfUnique (data) {
    const uid = uniqueOf(data)
    return selfUniqueId !== null && uid !== null && String(uid) === String(selfUniqueId)
  }

  function clearTimers () {
    for (const t of timers) clearTimeout(t)
    timers.clear()
  }

  function finishCycle (why, data) {
    if (!deathActive || completionAt) return
    completionAt = Date.now()
    completionEvidence.push(why)
    record('completion', {
      why,
      afterDeathMs: deathAt ? completionAt - deathAt : null,
      afterReadyMs: readyAt ? completionAt - readyAt : null,
      afterFinalizerMs: firstFinalizerAt ? completionAt - firstFinalizerAt : null,
      retryCount,
      data
    })
    say(`✅ server alive evidence: ${why}; finalizer retries=${retryCount}`)
    clearTimers()
    deathActive = false
  }

  function schedule (ms, fn) {
    const g = generation
    const t = setTimeout(() => {
      timers.delete(t)
      if (g !== generation) return
      fn()
    }, ms)
    if (typeof t.unref === 'function') t.unref()
    timers.add(t)
  }

  function scheduleRetries () {
    if (!cachedFinalizer || !cachedSender || !deathActive) return

    for (const delay of RETRY_DELAYS_MS) {
      schedule(delay, () => {
        if (!deathActive || completionAt) return
        retryCount++
        record('finalizer-retry', {
          retry: retryCount,
          delayMs: delay,
          packet: cachedFinalizer
        })
        say(`♻️ no alive evidence yet → resend SAME wire-6 finalizer #${retryCount}`)
        try {
          cachedSender('player_action', clonePacket(cachedFinalizer))
        } catch (err) {
          record('retry-error', { retry: retryCount, error: err?.stack || String(err) })
          say(`❌ retry #${retryCount} failed locally: ${err?.message || err}`)
        }
      })
    }

    schedule(FAIL_REPORT_MS, () => {
      if (!deathActive || completionAt) return
      record('no-completion', {
        afterFinalizerMs: Date.now() - firstFinalizerAt,
        retryCount
      })
      say(`⚠️ still NO server alive evidence after first finalizer + ${retryCount} retries`)
      say('   Expected any of: player_has_died=0, health>0, entity_event=respawn, play_status=player_spawn')
    })
  }

  function startDeath (data) {
    generation++
    clearTimers()
    deathActive = true
    deathAt = Date.now()
    readyAt = 0
    firstFinalizerAt = 0
    completionAt = 0
    completionEvidence = []
    retryCount = 0
    cachedFinalizer = null
    cachedSender = null

    record('death', { data, selfRuntimeId: String(selfRuntimeId) })
    say(`💀 actual self death detected (runtime=${selfRuntimeId})`)
  }

  function observeInbound (packet, meta) {
    const { name, data } = unwrap(packet, meta)
    if (!name) return

    if (name === 'start_game') {
      const rid = runtimeOf(data)
      const uid = uniqueOf(data)
      if (rid !== null) selfRuntimeId = rid
      if (uid !== null) selfUniqueId = uid
      record('start-game', {
        selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
        selfUniqueId: selfUniqueId === null ? null : String(selfUniqueId),
        currentTick: data?.current_tick,
        rewindHistorySize: data?.rewind_history_size,
        engine: data?.engine
      })
      return
    }

    if (name === 'play_status' && data?.status === 'player_spawn') {
      if (!initialSpawnComplete) {
        initialSpawnComplete = true
        record('initial-spawn-complete', { data })
        say('🌍 initial player_spawn complete; death lab armed')
      } else if (deathActive) {
        finishCycle('play_status:player_spawn', data)
      }
      return
    }

    // Ignore all initial-join respawn state traffic. This fixes the v1.7.17
    // console flood / false capture-window trigger.
    if (!initialSpawnComplete) return

    if (name === 'entity_event' && isSelfRuntime(data)) {
      if (data?.event_id === 'death_animation' && !deathActive) {
        startDeath(data)
      } else if (deathActive && data?.event_id === 'respawn') {
        finishCycle('entity_event:respawn', data)
      }
      return
    }

    if (!deathActive) return

    if (name === 'death_info') {
      record('death-info', { data })
      return
    }

    if (name === 'respawn') {
      record('respawn-in', { data })
      const state = data?.state
      if (state === 1 || state === 'ready_to_spawn' || state === 'readyToSpawn') {
        readyAt = Date.now()
        say(`🟢 server ReadyToSpawn (+${readyAt - deathAt}ms from death)`)
      }
      return
    }

    if (name === 'set_entity_data' && isSelfRuntime(data)) {
      const interesting = {}
      for (const m of data?.metadata || []) {
        if (['player_has_died', 'health', 'flags', 'boundingbox_width',
             'boundingbox_height', 'player_last_death_position'].includes(m?.key)) {
          if (m.key === 'flags' && m.value && typeof m.value === 'object') {
            interesting.flags = {
              invisible: m.value.invisible,
              moving: m.value.moving,
              no_ai: m.value.no_ai,
              has_collision: m.value.has_collision,
              affected_by_gravity: m.value.affected_by_gravity
            }
          } else {
            interesting[m.key] = m.value
          }
        }
      }

      if (Object.keys(interesting).length) {
        record('self-metadata', { tick: data?.tick, metadata: interesting })
        if ('player_has_died' in interesting) {
          say(`🏷️ self player_has_died=${interesting.player_has_died}`)
          if (Number(interesting.player_has_died) === 0) {
            finishCycle('self-metadata:player_has_died=0', interesting)
          }
        }
        if (interesting.flags?.invisible !== undefined) {
          say(`🏷️ self invisible=${interesting.flags.invisible}`)
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
        record('self-attributes', { tick: data?.tick, attrs })
        if (attrs['minecraft:health'] !== undefined) {
          say(`❤️ server self health=${attrs['minecraft:health']}`)
          if (Number(attrs['minecraft:health']) > 0) {
            finishCycle('update_attributes:health>0', attrs)
          }
        }
      }
      return
    }

    if (name === 'set_health') {
      record('set-health', { data })
      say(`❤️ set_health=${data?.health}`)
      if (Number(data?.health) > 0) finishCycle('set_health>0', data)
      return
    }

    if (name === 'update_abilities') {
      if (selfUniqueId === null || isSelfUnique(data)) {
        record('self-abilities', { data })
        say('🪽 self update_abilities received')
      }
      return
    }

    if (name === 'update_adventure_settings' || name === 'adventure_settings' ||
        name === 'set_player_game_type' || name === 'update_player_game_type' ||
        name === 'update_client_input_locks') {
      record(name, { data })
      say(`⚙️ ${name}`, safe(data))
      return
    }

    if (name === 'correct_player_move_prediction') {
      const d = data?.delta
      const horizontal = d ? Math.hypot(Number(d.x || 0), Number(d.z || 0)) : null
      record('movement-feedback', {
        tick: data?.tick,
        position: data?.position,
        delta: d,
        horizontal
      })
      // Don't spam: only print zero-feedback while finalization is pending.
      if (firstFinalizerAt && horizontal !== null && horizontal <= 1e-8) {
        const elapsed = Date.now() - firstFinalizerAt
        if (elapsed < 4500 && (elapsed < 500 || Math.abs(elapsed % 1000) < 80)) {
          say(`🧊 authoritative movement still zero (${elapsed}ms after finalizer)`)
        }
      }
    }
  }

  client.on('packet', observeInbound)

  function wrapOutbound (methodName, original) {
    if (typeof original !== 'function') return

    client[methodName] = function wrapped (name, params, ...rest) {
      const normalized = String(name || '').replace(/^packet_/, '')

      try {
        if (initialSpawnComplete && deathActive) {
          if (normalized === 'respawn') {
            record('respawn-out', { via: methodName, data: params })
          }

          if (normalized === 'player_action' &&
              String(runtimeOf(params)) === String(selfRuntimeId) &&
              actionLooksLikeWire6Compat(params?.action)) {
            record('finalizer-out', { via: methodName, data: params })

            if (!firstFinalizerAt) {
              firstFinalizerAt = Date.now()
              cachedFinalizer = clonePacket(params)

              // Re-use exactly the same transport method, bypassing this wrapper
              // for retries so we don't recursively schedule more retries.
              cachedSender = (packetName, packetData) =>
                original.call(client, packetName, packetData)

              const readyDelta = readyAt ? firstFinalizerAt - readyAt : null
              say(`➡️ first wire-6 finalizer observed${readyDelta !== null ? ` (${readyDelta}ms after ReadyToSpawn)` : ''}`)
              scheduleRetries()
            } else {
              record('additional-finalizer-observed', { via: methodName, data: params })
            }
          }

          if (normalized === 'set_local_player_as_initialized') {
            record('reinit-out', { via: methodName, data: params })
          }
        }
      } catch (err) {
        record('observer-error', { where: `outbound:${methodName}`, error: err?.stack || String(err) })
      }

      return original.call(this, name, params, ...rest)
    }
  }

  wrapOutbound('queue', originalQueue)
  wrapOutbound('write', originalWrite)

  const close = reason => {
    record('stop', {
      reason,
      initialSpawnComplete,
      deathActive,
      retryCount,
      completionEvidence
    })
    capture.close('capture-complete', { reason, outputFile: file })
  }
  client.once('close', () => close('close'))
  client.once('end', () => close('end'))

  record('start', {
    version: '1.7.18-finalizer-retry-lab',
    outputFile: file,
    retryDelaysMs: RETRY_DELAYS_MS,
    options: {
      host: options?.host,
      port: options?.port,
      offline: options?.offline,
      autoInitPlayer: options?.autoInitPlayer
    }
  })

  say(`v1.7.18 loaded; compact log → ${file}`)
  return client
}

function install () {
  let bp
  try {
    bp = require('bedrock-protocol')
  } catch (err) {
    console.error('RESP-LAB: cannot require bedrock-protocol:', err)
    return
  }

  if (!bp || typeof bp.createClient !== 'function') {
    console.error('RESP-LAB: createClient() unavailable')
    return
  }
  if (bp.createClient.__respawnFinalizerRetryLabWrapped) return

  const original = bp.createClient
  function wrappedCreateClient (options, ...rest) {
    return installOnClient(original.call(this, options, ...rest), options)
  }
  Object.defineProperty(wrappedCreateClient, '__respawnFinalizerRetryLabWrapped', { value: true })
  bp.createClient = wrappedCreateClient
}

install()
