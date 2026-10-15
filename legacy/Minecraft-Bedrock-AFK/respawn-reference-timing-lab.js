/*
 * Minecraft-Bedrock-AFK v1.7.19 reference-timing lab
 *
 * Narrow hypothesis:
 *   v1.7.16 sends Respawn(ClientReadyToSpawn/state=2) too soon after
 *   death (~60 ms observed), while a known working Bedrock bot flow
 *   deliberately waits ~200 ms before that packet, then waits for
 *   server ReadyToSpawn and ~200 ms before PlayerAction respawn.
 *
 * This preload changes ONE thing only:
 *   - hold the first outbound respawn state=2 until 200 ms after the
 *     self death_animation.
 *
 * Everything else remains bot.js behavior:
 *   - existing wire-6 compatibility shim
 *   - existing ~200 ms finalizer delay
 *   - movement / handled_teleport / re-init logic
 *
 * One-shot diagnostic JSONL automatically closes after the test window
 * so Windows releases the file handle without requiring Ctrl+C.
 */

'use strict'

const fs = require('fs')
const { openCaptureSink } = require('./relay-capture')
const path = require('path')

const INITIAL_DELAY_MS = Math.max(
  0,
  Number.parseInt(process.env.MC_RESPAWN_INITIAL_DELAY_MS || '200', 10) || 200
)
const OBSERVE_AFTER_FINALIZER_MS = Math.max(
  4000,
  Number.parseInt(process.env.MC_RESPAWN_OBSERVE_MS || '12000', 10) || 12000
)
const OUTPUT_DIR = path.resolve(process.cwd(), process.env.MC_RESPAWN_LAB_DIR || 'diag')

let clientSerial = 0

function iso () { return new Date().toISOString() }

