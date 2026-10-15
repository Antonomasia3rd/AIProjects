'use strict'

const { createSerializer } = require('bedrock-protocol/src/transforms/serializer')

const DEFAULT_VERSION = '1.26.51'
const PROTOCOL_2193_RESPAWN_ACTION = 6
const PROTOCOL_2193_RESPAWN_FACE = -1

function readVaruint (buffer, start = 0) {
  let value = 0n
  let shift = 0n
  let offset = start

  while (offset < buffer.length) {
    const byte = BigInt(buffer[offset++])
    value |= (byte & 0x7fn) << shift
    if ((byte & 0x80n) === 0n) return { value, offset }
    shift += 7n
    if (shift > 70n) throw new Error('varuint too large while decoding protocol probe')
  }

  throw new Error('truncated varuint while decoding protocol probe')
}

function zigzagDecode32 (value) {
  const n = Number(value)
  return (n >>> 1) ^ -(n & 1)
}

function decodePlayerActionOrdinal (buffer) {
  // Packet buffer order for player_action:
  // packet header varuint, runtime_entity_id varuint64, action zigzag32, ...
  const header = readVaruint(buffer, 0)
  const runtime = readVaruint(buffer, header.offset)
  const action = readVaruint(buffer, runtime.offset)
  return zigzagDecode32(action.value)
}

function makeProbePacket (serializer, action) {
  return serializer.createPacketBuffer({
    name: 'player_action',
    params: {
      runtime_entity_id: 1n,
      action,
      position: { x: 0, y: 0, z: 0 },
      result_position: { x: 0, y: 0, z: 0 },
      face: PROTOCOL_2193_RESPAWN_FACE
    }
  })
}


function makeRespawnPlayerActionParams (runtimeEntityId, actionToken) {
  return {
    runtime_entity_id: runtimeEntityId,
    action: actionToken,
    position: { x: 0, y: 0, z: 0 },
    result_position: { x: 0, y: 0, z: 0 },
    // The working Mineflayer-Bedrock respawn flow for this legacy standalone
    // PlayerAction uses face=-1 with zero block/result positions.
    face: PROTOCOL_2193_RESPAWN_FACE
  }
}

function makeRespawnAuthBlockAction (actionToken) {
  // Retained only for compatibility/testing. This path is not used for respawn
  // on the target BDS because StartGame advertises server-authoritative block
  // breaking as disabled.
  return {
    action: actionToken,
    position: { x: 0, y: 0, z: 0 },
    face: 0
  }
}

function makeRespawnClientReadyParams (runtimeEntityId, position) {
  const p = position || { x: 0, y: 0, z: 0 }
  return {
    position: { x: Number(p.x) || 0, y: Number(p.y) || 0, z: Number(p.z) || 0 },
    state: 2,
    runtime_entity_id: runtimeEntityId
  }
}

function resolveRespawnActionCompat (version = DEFAULT_VERSION) {
  const serializer = createSerializer(version)
  const candidates = ['respawn', 'stop_sleeping', 'jump']
  const observed = {}

  for (const token of candidates) {
    try {
      observed[token] = decodePlayerActionOrdinal(makeProbePacket(serializer, token))
    } catch (error) {
      observed[token] = { error: error?.message || String(error) }
    }
  }

  // Protocol 2193 / 1.26.51 expects PlayerAction.Respawn = 6.
  // minecraft-data's current latest mapper may incorrectly map symbolic
  // `respawn` to 7 (the 1.26.60-preview value). Pick whichever symbolic token
  // serialises to the required wire ordinal so the safety guard can stay
  // fail-closed on raw numeric action IDs.
  const expectedOrdinal = PROTOCOL_2193_RESPAWN_ACTION
  const token = candidates.find(name => observed[name] === expectedOrdinal)

  if (!token) {
    throw new Error(
      `Unable to encode Bedrock 1.26.51 respawn action ${expectedOrdinal}; mapper probe=${JSON.stringify(observed)}`
    )
  }

  return {
    version,
    token,
    expectedOrdinal,
    symbolicRespawnOrdinal: observed.respawn,
    observed,
    mapperShimActive: token !== 'respawn'
  }
}

module.exports = {
  PROTOCOL_2193_RESPAWN_ACTION,
  PROTOCOL_2193_RESPAWN_FACE,
  decodePlayerActionOrdinal,
  resolveRespawnActionCompat,
  makeRespawnPlayerActionParams,
  makeRespawnAuthBlockAction,
  makeRespawnClientReadyParams
}
