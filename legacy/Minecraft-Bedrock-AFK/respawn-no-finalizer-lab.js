/*
 * Minecraft-Bedrock-AFK v1.7.22 — no-finalizer A/B lab
 *
 * Tests one thing only:
 *   After the normal Respawn(state=2) -> server ReadyToSpawn(state=1) flow,
 *   suppress the standalone PlayerAction Respawn (wire 6) that bot.js normally
 *   sends as a "finalizer".
 *
 * Rationale:
 *   Protocol 2193 documents RespawnPacket itself as a respawn handshake, with
 *   one client RespawnPacket and three server RespawnPackets. We have never
 *   isolated whether our extra PlayerAction after ReadyToSpawn is actually
 *   required on this server.
 *
 * No state-2 fields are changed.
 * No loading-screen packets are sent.
 * No interact/init replay is sent.
 * No action 7 is sent.
 * No PlayerAuthInput block_action is sent.
 *
 * bot.js remains unchanged.
 */

'use strict'

const fs = require('fs')
const { openCaptureSink } = require('./relay-capture')
const path = require('path')

const OUTPUT_DIR = path.resolve(process.cwd(), process.env.MC_RESPAWN_LAB_DIR || 'diag')
const OBSERVE_MS = Math.max(5000, Number.parseInt(process.env.MC_RESPAWN_OBSERVE_MS || '12000', 10) || 12000)

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

  if (Array.isArray(value)) {
    return value.slice(0, 120).map(v => safe(v, depth + 1, seen))
  }

  const out = {}
  for (const k of Object.keys(value).slice(0, 180)) {
    try { out[k] = safe(value[k], depth + 1, seen) } catch (_) {}
  }
  return out
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
  return data?.runtime_entity_id ??
    data?.runtimeEntityId ??
    data?.runtime_id ??
    data?.runtimeId ??
    null
}

function stateIs (data, numeric, names) {
  const v = data?.state
  if (v === numeric || v === String(numeric)) return true
  if (typeof v !== 'string') return false
  const n = v.toLowerCase().replace(/[^a-z0-9]/g, '')
  return names.some(x => n === x.toLowerCase().replace(/[^a-z0-9]/g, ''))
}

function isRespawnAction6 (params) {
  const a = params?.action
  return a === 6 || a === '6' || a === 'stop_sleeping' || a === 'respawn'
}

