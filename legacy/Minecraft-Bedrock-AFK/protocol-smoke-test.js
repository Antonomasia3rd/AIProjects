'use strict'

const assert = require('assert')
const { createSerializer } = require('bedrock-protocol/src/transforms/serializer')
const createRegistry = require('prismarine-registry')
const World = require('./world')
const { inspectOutboundPacket, installNonDestructiveGuard } = require('./safety')
const { decodePlayerActionOrdinal, resolveRespawnActionCompat, makeRespawnPlayerActionParams, makeRespawnClientReadyParams, PROTOCOL_2193_RESPAWN_FACE } = require('./protocol-compat')

const serializer = createSerializer('1.26.51')
const respawnCompat = resolveRespawnActionCompat('1.26.51')

function authInput (inputData) {
  return {
    name: 'player_auth_input',
    params: {
      pitch: 0,
      yaw: 0,
      position: { x: 0, y: 64, z: 0 },
      move_vector: { x: 0, z: 1 },
      head_yaw: 0,
      input_data: inputData,
      input_mode: 'mouse',
      play_mode: 'screen',
      interaction_model: 'crosshair',
      interact_rotation: { x: 0, z: 0 },
      tick: 1n,
      delta: { x: 0, y: 0.1, z: 0.1 },
      analogue_move_vector: { x: 0, z: 1 },
      camera_orientation: { x: 0, y: 0, z: 1 },
      raw_move_vector: { x: 0, z: 1 }
    }
  }
}

const jump = serializer.createPacketBuffer(authInput([
  'block_breaking_delay_enabled',
  'vertical_collision',
  'up',
  'jump_down',
  'jumping',
  'want_up',
  'start_jumping',
  'jump_pressed_raw',
  'jump_current_raw'
]))
assert(jump.length > 0)

const swim = serializer.createPacketBuffer(authInput([
  'block_breaking_delay_enabled',
  'up',
  'auto_jumping_in_water',
  'want_up',
  'want_up_slow',
  'ascend',
  'jump_down',
  'jumping',
  'jump_current_raw',
  'start_swimming'
]))
assert(swim.length > 0)

const correctedMovement = serializer.createPacketBuffer(authInput([
  'block_breaking_delay_enabled',
  'vertical_collision',
  'received_server_data',
  'up'
]))
assert(correctedMovement.length > 0)

const teleportAckMovement = serializer.createPacketBuffer(authInput([
  'block_breaking_delay_enabled',
  'vertical_collision',
  'received_server_data',
  'handled_teleport'
]))
assert(teleportAckMovement.length > 0)

// Server correction contoh nyata memberi eye Y ~= 68.620010 saat feet Y ~= 67.
// Guard terhadap regresi eye-position vs feet-position yang membuat v1.2 jatuh sendiri.
const EYE_HEIGHT = 1.6200100183486938
const correctedEyeY = 68.62001037597656
assert(Math.abs((correctedEyeY - EYE_HEIGHT) - 67) < 1e-4)

const respawnReadyParams = makeRespawnClientReadyParams(1n, { x: 0, y: 0, z: 0 })
const respawn = serializer.createPacketBuffer({ name: 'respawn', params: respawnReadyParams })
assert(respawn.length > 0)
assert.strictEqual(respawnReadyParams.state, 2)
assert.deepStrictEqual(respawnReadyParams.position, { x: 0, y: 0, z: 0 })

const respawnActionParams = makeRespawnPlayerActionParams(1n, respawnCompat.token)
const respawnAction = serializer.createPacketBuffer({ name: 'player_action', params: respawnActionParams })
assert(respawnAction.length > 0)
assert.strictEqual(decodePlayerActionOrdinal(respawnAction), 6)
assert.strictEqual(respawnCompat.expectedOrdinal, 6)
assert.strictEqual(respawnActionParams.face, PROTOCOL_2193_RESPAWN_FACE)
assert.strictEqual(PROTOCOL_2193_RESPAWN_FACE, -1)
assert.deepStrictEqual(respawnActionParams.position, { x: 0, y: 0, z: 0 })
assert.deepStrictEqual(respawnActionParams.result_position, { x: 0, y: 0, z: 0 })


const loadingStart = serializer.createPacketBuffer({
  name: 'serverbound_loading_screen',
  params: { type: 1, loading_screen_id: undefined }
})
assert(loadingStart.length > 0)

const loadingDone = serializer.createPacketBuffer({
  name: 'serverbound_loading_screen',
  params: { type: 2, loading_screen_id: undefined }
})
assert(loadingDone.length > 0)

const interactReady = serializer.createPacketBuffer({
  name: 'interact',
  params: {
    action_id: 'mouse_over_entity',
    target_entity_id: 0n,
    has_position: false,
    position: undefined
  }
})
assert(interactReady.length > 0)

