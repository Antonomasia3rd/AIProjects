'use strict'

const fs = require('fs')
const path = require('path')
const assert = require('assert')

const source = fs.readFileSync(path.join(__dirname, 'bot.js'), 'utf8')

assert(source.includes('function beginPostRespawnSync'), 'missing post-respawn sync entry point')
assert(source.includes('function completePostRespawnSync'), 'missing post-respawn sync completion')
assert(source.includes('ai.swimming || postRespawnSync.active || postRespawnRearm.active'), 'stuck detection must be suppressed during post-respawn sync/re-arm')
assert(source.includes("move_vector: { x: 0, z: 0 }"), 'post-respawn sync must send neutral movement')
assert(source.includes("const delta = { x: 0, y: 0, z: 0 }"), 'post-respawn sync must send zero delta')
assert(source.includes("notePostRespawnFeedback(pos, 'CorrectPlayerMovePrediction')"), 'correction feedback must feed post-respawn sync')
assert(source.includes('notePostRespawnFeedback(pos, `MovePlayer/${packet.mode'), 'MovePlayer feedback must feed post-respawn sync')
assert(source.includes('if (postRespawnSync.active)'), 'movement path must gate on sync state')
assert(source.includes("finishRespawn('final PlayerAction grace elapsed'"), 'v1.7.11 proven respawn handshake/grace path must remain')
assert(source.includes('face=${PROTOCOL_2193_RESPAWN_FACE}'), 'proven standalone PlayerAction payload must remain')

console.log('✅ Post-respawn sync structural test OK')
console.log('   respawn handshake : v1.7.11 path retained')
console.log('   settle movement   : neutral PlayerAuthInput')
console.log('   correction path   : authoritative rebase captured')
console.log('   stuck recovery    : suppressed during settle/re-arm')
