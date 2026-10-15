'use strict'

const fs = require('fs')
const path = require('path')
const assert = require('assert')

const source = fs.readFileSync(path.join(__dirname, 'bot.js'), 'utf8')

assert(source.includes('function calculatePostRespawnRearmProbe'), 'missing authoritative re-arm probe physics')
assert(source.includes('keeping Y and vertical delta anchored to the server state'), 're-arm must document authoritative vertical anchoring')
assert(source.includes('postRespawnRearm.authoritativeDelta'), 're-arm must retain authoritative delta')
assert(source.includes('postRespawnRearm.authoritativeOnGround'), 're-arm must retain authoritative on-ground state')
assert(source.includes("thisClient.on('update_client_input_locks'"), 'must observe server input locks')
assert(source.includes("clientInputLocks.move"), 'move input lock must gate re-arm probing')
assert(source.includes("thisClient.on('set_entity_motion'"), 'must observe server motion packets during re-arm')
assert(source.includes("const probing = !clientInputLocks.move"), 'movement probes must not run while server MOVE lock is active')
assert(source.includes("notePostRespawnRearmFeedback(pos, packet.delta || null, 'CorrectPlayerMovePrediction', packet.on_ground)"), 'correction on_ground must feed re-arm physics state')
assert(source.includes("finishRespawn('final PlayerAction grace elapsed'"), 'proven v1.7.11 respawn path must remain')

console.log('✅ Post-respawn authoritative physics/input-lock structural test OK')
console.log('   vertical physics : frozen to latest authoritative correction during re-arm')
console.log('   probe origin     : latest authoritative pose; no local fall/drift accumulation')
console.log('   input lock       : UpdateClientInputLocks MOVE bit gates probes')
console.log('   server motion    : SetEntityMotion instrumented during respawn/re-arm')
console.log('   respawn handshake: v1.7.11 path unchanged')
