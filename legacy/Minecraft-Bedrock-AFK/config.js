'use strict'

const crypto = require('crypto')
const fs = require('fs')
const path = require('path')

const INSTANCE_ID_FILE = process.env.BOT_INSTANCE_FILE || path.join(__dirname, '.bot-instance-id')

function normalizeInstanceId (value) {
  if (value === undefined || value === null) return null
  const normalized = String(value)
    .trim()
    .toUpperCase()
    .replace(/[^A-Z0-9_-]/g, '')
    .slice(0, 16)
  return normalized || null
}

function generateInstanceId () {
  // 32-bit random ID: short enough for a Bedrock username, while making
  // accidental collisions between independent installations extremely unlikely.
  return crypto.randomBytes(4).toString('hex').toUpperCase()
}

function readPersistedInstanceId () {
  try {
    return normalizeInstanceId(fs.readFileSync(INSTANCE_ID_FILE, 'utf8'))
  } catch (error) {
    if (error && error.code !== 'ENOENT') {
      console.warn(`⚠️ Gagal membaca instance ID ${INSTANCE_ID_FILE}: ${error.message}`)
    }
    return null
  }
}

function resolveInstanceIdentity () {
  const forced = normalizeInstanceId(process.env.BOT_INSTANCE_ID)
  if (forced) {
    return { id: forced, source: 'environment', persistent: true, file: INSTANCE_ID_FILE }
  }

  const persisted = readPersistedInstanceId()
  if (persisted) {
    return { id: persisted, source: 'file', persistent: true, file: INSTANCE_ID_FILE }
  }

  const generated = generateInstanceId()

  try {
    fs.mkdirSync(path.dirname(INSTANCE_ID_FILE), { recursive: true })
    // Atomic create: if another process created the file between our read/write,
    // use that persisted value instead of silently diverging.
    fs.writeFileSync(INSTANCE_ID_FILE, `${generated}\n`, { encoding: 'utf8', flag: 'wx' })
    return { id: generated, source: 'generated', persistent: true, file: INSTANCE_ID_FILE }
  } catch (error) {
    if (error && error.code === 'EEXIST') {
      const raced = readPersistedInstanceId()
      if (raced) return { id: raced, source: 'file-race', persistent: true, file: INSTANCE_ID_FILE }
    }

    console.warn(`⚠️ Instance ID tidak bisa disimpan; memakai ID sementara ${generated}: ${error.message}`)
    return { id: generated, source: 'ephemeral', persistent: false, file: INSTANCE_ID_FILE }
  }
}

const resolvedInstance = resolveInstanceIdentity()
const baseUsername = String(process.env.MC_USERNAME || 'I/O').trim() || 'I/O'
// Keep room for "-XXXXXXXX". Bedrock names accepted by offline servers can vary,
// so stay conservatively under 32 characters.
const usernameBaseMax = Math.max(1, 32 - 1 - resolvedInstance.id.length)
const username = `${baseUsername.slice(0, usernameBaseMax)}-${resolvedInstance.id}`

