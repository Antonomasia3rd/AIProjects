/*
 * Minecraft-Bedrock-AFK v1.7.21 — Respawn state-2 field A/B lab
 *
 * Purpose:
 *   Test ONLY the fields of the already-expected ClientReadyToSpawn packet.
 *
 * No new packet types are introduced.
 * No loading-screen replay.
 * No interact replay.
 * No extra set_local_player_as_initialized.
 * No PlayerAction retry.
 * No action-7 experiment.
 *
 * Modes:
 *   control
 *     Existing bot packet unchanged.
 *
 *   zero-runtime            [recommended first novel test]
 *     state=2 position unchanged, runtime_entity_id forced to 0.
 *
 *   server-position
 *     state=2 position replaced with latest server Respawn(state=0) position,
 *     runtime_entity_id unchanged.
 *
 *   zero-runtime-server-position
 *     Applies both changes.
 *
 * The known v1.7.16 final PlayerAction wire-6 logic is left untouched.
 */

'use strict'

const fs = require('fs')
const { openCaptureSink } = require('./relay-capture')
const path = require('path')

const MODE = String(
  process.env.MC_RESPAWN_STATE2_MODE || 'zero-runtime'
).trim().toLowerCase()

const VALID_MODES = new Set([
  'control',
  'zero-runtime',
  'server-position',
  'zero-runtime-server-position'
])

const OBSERVE_MS = Math.max(
  5000,
  Number.parseInt(process.env.MC_RESPAWN_OBSERVE_MS || '12000', 10) || 12000
)

const OUTPUT_DIR = path.resolve(
  process.cwd(),
  process.env.MC_RESPAWN_LAB_DIR || 'diag'
)

let clientSerial = 0

function iso () { return new Date().toISOString() }