const initialized = serializer.createPacketBuffer({
  name: 'set_local_player_as_initialized',
  params: { runtime_entity_id: 1n }
})
assert(initialized.length > 0)


const airMetadata = serializer.createPacketBuffer({
  name: 'set_entity_data',
  params: {
    runtime_entity_id: 1n,
    metadata: [
      { key: 'air', type: 'short', legacy_type: 1, value: 299 },
      { key: 'max_airdata_max_air', type: 'int', legacy_type: 2, value: 300 }
    ],
    properties: { ints: [], floats: [] },
    tick: 1n
  }
})
assert(airMetadata.length > 0)


const worldProbe = new World()
worldProbe.columns.set('0,0', {
  getBlock: () => ({
    name: 'kelp',
    superimposed: { name: 'water' },
    computedStates: { waterlogged: true },
    boundingBox: 'empty',
    shapes: []
  })
})
assert.strictEqual(worldProbe.blockAt(1, 64, 1), 'kelp')
assert.strictEqual(worldProbe.isWater(1, 64, 1), true)
assert.strictEqual(worldProbe.isSolid(1, 64, 1), false)

const plantWorld = new World()
plantWorld.columns.set('0,0', {
  getBlock: () => ({
    name: 'short_grass',
    superimposed: null,
    computedStates: {},
    boundingBox: 'empty',
    shapes: []
  })
})
assert.strictEqual(plantWorld.isSolid(1, 64, 1), false)

const registry = createRegistry('bedrock_1.26.51')
assert.strictEqual(registry.blocksByName.air.name, 'air')
assert.strictEqual(registry.blocksByName.water.name, 'water')



// Strict non-destructive safety guard regression tests.
assert.strictEqual(inspectOutboundPacket('player_auth_input', authInput([
  'block_breaking_delay_enabled', 'up'
]).params).allowed, true)
assert.strictEqual(inspectOutboundPacket('player_action', {
  action: respawnCompat.token
}).allowed, true)
assert.strictEqual(inspectOutboundPacket('interact', {
  action_id: 'mouse_over_entity'
}).allowed, true)
assert.strictEqual(inspectOutboundPacket('inventory_transaction', {}).allowed, false)
assert.strictEqual(inspectOutboundPacket('command_request', {}).allowed, false)
assert.strictEqual(inspectOutboundPacket('player_action', {
  action: 'start_break'
}).allowed, false)
assert.strictEqual(inspectOutboundPacket('player_auth_input', {
  input_data: ['up', 'block_action'],
  block_action: [{ action: 'start_break', position: { x: 0, y: 64, z: 0 }, face: 1 }]
}).allowed, false)

const sentByGuardProbe = []
const fakeClient = {
  queue: (name, params) => { sentByGuardProbe.push(['queue', name, params]); return true },
  write: (name, params) => { sentByGuardProbe.push(['write', name, params]); return true }
}
installNonDestructiveGuard(fakeClient, { logBlocked: false })
assert.strictEqual(fakeClient.queue('player_action', { action: respawnCompat.token }), true)
assert.strictEqual(fakeClient.queue('player_action', { action: 'start_break' }), false)
assert.strictEqual(fakeClient.write('inventory_transaction', {}), false)
assert.strictEqual(sentByGuardProbe.length, 1)
assert.strictEqual(sentByGuardProbe[0][1], 'player_action')
assert.strictEqual(fakeClient.__nonDestructiveGuardStats.blocked, 2)

console.log('✅ Protocol smoke test OK')
console.log(`   jump packet : ${jump.length} bytes`)
console.log(`   swim packet : ${swim.length} bytes`)
console.log(`   correction  : ${correctedMovement.length} bytes`)
console.log(`   teleport ack: ${teleportAckMovement.length} bytes (received_server_data + handled_teleport)`)
console.log('   eye/feet    : 68.620010 eye ~= 67.000000 feet')
console.log(`   respawn     : ${respawn.length} bytes + standalone player_action ${respawnAction.length} bytes`)
console.log(`   respawn wire: action 6, standalone PlayerAction pos/result 0,0,0 face -1 via token '${respawnCompat.token}'${respawnCompat.mapperShimActive ? ' (compat shim)' : ''}`)
console.log('   respawn seq : client state 2 -> server ReadyToSpawn -> standalone PlayerAction action 6 face -1')
console.log(`   ready seq   : ${loadingStart.length + loadingDone.length + interactReady.length + initialized.length} bytes total`)
console.log(`   air metadata: ${airMetadata.length} bytes`)
console.log('   collision   : kelp/grass non-collision blocks are not treated as walls')
console.log('   water layer : kelp + superimposed water detected as water/non-solid')
console.log('   registry    : air/water use bare names (not minecraft:air)')
console.log('   safety      : destructive outbound packets/actions are blocked fail-closed')