function installOnClient (client, options) {
  if (!client || client.__noFinalizerLab) return client
  Object.defineProperty(client, '__noFinalizerLab', { value: true })


  const serial = ++clientSerial
  const stamp = iso().replace(/[:.]/g, '-')
  const file = path.join(OUTPUT_DIR, `respawn-no-finalizer-${stamp}-c${serial}.jsonl`)
  const capture = openCaptureSink(fs, file, { directory: OUTPUT_DIR })

  let closed = false
  let selfRuntimeId = null
  let initialSpawnComplete = false

  let deathActive = false
  let deathAt = 0
  let state2At = 0
  let readyAt = 0
  let suppressedAt = 0
  let suppressedCount = 0
  let aliveEvidence = null
  let observeTimer = null

  function record (type, data = {}) {
    if (closed) return
    capture.write(type, data)
  }

  function say (...args) {
    console.log('🚫 FINALIZER-LAB', ...args)
  }

  function closeLog (reason) {
    if (closed) return
    closed = true
    capture.close('capture-complete', { reason, outputFile: file }, error => {
      if (!error) say(`📁 capture selesai dan file sudah UNLOCKED: ${file}`)
    })
  }

  function isSelfRuntime (data) {
    const rid = runtimeOf(data)
    return (
      selfRuntimeId !== null &&
      rid !== null &&
      String(rid) === String(selfRuntimeId)
    )
  }

  function markAlive (why, data) {
    if (!deathActive || aliveEvidence) return

    aliveEvidence = why
    record('alive-evidence', {
      why,
      afterDeathMs: deathAt ? Date.now() - deathAt : null,
      afterState2Ms: state2At ? Date.now() - state2At : null,
      afterReadyMs: readyAt ? Date.now() - readyAt : null,
      afterSuppressedFinalizerMs: suppressedAt ? Date.now() - suppressedAt : null,
      data
    })

    say(`✅ SERVER ALIVE evidence WITHOUT final wire-6: ${why}`)

    if (observeTimer) {
      clearTimeout(observeTimer)
      observeTimer = null
    }

    closeLog(`success:${why}`)
  }

  function armObserve () {
    if (observeTimer) clearTimeout(observeTimer)

    observeTimer = setTimeout(() => {
      if (!aliveEvidence) {
        record('no-alive-evidence', {
          afterDeathMs: Date.now() - deathAt,
          afterState2Ms: state2At ? Date.now() - state2At : null,
          afterReadyMs: readyAt ? Date.now() - readyAt : null,
          afterSuppressedFinalizerMs: suppressedAt ? Date.now() - suppressedAt : null,
          suppressedCount
        })
        say(`❌ tidak ada server-alive evidence ${OBSERVE_MS} ms setelah finalizer pertama ditelan`)
      }
      closeLog(aliveEvidence ? `success:${aliveEvidence}` : 'timeout-no-alive-evidence')
    }, OBSERVE_MS)
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
        engine: data?.engine
      })
      return
    }

    if (name === 'play_status' && data?.status === 'player_spawn') {
      if (!initialSpawnComplete) {
        initialSpawnComplete = true
        record('initial-spawn-complete', { data })
        say('🌍 initial spawn complete; lab armed')
      } else if (deathActive) {
        markAlive('play_status:player_spawn', data)
      }
      return
    }

    if (!initialSpawnComplete) return

    if (name === 'entity_event' && isSelfRuntime(data)) {
      if (data?.event_id === 'death_animation') {
        deathActive = true
        deathAt = Date.now()
        state2At = 0
        readyAt = 0
        suppressedAt = 0
        suppressedCount = 0
        aliveEvidence = null

        if (observeTimer) {
          clearTimeout(observeTimer)
          observeTimer = null
        }

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
      const now = Date.now()
      record('respawn-in', {
        afterDeathMs: now - deathAt,
        runtime_entity_id: runtimeOf(data) === null ? null : String(runtimeOf(data)),
        data
      })

      if (stateIs(data, 1, ['ready_to_spawn', 'readytospawn'])) {
        readyAt = now
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
            markAlive('self-metadata:player_has_died=0', interesting)
          }
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

      if (Number(data?.health) > 0) {
        markAlive('set_health>0', data)
      }
      return
    }

    if (name === 'correct_player_move_prediction') {
      const dx = Number(data?.delta?.x || 0)
      const dz = Number(data?.delta?.z || 0)
      record('movement-feedback', {
        afterDeathMs: Date.now() - deathAt,
        afterSuppressedFinalizerMs: suppressedAt ? Date.now() - suppressedAt : null,
        tick: data?.tick,
        position: data?.position,
        delta: data?.delta,
        horizontal: Math.hypot(dx, dz)
      })
    }
  }

  client.on('packet', onInbound)

  const originalQueue = typeof client.queue === 'function' ? client.queue : null

  if (originalQueue) {
    client.queue = function wrappedQueue (name, params, ...rest) {
      const normalized = String(name || '').replace(/^packet_/, '')

      try {
        if (
          normalized === 'respawn' &&
          initialSpawnComplete &&
          deathActive &&
          stateIs(params, 2, ['client_ready_to_spawn', 'clientreadytospawn'])
        ) {
          state2At = Date.now()
          record('state2-out', {
            afterDeathMs: state2At - deathAt,
            via: 'queue',
            data: params
          })
          say(
            `➡️ normal state-2 observed at +${state2At - deathAt} ms ` +
            `(runtime=${runtimeOf(params)}, pos=${JSON.stringify(params?.position)})`
          )
        }

        if (
          normalized === 'player_action' &&
          initialSpawnComplete &&
          deathActive &&
          String(runtimeOf(params)) === String(selfRuntimeId) &&
          isRespawnAction6(params)
        ) {
          const now = Date.now()
          suppressedCount++

          record('finalizer-suppressed', {
            count: suppressedCount,
            afterDeathMs: now - deathAt,
            afterState2Ms: state2At ? now - state2At : null,
            afterReadyMs: readyAt ? now - readyAt : null,
            via: 'queue',
            data: params
          })

          if (!suppressedAt) {
            suppressedAt = now
            say(
              `🛑 SUPPRESSED final wire-6 ` +
              `(${readyAt ? now - readyAt : 'n/a'} ms after ReadyToSpawn)`
            )
            say('   bot.js thinks queue() returned normally; packet was NOT put on wire')
            armObserve()
          } else {
            say(`🛑 SUPPRESSED duplicate final wire-6 #${suppressedCount}`)
          }

          // Deliberately swallow. queue() is void-like; returning undefined
          // preserves bot.js control flow while preventing the packet on wire.
          return undefined
        }
      } catch (err) {
        record('observer-error', {
          where: 'queue',
          error: err?.stack || String(err)
        })
      }

      return originalQueue.call(this, name, params, ...rest)
    }
  }

  function shutdown (reason) {
    if (observeTimer) {
      clearTimeout(observeTimer)
      observeTimer = null
    }

    record('stop', {
      reason,
      initialSpawnComplete,
      deathActive,
      suppressedCount,
      aliveEvidence
    })
    closeLog(`shutdown:${reason}`)
  }

  client.once('close', () => shutdown('close'))
  client.once('end', () => shutdown('end'))

  record('start', {
    version: '1.7.22-no-finalizer-lab',
    outputFile: file,
    observeMs: OBSERVE_MS,
    options: {
      host: options?.host,
      port: options?.port,
      version: options?.version,
      protocolVersion: options?.protocolVersion,
      offline: options?.offline,
      autoInitPlayer: options?.autoInitPlayer
    }
  })

  say('v1.7.22 loaded')
  say('ONLY experiment: suppress post-ReadyToSpawn standalone PlayerAction wire-6')
  say(`compact one-shot log → ${file}`)

  return client
}

function install () {
  let bp
  try {
    bp = require('bedrock-protocol')
  } catch (err) {
    console.error('FINALIZER-LAB: cannot require bedrock-protocol:', err)
    return
  }

  if (!bp || typeof bp.createClient !== 'function') {
    console.error('FINALIZER-LAB: bedrock-protocol.createClient() unavailable')
    return
  }

  if (bp.createClient.__noFinalizerLabWrapped) return

  const originalCreateClient = bp.createClient

  function wrappedCreateClient (options, ...rest) {
    return installOnClient(originalCreateClient.call(this, options, ...rest), options)
  }

  Object.defineProperty(wrappedCreateClient, '__noFinalizerLabWrapped', { value: true })
  bp.createClient = wrappedCreateClient
}

install()
