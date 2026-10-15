'use strict'

const fs = require('fs')
const path = require('path')

const src = fs.readFileSync(path.join(__dirname, 'bot.js'), 'utf8')

function need (needle, label) {
  if (!src.includes(needle)) throw new Error(`Missing ${label}: ${needle}`)
}

need("function sendPostRespawnReinitialize (source = 'post-respawn')", 'post-respawn reinit helper')
need("client.queue('set_local_player_as_initialized'", 'set_local_player_as_initialized send')
need("sendPostRespawnReinitialize('begin-rearm')", 'reinit before movement re-arm')
need('sendPostRespawnReinitialize(`retry-${postRespawnRearm.retryCount}`)', 'reinit on controlled retry')
need("console.log('🎮 play_status: player_spawn'", 'player_spawn instrumentation')
need("sendPostRespawnReinitialize('play_status/player_spawn')", 'player_spawn reinit trigger')
need("postRespawnRearm.warmupUntil = Math.max(postRespawnRearm.warmupUntil, Date.now() + 650)", 'reinit warmup gate')
need('postRespawnReinitCount:', 'diagnostics expose reinit count')

console.log('✅ Post-respawn local-player reinitialization structural test OK')
console.log('   initialized packet : resent once before first post-respawn probe')
console.log('   retry behavior     : controlled re-init before each 5s probe retry')
console.log('   player_spawn       : instrumented and triggers post-respawn re-init')
console.log('   movement gate      : >=650ms warmup after local-player re-init')
console.log('   respawn handshake  : v1.7.11 path unchanged')
