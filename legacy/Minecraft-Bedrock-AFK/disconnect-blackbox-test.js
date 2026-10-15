'use strict'

const assert = require('assert')
const { DisconnectBlackBox, packetName } = require('./disconnect-blackbox')

assert.strictEqual(packetName({ data: { name: 'tick_sync' } }), 'tick_sync')
assert.strictEqual(packetName({ name: 'move_player' }), 'move_player')

const remote = new DisconnectBlackBox({ username: 'I/O-TEST', host: 'example', port: 19132, pingTimeoutMs: 7000 })
remote.markPhase('connect')
remote.markPhase('join')
remote.markPhase('spawn')
remote.recordPacket({ data: { name: 'network_stack_latency' } })
remote.recordHeartbeat()
remote.recordError(Object.assign(new Error('socket closed'), { code: 'ECONNRESET' }))
let report = remote.buildReport({ finalTransport: 'raknet', dead: false })
assert.strictEqual(report.classification, 'transport-or-protocol-error')
assert.strictEqual(report.lastPacket.name, 'network_stack_latency')
assert.strictEqual(report.state.finalTransport, 'raknet')

const respawn = new DisconnectBlackBox({ username: 'I/O-TEST', host: 'example', port: 19132 })
respawn.markLocalDisconnect('Respawn handshake timeout', 'respawn-timeout')
report = respawn.buildReport({ dead: true, respawnAttempts: 4 })
assert.strictEqual(report.classification, 'local-respawn-timeout')
assert.strictEqual(report.localDisconnect.source, 'respawn-timeout')

const kicked = new DisconnectBlackBox({ username: 'I/O-TEST', host: 'example', port: 19132 })
kicked.recordKick({ reason: 'server_id_conflict' })
report = kicked.buildReport({ dead: false })
assert.strictEqual(report.classification, 'server-kick')
assert.strictEqual(report.lastKick.reason.reason, 'server_id_conflict')

{
  const box = new DisconnectBlackBox({ pingTimeoutMs: 7000 })
  box.recordViolation({ violation_type: 'malformed', severity: 'terminating', packet_id: 144, reason: 'bad packet' })
  const report = box.buildReport({ dead: true }, [])
  assert.strictEqual(report.classification, 'server-protocol-violation')
  assert.strictEqual(report.lastViolation.packet.severity, 'terminating')
}

console.log('✅ Disconnect black-box unit test OK')
console.log('   transport error     : classified')
console.log('   respawn timeout     : distinguished as local')
console.log('   server kick         : distinguished as remote')
console.log('   packet/heartbeat    : captured')