module.exports = {
  instance: {
    id: resolvedInstance.id,
    source: resolvedInstance.source,
    persistent: resolvedInstance.persistent,
    file: resolvedInstance.file,
    baseUsername,
    username
  },

  server: {
    host: process.env.MC_HOST || 'Nifirealm.aternos.me',
    port: Number(process.env.MC_PORT || 24128),
    username,
    offline: true,

    // 'auto' = biarkan bedrock-protocol mencoba discovery RakNet + NetherNet.
    // Bisa dipaksa menjadi 'raknet' atau 'nethernet' lewat MC_TRANSPORT.
    transport: process.env.MC_TRANSPORT || 'auto',
    connectTimeout: 15000,
    pingTimeout: 7000,
    followPort: true
  },

  movement: {
    tickMs: 50,
    walkSpeed: 0.10,
    swimSpeed: 0.065,
    turnDegreesPerTick: 3.0,
    jumpVelocity: 0.42,
    gravity: 0.08,
    verticalDrag: 0.98,
    swimRisePerTick: 0.055
  },

  behavior: {
    walkMinMs: 5000,
    walkMaxMs: 12000,
    idleChance: 0.08,
    idleMinMs: 800,
    idleMaxMs: 2200,
    gentleTurnMinDeg: -25,
    gentleTurnMaxDeg: 25
  },

  perception: {
    entityRadius: 3.5,
    forwardEntityDistance: 1.9,
    obstacleDistance: 0.9,
    obstacleCheckIntervalMs: 150
  },

  pathfinding: {
    enabled: true,
    lookAhead: 4.0,
    sampleStep: 0.5,
    candidateAngles: [0, -15, 15, -30, 30, -45, 45, -60, 60, -90, 90],
    cliffCheck: true,
    waterPenalty: 0.35
  },

  survival: {
    enabled: true,
    hostileAvoidanceRadius: 9.0,
    panicRadius: 4.5,
    threatLockMs: 2600,
    respawnInitialDelayMs: 200,
    respawnActionDelayMs: 200,
    respawnFinalizeGraceMs: 1200,
    respawnRetryMs: 1400,
    respawnMaxRetries: 4,
    respawnTimeoutMs: 9000
  },

  swimming: {
    normalRisePerTick: 0.035,
    emergencyRisePerTick: 0.08,
    surfaceBiasPerTick: 0.002,
    emergencyAfterMs: 1800,
    emergencyAirThreshold: 260,
    escapeHeadingHoldMs: 5500
  },

  recovery: {
    stuckCheckMs: 1500,
    authoritativeFreshMs: 3000,
    minProgress: 0.12,
    minServerHorizontalSpeed: 0.025,
    samplesBeforeHardRecovery: 4,

    authoritativeRebaseHoldMs: 1800,
    maxPredictionDrift: 0.35,
    serverRejectSamples: 2,
    serverRejectMinCorrectionError: 0.60,
    serverRejectMaxAuthoritativeStep: 0.08,
    serverRejectResetStep: 0.20,
    serverBlockedHeadingHoldMs: 5500,
    serverBlockedHeadingRadiusDeg: 40,
    serverBlockedHeadingPenalty: 2.6
  },

  reconnect: {
    enabled: true,
    delaysMs: [2000, 5000, 10000, 20000, 30000]
  },

  safety: {
    // Default fail-safe: bot AFK ini tidak boleh merusak/mengubah world atau menyerang.
    // Untuk menonaktifkan guard harus eksplisit memakai token panjang ini; sengaja
    // tidak memakai BOT_ALLOW_DESTRUCTIVE=1 agar tidak mudah ter-disable tanpa sengaja.
    nonDestructive: process.env.BOT_ALLOW_DESTRUCTIVE !== 'I_UNDERSTAND_AND_ACCEPT_WORLD_CHANGES',
    logBlockedPackets: true
  },

  world: {
    chunkKeepRadius: 10
  },

  debug: {
    logChunks: false,
    logAi: true,
    logMovement: false,
    logPackets: false,

    diagnostics: true,
    diagnosticsIntervalMs: 10000,
    diagnosticsOnStuck: true,
    diagnosticsNearbyRadius: 6,
    correctionLogEvery: 100,
    largeCorrectionDistance: 0.75,
    predictionHistorySize: 256,
    logHealthChanges: true,
    logRawDeathPackets: true,

    // Disconnect black-box: satu JSON line per disconnect, plus report ke console.
    // File disimpan di folder project supaya mudah ikut workflow overlay/backup.
    disconnectBlackBox: process.env.BOT_DISCONNECT_BLACKBOX !== '0',
    disconnectBlackBoxFile: process.env.BOT_DISCONNECT_LOG || path.join(__dirname, 'disconnect-blackbox.jsonl'),
    disconnectBlackBoxMaxBytes: Number(process.env.BOT_DISCONNECT_LOG_MAX_BYTES || 1024 * 1024),
    disconnectPacketHistory: Number(process.env.BOT_DISCONNECT_PACKET_HISTORY || 24)
  },

  testing: {
    forceJumpTest: false,
    forceJumpIntervalMs: 3000
  }
}