function safe (value, depth = 0, seen = new WeakSet()) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return value.toString()
  if (['string', 'number', 'boolean'].includes(typeof value)) return value
  if (Buffer.isBuffer(value)) return {
    type: 'Buffer',
    length: value.length,
    hexPrefix: value.subarray(0, 64).toString('hex')
  }
  if (typeof value !== 'object') return String(value)
  if (depth >= 6) return '[MaxDepth]'
  if (seen.has(value)) return '[Circular]'
  seen.add(value)

  if (Array.isArray(value)) return value.slice(0, 100).map(v => safe(v, depth + 1, seen))

  const out = {}
  for (const k of Object.keys(value).slice(0, 160)) {
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

function installOnClient (client, options) {
  if (!client || client.__respawnReferenceTimingLab) return client
  Object.defineProperty(client, '__respawnReferenceTimingLab', { value: true })

  const serial = ++clientSerial
  const stamp = iso().replace(/[:.]/g, '-')
  const file = path.join(OUTPUT_DIR, `respawn-reference-timing-${stamp}-c${serial}.jsonl`)
  const capture = openCaptureSink(fs, file, { directory: OUTPUT_DIR })

  let logClosed = false

  function record (type, data = {}) {
    if (logClosed) return
    capture.write(type, data)
  }

  function say (...args) {
    console.log('⏱️ RESP-TIMING', ...args)
  }

  function closeLog (reason) {
    if (logClosed) return
    logClosed = true
    capture.close('capture-complete', { reason, outputFile: file }, error => {
      if (!error) say(`📁 capture selesai dan file sudah UNLOCKED: ${file}`)
    })
  }

  let selfRuntimeId = null
  let initialSpawnComplete = false
  let deathActive = false
  let deathAt = 0
  let readyAt = 0
  let state2ActualAt = 0
  let firstFinalizerAt = 0
  let aliveEvidence = null

  let heldState2 = null
  let heldState2Sender = null
  let heldState2Timer = null
  let state2Sent = false
  let duplicateState2Suppressed = 0

  let observeTimer = null
  let zeroFeedbackPrinted = 0

  const originalQueue = typeof client.queue === 'function' ? client.queue : null
  const originalWrite = typeof client.write === 'function' ? client.write : null

  function isSelfRuntime (data) {
    const rid = runtimeOf(data)
    return selfRuntimeId !== null && rid !== null && String(rid) === String(selfRuntimeId)
  }

  function resetDeathState () {
    deathActive = true
    deathAt = Date.now()
    readyAt = 0
    state2ActualAt = 0
    firstFinalizerAt = 0
    aliveEvidence = null
    state2Sent = false
    duplicateState2Suppressed = 0
    heldState2 = null
    heldState2Sender = null
    zeroFeedbackPrinted = 0

    if (heldState2Timer) clearTimeout(heldState2Timer)
    heldState2Timer = null
    if (observeTimer) clearTimeout(observeTimer)
    observeTimer = null
  }

  function markAlive (why, data) {
    if (!deathActive || aliveEvidence) return
    aliveEvidence = why
    record('alive-evidence', {
      why,
      afterDeathMs: Date.now() - deathAt,
      afterState2Ms: state2ActualAt ? Date.now() - state2ActualAt : null,
      afterReadyMs: readyAt ? Date.now() - readyAt : null,
      afterFinalizerMs: firstFinalizerAt ? Date.now() - firstFinalizerAt : null,
      data
    })
    say(`✅ SERVER ALIVE evidence: ${why}`)
    closeLog(`success:${why}`)
  }

  function scheduleCaptureClose () {
    if (observeTimer) clearTimeout(observeTimer)
    observeTimer = setTimeout(() => {
      if (!aliveEvidence) {
        record('no-alive-evidence', {
          afterDeathMs: Date.now() - deathAt,
          afterState2Ms: state2ActualAt ? Date.now() - state2ActualAt : null,
          afterReadyMs: readyAt ? Date.now() - readyAt : null,
          afterFinalizerMs: firstFinalizerAt ? Date.now() - firstFinalizerAt : null,
          duplicateState2Suppressed
        })
        say(`❌ tidak ada server-alive evidence ${OBSERVE_AFTER_FINALIZER_MS} ms setelah finalizer`)
      }
      closeLog(aliveEvidence ? `success:${aliveEvidence}` : 'timeout-no-alive-evidence')
    }, OBSERVE_AFTER_FINALIZER_MS)
  }

  function actuallySendHeldState2 () {
    if (!deathActive || state2Sent || !heldState2 || !heldState2Sender) return
    state2Sent = true
    state2ActualAt = Date.now()

    record('state2-released', {
      actualAfterDeathMs: state2ActualAt - deathAt,
      targetDelayMs: INITIAL_DELAY_MS,
      packet: heldState2
    })

    say(`➡️ ClientReadyToSpawn state-2 RELEASED at +${state2ActualAt - deathAt} ms from death`)

    try {
      heldState2Sender('respawn', clonePacket(heldState2))
    } catch (err) {
      record('state2-send-error', { error: err?.stack || String(err) })
      say(`❌ state-2 send failed: ${err?.message || err}`)
    }
  }

  function holdState2 (params, sender, via) {
    const now = Date.now()

    if (!deathActive) {
      // Not an actual death sequence: allow untouched.
      return false
    }

    if (state2Sent || heldState2) {
      duplicateState2Suppressed++
      record('state2-duplicate-suppressed', {
        via,
        afterDeathMs: now - deathAt,
        packet: params
      })
      say(`🚫 duplicate state-2 suppressed (#${duplicateState2Suppressed})`)
      return true
    }

    heldState2 = clonePacket(params)
    heldState2Sender = sender

    const elapsed = now - deathAt
    const remaining = Math.max(0, INITIAL_DELAY_MS - elapsed)

    record('state2-held', {
      via,
      observedAfterDeathMs: elapsed,
      targetDelayMs: INITIAL_DELAY_MS,
      remainingMs: remaining,
      packet: params
    })

    say(`⏸️ bot wanted state-2 at +${elapsed} ms; holding ${remaining} ms to match reference timing`)

    if (remaining <= 0) {
      actuallySendHeldState2()
    } else {
      heldState2Timer = setTimeout(actuallySendHeldState2, remaining)
    }

    return true
  }

  function onInbound (packet, meta) {
    const { name, data } = unwrapInbound(packet, meta)
    if (!name) return

    if (name === 'start_game') {
      const rid = runtimeOf(data)
      if (rid !== null) selfRuntimeId = rid
      record('start-game', {
        selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
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
        say('🌍 initial spawn complete; timing lab armed')
      } else if (deathActive) {
        markAlive('play_status:player_spawn', data)
      }
      return
    }

    if (!initialSpawnComplete) return

    if (name === 'entity_event' && isSelfRuntime(data)) {
      if (data?.event_id === 'death_animation') {
        resetDeathState()
        record('death', {
          selfRuntimeId: String(selfRuntimeId),
          data
        })
        say(`💀 actual self death detected (runtime=${selfRuntimeId})`)
      } else if (deathActive && data?.event_id === 'respawn') {
        markAlive('entity_event:respawn', data)
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
        say(`🟢 ReadyToSpawn at +${readyAt - deathAt} ms from death; state-2 age=${state2ActualAt ? readyAt - state2ActualAt : 'n/a'} ms`)
      }
      return
    }

    if (name === 'set_entity_data' && isSelfRuntime(data)) {
      const interesting = {}
      for (const m of data?.metadata || []) {
        if (m?.key === 'player_has_died') interesting.player_has_died = m.value
        else if (m?.key === 'boundingbox_width') interesting.boundingbox_width = m.value
        else if (m?.key === 'boundingbox_height') interesting.boundingbox_height = m.value
        else if (m?.key === 'flags') {
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
            markAlive('self-metadata:player_has_died=0', interesting)
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
            markAlive('update_attributes:health>0', attrs)
          }
        }
      }
      return
    }

    if (name === 'set_health') {
      record('set-health', {
        afterDeathMs: Date.now() - deathAt,
        data
      })
      say(`❤️ SetHealth=${data?.health}`)
      if (Number(data?.health) > 0) markAlive('set_health>0', data)
      return
    }

    if (name === 'correct_player_move_prediction') {
      const dx = Number(data?.delta?.x || 0)
      const dz = Number(data?.delta?.z || 0)
      const horizontal = Math.hypot(dx, dz)
      record('movement-feedback', {
        afterDeathMs: Date.now() - deathAt,
        afterFinalizerMs: firstFinalizerAt ? Date.now() - firstFinalizerAt : null,
        tick: data?.tick,
        position: data?.position,
        delta: data?.delta,
        horizontal
      })

      if (firstFinalizerAt && horizontal <= 1e-8 && zeroFeedbackPrinted < 4) {
        zeroFeedbackPrinted++
        say(`🧊 authoritative horizontal movement = 0 (#${zeroFeedbackPrinted})`)
      }
    }
  }

  client.on('packet', onInbound)

  function wrapOutbound (methodName, original) {
    if (typeof original !== 'function') return

    client[methodName] = function wrapped (name, params, ...rest) {
      const normalized = String(name || '').replace(/^packet_/, '')

      try {
        if (
          normalized === 'respawn' &&
          params?.state === 2 &&
          initialSpawnComplete &&
          deathActive
        ) {
          const sender = (packetName, packetData) =>
            original.call(client, packetName, packetData)

          if (holdState2(params, sender, methodName)) {
            // Deliberately suppress the immediate original send.
            return undefined
          }
        }

        if (
          normalized === 'player_action' &&
          initialSpawnComplete &&
          deathActive &&
          String(runtimeOf(params)) === String(selfRuntimeId) &&
          (params?.action === 'stop_sleeping' || params?.action === 6 || params?.action === '6')
        ) {
          const now = Date.now()
          record('finalizer-out', {
            afterDeathMs: now - deathAt,
            afterState2Ms: state2ActualAt ? now - state2ActualAt : null,
            afterReadyMs: readyAt ? now - readyAt : null,
            via: methodName,
            data: params
          })

          if (!firstFinalizerAt) {
            firstFinalizerAt = now
            say(
              `➡️ final wire-6 at +${now - deathAt} ms from death ` +
              `(${readyAt ? now - readyAt : 'n/a'} ms after ReadyToSpawn)`
            )
            scheduleCaptureClose()
          } else {
            say('⚠️ additional bot finalizer observed')
          }
        }

        if (
          normalized === 'set_local_player_as_initialized' &&
          initialSpawnComplete &&
          deathActive
        ) {
          record('reinit-out', {
            afterDeathMs: Date.now() - deathAt,
            via: methodName,
            data: params
          })
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
    if (heldState2Timer) clearTimeout(heldState2Timer)
    if (observeTimer) clearTimeout(observeTimer)
    record('stop', {
      reason,
      deathActive,
      aliveEvidence,
      state2Sent,
      duplicateState2Suppressed
    })
    closeLog(`shutdown:${reason}`)
  }

  client.once('close', () => shutdown('close'))
  client.once('end', () => shutdown('end'))

  record('start', {
    version: '1.7.19-reference-timing-lab',
    outputFile: file,
    initialDelayMs: INITIAL_DELAY_MS,
    observeAfterFinalizerMs: OBSERVE_AFTER_FINALIZER_MS,
    options: {
      host: options?.host,
      port: options?.port,
      offline: options?.offline,
      autoInitPlayer: options?.autoInitPlayer
    }
  })

  say(`v1.7.19 loaded; forced initial respawn delay=${INITIAL_DELAY_MS} ms`)
  say(`compact one-shot log → ${file}`)
  return client
}

function install () {
  let bp
  try {
    bp = require('bedrock-protocol')
  } catch (err) {
    console.error('RESP-TIMING: cannot require bedrock-protocol:', err)
    return
  }

  if (!bp || typeof bp.createClient !== 'function') {
    console.error('RESP-TIMING: createClient() unavailable')
    return
  }

  if (bp.createClient.__respawnReferenceTimingWrapped) return

  const originalCreateClient = bp.createClient

  function wrappedCreateClient (options, ...rest) {
    return installOnClient(originalCreateClient.call(this, options, ...rest), options)
  }

  Object.defineProperty(wrappedCreateClient, '__respawnReferenceTimingWrapped', {
    value: true
  })

  bp.createClient = wrappedCreateClient
}

install()