function safe (value, depth = 0, seen = new WeakSet()) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return value.toString()
  if (['string', 'number', 'boolean'].includes(typeof value)) return value
  if (Buffer.isBuffer(value)) {
    return {
      type: 'Buffer',
      length: value.length,
      hexPrefix: value.subarray(0, 64).toString('hex')
    }
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

function clonePacket (value) {
  if (value === null || value === undefined) return value
  if (typeof value === 'bigint') return BigInt(value)
  if (Buffer.isBuffer(value)) return Buffer.from(value)
  if (Array.isArray(value)) return value.map(clonePacket)
  if (typeof value === 'object') {
    const out = {}
    for (const [k, v] of Object.entries(value)) out[k] = clonePacket(v)
    return out
  }
  return value
}

function unwrapInbound (packet, meta) {
  if (meta && typeof meta.name === 'string') {
    return {
      name: meta.name.replace(/^packet_/, ''),
      data: packet
    }
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

function stateOf (data) {
  return data?.state
}

function isState (value, numeric, symbolicNames) {
  if (value === numeric || value === String(numeric)) return true
  if (typeof value !== 'string') return false
  const normalized = value.toLowerCase().replace(/[^a-z0-9]/g, '')
  return symbolicNames.some(
    x => normalized === x.toLowerCase().replace(/[^a-z0-9]/g, '')
  )
}

function isState0 (data) {
  return isState(stateOf(data), 0, ['searching_for_spawn', 'searchingforspawn'])
}

function isState1 (data) {
  return isState(stateOf(data), 1, ['ready_to_spawn', 'readytospawn'])
}

function isState2 (data) {
  return isState(stateOf(data), 2, ['client_ready_to_spawn', 'clientreadytospawn'])
}

function positionCopy (p) {
  if (!p || typeof p !== 'object') return null
  const x = Number(p.x)
  const y = Number(p.y)
  const z = Number(p.z)
  if (![x, y, z].every(Number.isFinite)) return null
  return { x, y, z }
}

function modeUsesZeroRuntime () {
  return MODE === 'zero-runtime' || MODE === 'zero-runtime-server-position'
}

function modeUsesServerPosition () {
  return MODE === 'server-position' || MODE === 'zero-runtime-server-position'
}

function installOnClient (client, options) {
  if (!client || client.__respawnState2FieldLab) return client

  Object.defineProperty(client, '__respawnState2FieldLab', {
    value: true
  })


  const serial = ++clientSerial
  const stamp = iso().replace(/[:.]/g, '-')
  const file = path.join(
    OUTPUT_DIR,
    `respawn-state2-${MODE}-${stamp}-c${serial}.jsonl`
  )

  const capture = openCaptureSink(fs, file, { directory: OUTPUT_DIR })
  let closed = false

  function record (type, data = {}) {
    if (closed) return
    capture.write(type, data)
  }

  function say (...args) {
    console.log('🧬 STATE2-LAB', ...args)
  }

  function closeLog (reason) {
    if (closed) return

    closed = true
    capture.close('capture-complete', { reason, mode: MODE, outputFile: file }, error => {
      if (!error) say(`📁 capture selesai dan file sudah UNLOCKED: ${file}`)
    })
  }

  let selfRuntimeId = null
  let initialSpawnComplete = false

  let deathActive = false
  let deathAt = 0
  let latestServerState0 = null
  let readyAt = 0
  let state2At = 0
  let finalizerAt = 0
  let aliveEvidence = null
  let transformedState2Count = 0
  let observeTimer = null

  const originalQueue = typeof client.queue === 'function'
    ? client.queue
    : null

  const originalWrite = typeof client.write === 'function'
    ? client.write
    : null

  function isSelfRuntime (data) {
    const rid = runtimeOf(data)
    return (
      selfRuntimeId !== null &&
      rid !== null &&
      String(rid) === String(selfRuntimeId)
    )
  }

  function startDeath (data) {
    deathActive = true
    deathAt = Date.now()
    latestServerState0 = null
    readyAt = 0
    state2At = 0
    finalizerAt = 0
    aliveEvidence = null
    transformedState2Count = 0

    if (observeTimer) {
      clearTimeout(observeTimer)
      observeTimer = null
    }

    record('death', {
      selfRuntimeId: selfRuntimeId === null ? null : String(selfRuntimeId),
      data
    })

    say(`💀 actual self death detected (runtime=${selfRuntimeId})`)
  }

  function markAlive (why, data) {
    if (!deathActive || aliveEvidence) return

    aliveEvidence = why

    record('alive-evidence', {
      why,
      mode: MODE,
      afterDeathMs: deathAt ? Date.now() - deathAt : null,
      afterState2Ms: state2At ? Date.now() - state2At : null,
      afterReadyMs: readyAt ? Date.now() - readyAt : null,
      afterFinalizerMs: finalizerAt ? Date.now() - finalizerAt : null,
      data
    })

    say(`✅ SERVER ALIVE evidence: ${why}`)

    if (observeTimer) {
      clearTimeout(observeTimer)
      observeTimer = null
    }

    closeLog(`success:${why}`)
  }

  function armObservationWindow () {
    if (observeTimer) clearTimeout(observeTimer)

    observeTimer = setTimeout(() => {
      if (!aliveEvidence) {
        record('no-alive-evidence', {
          mode: MODE,
          afterDeathMs: deathAt ? Date.now() - deathAt : null,
          afterState2Ms: state2At ? Date.now() - state2At : null,
          afterReadyMs: readyAt ? Date.now() - readyAt : null,
          afterFinalizerMs: finalizerAt ? Date.now() - finalizerAt : null,
          transformedState2Count,
          latestServerState0
        })

        say(
          `❌ tidak ada server-alive evidence ${OBSERVE_MS} ms setelah finalizer`
        )
      }

      closeLog(
        aliveEvidence
          ? `success:${aliveEvidence}`
          : 'timeout-no-alive-evidence'
      )
    }, OBSERVE_MS)
  }

  function transformState2 (params) {
    const original = clonePacket(params)
    const out = clonePacket(params)

    if (modeUsesZeroRuntime()) {
      // Keep representation serializer-friendly and unambiguous.
      out.runtime_entity_id = '0'

      if ('runtimeEntityId' in out) out.runtimeEntityId = '0'
      if ('runtime_id' in out) out.runtime_id = '0'
      if ('runtimeId' in out) out.runtimeId = '0'
    }

    if (modeUsesServerPosition()) {
      const p = positionCopy(latestServerState0?.position)
      if (p) out.position = p
    }

    return { original, transformed: out }
  }

  function onInbound (packet, meta) {
    const { name, data } = unwrapInbound(packet, meta)
    if (!name) return

    if (name === 'start_game') {
      const rid = runtimeOf(data)
      if (rid !== null) selfRuntimeId = rid

      record('start-game', {
        selfRuntimeId:
          selfRuntimeId === null ? null : String(selfRuntimeId),
        currentTick: data?.current_tick,
        engine: data?.engine
      })

      return
    }

    if (name === 'play_status' && data?.status === 'player_spawn') {
      if (!initialSpawnComplete) {
        initialSpawnComplete = true
        record('initial-spawn-complete', { data })
        say('🌍 initial spawn complete; state-2 lab armed')
      } else if (deathActive) {
        markAlive('play_status:player_spawn', data)
      }

      return
    }

    if (!initialSpawnComplete) return

    if (name === 'entity_event' && isSelfRuntime(data)) {
      if (data?.event_id === 'death_animation') {
        startDeath(data)
      } else if (deathActive && data?.event_id === 'respawn') {
        markAlive('entity_event:respawn', data)
      }

      return
    }

    if (!deathActive) return

    if (name === 'respawn') {
      if (isState0(data)) {
        latestServerState0 = {
          position: positionCopy(data?.position),
          runtime_entity_id:
            runtimeOf(data) === null
              ? null
              : String(runtimeOf(data)),
          receivedAt: iso(),
          afterDeathMs: Date.now() - deathAt
        }

        record('server-state0', {
          ...latestServerState0,
          data
        })

        say(
          `⬅️ server state-0 runtime=${latestServerState0.runtime_entity_id} ` +
          `pos=${latestServerState0.position
            ? `${latestServerState0.position.x.toFixed(3)},` +
              `${latestServerState0.position.y.toFixed(3)},` +
              `${latestServerState0.position.z.toFixed(3)}`
            : 'n/a'}`
        )
      }

      if (isState1(data)) {
        readyAt = Date.now()

        record('server-ready', {
          afterDeathMs: readyAt - deathAt,
          runtime_entity_id:
            runtimeOf(data) === null
              ? null
              : String(runtimeOf(data)),
          position: positionCopy(data?.position),
          data
        })

        say(`🟢 ReadyToSpawn at +${readyAt - deathAt} ms`)
      }

      return
    }

    if (name === 'set_entity_data' && isSelfRuntime(data)) {
      const interesting = {}

      for (const m of data?.metadata || []) {
        if (m?.key === 'player_has_died') {
          interesting.player_has_died = m.value
        } else if (m?.key === 'boundingbox_width') {
          interesting.boundingbox_width = m.value
        } else if (m?.key === 'boundingbox_height') {
          interesting.boundingbox_height = m.value
        } else if (m?.key === 'flags') {
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
            markAlive(
              'self-metadata:player_has_died=0',
              interesting
            )
          }
        }
      }

      return
    }

    if (name === 'update_attributes' && isSelfRuntime(data)) {
      const attrs = {}

      for (const a of data?.attributes || []) {
        if (
          a?.name === 'minecraft:health' ||
          a?.name === 'minecraft:movement'
        ) {
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
          say(
            `❤️ server self health=${attrs['minecraft:health']}`
          )

          if (Number(attrs['minecraft:health']) > 0) {
            markAlive(
              'update_attributes:health>0',
              attrs
            )
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
      const horizontal = Math.hypot(dx, dz)

      record('movement-feedback', {
        afterDeathMs: Date.now() - deathAt,
        afterFinalizerMs:
          finalizerAt ? Date.now() - finalizerAt : null,
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

    client[methodName] = function wrapped (
      name,
      params,
      ...rest
    ) {
      const normalized = String(name || '')
        .replace(/^packet_/, '')

      let sendParams = params

      try {
        if (
          normalized === 'respawn' &&
          initialSpawnComplete &&
          deathActive &&
          isState2(params)
        ) {
          const {
            original: before,
            transformed
          } = transformState2(params)

          sendParams = transformed
          transformedState2Count++
          state2At = Date.now()

          record('state2-out', {
            mode: MODE,
            via: methodName,
            count: transformedState2Count,
            afterDeathMs: state2At - deathAt,
            latestServerState0,
            before,
            after: transformed
          })

          say(
            `➡️ state-2 ${MODE}: ` +
            `runtime ${String(runtimeOf(before))} → ` +
            `${String(runtimeOf(transformed))}; ` +
            `pos ${JSON.stringify(before?.position)} → ` +
            `${JSON.stringify(transformed?.position)}`
          )
        }

        if (
          normalized === 'player_action' &&
          initialSpawnComplete &&
          deathActive &&
          String(runtimeOf(params)) === String(selfRuntimeId) &&
          (
            params?.action === 'stop_sleeping' ||
            params?.action === 6 ||
            params?.action === '6'
          )
        ) {
          const now = Date.now()

          record('finalizer-out', {
            afterDeathMs: now - deathAt,
            afterState2Ms: state2At
              ? now - state2At
              : null,
            afterReadyMs: readyAt
              ? now - readyAt
              : null,
            via: methodName,
            data: params
          })

          if (!finalizerAt) {
            finalizerAt = now

            say(
              `➡️ existing wire-6 finalizer observed ` +
              `(${readyAt ? now - readyAt : 'n/a'} ms after ReadyToSpawn)`
            )

            armObservationWindow()
          }
        }
      } catch (err) {
        record('observer-error', {
          where: `outbound:${methodName}`,
          error: err?.stack || String(err)
        })

        say(
          `⚠️ observer error in ${methodName}: ` +
          `${err?.message || err}`
        )
      }

      return original.call(
        this,
        name,
        sendParams,
        ...rest
      )
    }
  }

  wrapOutbound('queue', originalQueue)
  wrapOutbound('write', originalWrite)

  function shutdown (reason) {
    if (observeTimer) {
      clearTimeout(observeTimer)
      observeTimer = null
    }

    record('stop', {
      reason,
      mode: MODE,
      deathActive,
      aliveEvidence,
      transformedState2Count
    })

    closeLog(`shutdown:${reason}`)
  }

  client.once('close', () => shutdown('close'))
  client.once('end', () => shutdown('end'))

  record('start', {
    version: '1.7.21-state2-field-lab',
    mode: MODE,
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

  say(`v1.7.21 loaded; mode=${MODE}`)
  say('NO loading-screen/interact/init replay will be sent')
  say(`compact one-shot log → ${file}`)

  return client
}

function install () {
  if (!VALID_MODES.has(MODE)) {
    console.error(
      `STATE2-LAB: invalid MC_RESPAWN_STATE2_MODE="${MODE}"`
    )
    console.error(
      `STATE2-LAB: valid modes: ${Array.from(VALID_MODES).join(', ')}`
    )
    process.exitCode = 2
    return
  }

  let bp

  try {
    bp = require('bedrock-protocol')
  } catch (err) {
    console.error(
      'STATE2-LAB: cannot require bedrock-protocol:',
      err
    )
    return
  }

  if (!bp || typeof bp.createClient !== 'function') {
    console.error(
      'STATE2-LAB: bedrock-protocol.createClient() unavailable'
    )
    return
  }

  if (bp.createClient.__respawnState2FieldLabWrapped) {
    return
  }

  const originalCreateClient = bp.createClient

  function wrappedCreateClient (options, ...rest) {
    return installOnClient(
      originalCreateClient.call(this, options, ...rest),
      options
    )
  }

  Object.defineProperty(
    wrappedCreateClient,
    '__respawnState2FieldLabWrapped',
    { value: true }
  )

  bp.createClient = wrappedCreateClient
}

install()
