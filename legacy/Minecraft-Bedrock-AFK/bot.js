'use strict'

const bedrock = require('bedrock-protocol')
const World = require('./world')
const config = require('./config')
const { installNonDestructiveGuard } = require('./safety')
const { createDisconnectBlackBox } = require('./disconnect-blackbox')
const { resolveRespawnActionCompat, makeRespawnPlayerActionParams, makeRespawnClientReadyParams, PROTOCOL_2193_RESPAWN_FACE } = require('./protocol-compat')
const packageVersion = require('./package.json').version

const { instance, server, movement, behavior, perception, pathfinding, survival, swimming, recovery, reconnect, safety, debug, testing } = config
const respawnActionCompat = resolveRespawnActionCompat('1.26.51')

// Bedrock movement packets use the player's eye/movement position, not feet position.
// Vanilla standing eye height for the current Bedrock line (1.26.x).
const EYE_HEIGHT = 1.6200100183486938
const GROUND_SLIPPERINESS = 0.60000002
const AIR_FRICTION_XZ = 0.91000003
const GROUND_FRICTION_XZ = 0.54600006
const PLAYER_HALF_WIDTH = 0.30
const COLLISION_INSET = 0.02

const HOSTILE_ENTITY_TYPES = new Set([
  'zombie', 'zombie_villager', 'drowned', 'husk', 'skeleton', 'stray', 'bogged',
  'creeper', 'spider', 'cave_spider', 'witch', 'phantom', 'slime', 'magma_cube',
  'pillager', 'vindicator', 'evoker', 'vex', 'ravager', 'guardian', 'elder_guardian',
  'silverfish', 'endermite', 'blaze', 'ghast', 'wither_skeleton', 'piglin_brute',
  'hoglin', 'zoglin', 'shulker', 'warden', 'breeze', 'wither'
])


let client = null
let world = null
let runtimeId = null
let pos = null
let yaw = 0
let pitch = 0
let tick = 0n
let startGameCurrentTick = null
let rewindHistorySize = null
let movementTickSeeded = false
let spawned = false
let shuttingDown = false
let reconnectAttempt = 0
let reconnectTimer = null
let readyHandshakeSent = false
let movementPacketsSent = 0
let movementCorrections = 0
let sawDeathState = false
let hasEverSpawned = false
let onGround = false
let receivedServerData = false
let handledTeleportPending = false
let handledTeleportReason = null
let handledTeleportAckSentAt = 0
let clientInputLocks = {
  raw: null,
  move: false,
  jump: false,
  sneak: false,
  mount: false,
  dismount: false,
  rotation: false,
  updatedAt: 0
}
let movementReady = false
let serverAir = null
let serverMaxAir = 300
let authoritativePos = null
let lastAuthoritativeAt = 0
let lastServerDelta = { x: 0, y: 0, z: 0 }
let lastCorrectionAt = 0
let totalAuthoritativeDistance = 0
let currentHealth = 20
let lastHealth = 20
let lastDamageAt = 0
let lastDamageAmount = 0
let lastDeathInfo = null
let lastThreatSnapshot = null
let floorDecodeWarningShown = false
let correctionRejectStreak = 0
let disconnectBlackBox = null
let lastCorrectionAuthoritativePos = null

const predictionHistory = new Map()
const lifecycle = {
  dead: false,
  deathAt: 0,
  deathReason: null,
  deathMessages: [],
  respawnRequestSent: false,
  respawnReadyReceived: false,
  respawnActionSent: false,
  respawnClientReadySent: false,
  respawnSearchingReceived: false,
  respawnAttempts: 0,
  respawnPosition: null,
  respawnLastState: null,
  respawnStateHistory: [],
  respawnActionLastAt: 0,
  respawnActionChannel: null,
  respawnActionTick: null,
  finalizePending: false
}

const postRespawnSync = {
  active: false,
  startedAt: 0,
  minUntil: 0,
  maxUntil: 0,
  feedbackSeen: false,
  firstFeedbackAt: 0,
  lastFeedbackAt: 0,
  feedbackCount: 0,
  initialPosition: null,
  latestPosition: null,
  largestShift: 0,
  source: null
}

const postRespawnRearm = {
  active: false,
  startedAt: 0,
  maxUntil: 0,
  warmupUntil: 0,
  baselinePosition: null,
  latestPosition: null,
  probeStartedAt: 0,
  probePackets: 0,
  feedbackCount: 0,
  largestServerStep: 0,
  source: null,
  firstPacketLogged: false,
  retryCount: 0,
  authoritativeOnGround: false,
  authoritativeDelta: { x: 0, y: 0, z: 0 },
  inputLockWaitSince: 0,
  inputLockWaitLogged: false,
  reinitCount: 0,
  reinitLastAt: 0,
  reinitLastSource: null
}

const diagnostics = {
  windowStartedAt: Date.now(),
  packets: 0,
  corrections: 0,
  correctionErrorSum: 0,
  correctionErrorMax: 0,
  lastCorrectionError: 0,
  lastPeriodicAt: 0
}

const timers = new Set()
const entities = new Map()

const ai = {
  mode: 'WALK',
  actionUntil: 0,
  targetYaw: null,
  turnSpeed: movement.turnDegreesPerTick,
  jumping: false,
  jumpUntil: 0,
  jumpCooldownUntil: 0,
  jumpStartSent: false,
  jumpReleasePending: false,
  verticalVelocity: 0,
  horizontalVelocityX: 0,
  horizontalVelocityZ: 0,
  swimming: false,
  swimStartSent: false,
  swimStopPending: false,
  waterEvidenceUntil: 0,
  emergencySurfacing: false,
  lastWaterDiagnostic: 0,
  strafeSign: 0,
  strafeUntil: 0,
  entityLockUntil: 0,
  obstacleLockUntil: 0,
  lastObstacleCheck: 0,
  lastCheckTime: 0,
  lastCheckPos: null,
  stuckCount: 0,
  lastPlannerYaw: null,
  recoveryAction: 'none',
  threatLockUntil: 0,
  threatName: null,
  threatDistance: null,
  submergedSince: 0,
  swimHeadingUntil: 0,
  swimHeading: null,
  authoritativeRebaseUntil: 0,
  lastAuthoritativeRebaseAt: 0,
  serverBlockedYaw: null,
  serverBlockedUntil: 0,
  serverBlockedReason: null

}

function logAi (...args) {
  if (debug.logAi) console.log(...args)
}

function setManagedInterval (fn, ms) {
  const id = setInterval(fn, ms)
  timers.add(id)
  return id
}

function setManagedTimeout (fn, ms) {
  const id = setTimeout(() => {
    timers.delete(id)
    fn()
  }, ms)
  timers.add(id)
  return id
}

function clearRuntimeTimers () {
  for (const id of timers) {
    clearInterval(id)
    clearTimeout(id)
  }
  timers.clear()
}

function sendClientReadyHandshake (source = 'unknown') {
  if (!client || readyHandshakeSent || runtimeId === null) return

  try {
    // Urutan ini mengikuti handshake client Bedrock modern yang diamati pada
    // implementasi bot 1.26.x: end loading screen -> mouse-over sentinel ->
    // player initialized. bedrock-protocol autoInitPlayer hanya mengirim
    // packet terakhir, jadi tiga packet ini tetap kita kirim eksplisit.
    client.queue('serverbound_loading_screen', {
      type: 1,
      loading_screen_id: undefined
    })
    client.queue('serverbound_loading_screen', {
      type: 2,
      loading_screen_id: undefined
    })
    client.queue('interact', {
      action_id: 'mouse_over_entity',
      target_entity_id: 0n,
      has_position: false,
      position: undefined
    })
    client.queue('set_local_player_as_initialized', {
      runtime_entity_id: runtimeId
    })

    readyHandshakeSent = true
    console.log(`🤝 Client-ready handshake dikirim (${source})`)
  } catch (err) {
    console.error('❌ Gagal mengirim client-ready handshake:', err?.stack || err)
  }
}

function sendPostRespawnReinitialize (source = 'post-respawn') {
  if (!client || runtimeId === null) return false

  try {
    client.queue('set_local_player_as_initialized', {
      runtime_entity_id: runtimeId
    })
    postRespawnRearm.reinitCount++
    postRespawnRearm.reinitLastAt = Date.now()
    postRespawnRearm.reinitLastSource = source
    console.log('🤝 Post-respawn set_local_player_as_initialized dikirim', {
      source,
      runtimeId: String(runtimeId),
      count: postRespawnRearm.reinitCount,
      tick: String(tick)
    })
    return true
  } catch (err) {
    console.error('❌ Gagal mengirim post-respawn set_local_player_as_initialized:', err?.stack || err)
    return false
  }
}

function randomBetween (min, max) {
  return min + Math.random() * (max - min)
}

function normalizeYaw (value) {
  while (value > 180) value -= 360
  while (value < -180) value += 360
  return value
}

function distance3D (a, b) {
  if (!a || !b) return 0
  const dx = a.x - b.x
  const dy = a.y - b.y
  const dz = a.z - b.z
  return Math.sqrt(dx * dx + dy * dy + dz * dz)
}

function horizontalDistance (a, b) {
  if (!a || !b) return 0
  return Math.hypot(a.x - b.x, a.z - b.z)
}

function authoritativeIsFresh (now = Date.now()) {
  return Boolean(authoritativePos && (now - lastAuthoritativeAt <= recovery.authoritativeFreshMs))
}

function authoritativeRebaseActive (now = Date.now()) {
  return authoritativeIsFresh(now) && now < ai.authoritativeRebaseUntil
}

function perceptionOrigin (forceAuthoritative = false) {
  const now = Date.now()
  if (authoritativeIsFresh(now) && (forceAuthoritative || authoritativeRebaseActive(now) || ai.stuckCount >= 2)) {
    return authoritativePos
  }
  return pos
}

function rememberServerBlockedHeading (reason = 'server-reject') {
  const now = Date.now()
  ai.serverBlockedYaw = yaw
  ai.serverBlockedUntil = now + recovery.serverBlockedHeadingHoldMs
  ai.serverBlockedReason = reason
}

function enterAuthoritativeRebase (reason = 'server-reject') {
  const now = Date.now()
  if (!authoritativeIsFresh(now)) return false

  ai.authoritativeRebaseUntil = Math.max(ai.authoritativeRebaseUntil, now + recovery.authoritativeRebaseHoldMs)
  pos = { ...authoritativePos }
  ai.horizontalVelocityX = 0
  ai.horizontalVelocityZ = 0
  if (!ai.jumping && !ai.swimming) ai.verticalVelocity = 0
  ai.lastCheckPos = { ...authoritativePos }

  if (now - ai.lastAuthoritativeRebaseAt > 900) {
    ai.lastAuthoritativeRebaseAt = now
    logAi(`🧲 Authoritative rebase (${reason}) → ${authoritativePos.x.toFixed(3)}, ${authoritativePos.y.toFixed(3)}, ${authoritativePos.z.toFixed(3)}`)
  }
  return true
}

function capPredictionDrift () {
  if (!authoritativeRebaseActive() || !pos || !authoritativePos) return false
  const drift = horizontalDistance(pos, authoritativePos)
  if (drift <= recovery.maxPredictionDrift) return false

  pos = { ...authoritativePos }
  ai.horizontalVelocityX = 0
  ai.horizontalVelocityZ = 0
  if (!ai.jumping && !ai.swimming) ai.verticalVelocity = 0
  return true
}

function roundNumber (value, digits = 3) {
  return Number.isFinite(value) ? Number(value.toFixed(digits)) : value
}

function roundVec (value, digits = 3) {
  if (!value) return null
  return {
    x: roundNumber(value.x, digits),
    y: roundNumber(value.y, digits),
    z: roundNumber(value.z, digits)
  }
}

function forwardVector (angle = yaw) {
  const rad = angle * Math.PI / 180
  return { x: -Math.sin(rad), z: Math.cos(rad) }
}

function yawFromVector (x, z) {
  if (!Number.isFinite(x) || !Number.isFinite(z) || Math.hypot(x, z) < 1e-6) return yaw
  return normalizeYaw(Math.atan2(-x, z) * 180 / Math.PI)
}

function normalizeEntityType (type) {
  return String(type || '').replace(/^minecraft:/, '')
}

function isHostileEntityType (type) {
  return HOSTILE_ENTITY_TYPES.has(normalizeEntityType(type))
}

function setTargetYaw (target) {
  ai.targetYaw = normalizeYaw(target)
}

function smoothTurn () {
  if (typeof ai.targetYaw !== 'number') return
  const difference = normalizeYaw(ai.targetYaw - yaw)

  if (Math.abs(difference) <= ai.turnSpeed) {
    yaw = ai.targetYaw
    ai.targetYaw = null
    return
  }

  yaw = normalizeYaw(yaw + Math.sign(difference) * ai.turnSpeed)
}

function resetAiState () {
  ai.mode = 'WALK'
  ai.actionUntil = 0
  ai.targetYaw = null
  ai.jumping = false
  ai.jumpUntil = 0
  ai.jumpCooldownUntil = 0
  ai.jumpStartSent = false
  ai.jumpReleasePending = false
  ai.verticalVelocity = 0
  ai.horizontalVelocityX = 0
  ai.horizontalVelocityZ = 0
  ai.swimming = false
  ai.swimStartSent = false
  ai.swimStopPending = false
  ai.waterEvidenceUntil = 0
  ai.emergencySurfacing = false
  ai.lastWaterDiagnostic = 0
  ai.strafeSign = 0
  ai.strafeUntil = 0
  ai.entityLockUntil = 0
  ai.obstacleLockUntil = 0
  ai.lastObstacleCheck = 0
  ai.lastCheckTime = Date.now()
  ai.lastCheckPos = authoritativePos ? { ...authoritativePos } : (pos ? { ...pos } : null)
  ai.stuckCount = 0
  ai.lastPlannerYaw = null
  ai.recoveryAction = 'none'
  ai.threatLockUntil = 0
  ai.threatName = null
  ai.threatDistance = null
  ai.submergedSince = 0
  ai.swimHeadingUntil = 0
  ai.swimHeading = null
  ai.authoritativeRebaseUntil = 0
  ai.lastAuthoritativeRebaseAt = 0
  ai.serverBlockedYaw = null
  ai.serverBlockedUntil = 0
  ai.serverBlockedReason = null

}

function startJump () {
  const now = Date.now()
  if (now < ai.jumpCooldownUntil || ai.jumping || ai.swimming) return false

  ai.jumping = true
  ai.jumpUntil = now + 450
  ai.jumpCooldownUntil = now + 850
  ai.jumpStartSent = false
  ai.jumpReleasePending = false
  ai.verticalVelocity = Math.max(ai.verticalVelocity, movement.jumpVelocity)
  logAi('🦘 JUMP dimulai')
  return true
}

function updateJumpState () {
  if (ai.jumping && Date.now() >= ai.jumpUntil) {
    ai.jumping = false
    ai.jumpStartSent = false
    ai.jumpReleasePending = true
  }
}

function ignoredEntityType (type) {
  if (!type) return true
  return [
    'minecraft:item',
    'minecraft:xp_orb',
    'minecraft:arrow',
    'minecraft:spectral_arrow',
    'minecraft:snowball',
    'minecraft:egg',
    'minecraft:ender_pearl',
    'minecraft:experience_bottle',
    'minecraft:thrown_trident',
    'minecraft:fishing_hook',
    'minecraft:fireball',
    'minecraft:small_fireball',
    'minecraft:area_effect_cloud'
  ].includes(type)
}

function trackEntity (data) {
  if (!data || data.runtimeId === undefined || !data.position) return
  if (runtimeId !== null && String(data.runtimeId) === String(runtimeId)) return
  if (ignoredEntityType(data.entityType)) return

  entities.set(String(data.runtimeId), {
    runtimeId: data.runtimeId,
    uniqueId: data.uniqueId,
    username: data.username || null,
    entityType: data.entityType || 'entity',
    position: { ...data.position },
    lastUpdate: Date.now()
  })
}

function updateEntityPosition (id, position) {
  if (id === undefined || !position) return
  const entity = entities.get(String(id))
  if (!entity) return
  entity.position = { ...position }
  entity.lastUpdate = Date.now()
}

function getNearbyHostiles (radius = survival.hostileAvoidanceRadius) {
  if (!pos || !survival.enabled) return []
  const now = Date.now()
  const hostiles = []

  for (const entity of entities.values()) {
    if (!entity.position || now - entity.lastUpdate > 5000) continue
    if (!isHostileEntityType(entity.entityType)) continue

    const dx = entity.position.x - pos.x
    const dy = (entity.position.y ?? pos.y) - pos.y
    const dz = entity.position.z - pos.z
    const distance = Math.hypot(dx, dz)
    if (distance > radius) continue

    hostiles.push({ entity, dx, dy, dz, distance })
  }

  return hostiles.sort((a, b) => a.distance - b.distance)
}

function chooseFleeHeading (hostiles) {
  if (!hostiles.length) return yaw

  // Gabungkan repulsion dari semua mob: mob terdekat memiliki bobot terbesar.
  let awayX = 0
  let awayZ = 0
  for (const hostile of hostiles.slice(0, 5)) {
    const weight = 1 / Math.max(0.75, hostile.distance * hostile.distance)
    awayX -= hostile.dx * weight
    awayZ -= hostile.dz * weight
  }

  const desired = yawFromVector(awayX, awayZ)
  const offsets = [0, -20, 20, -40, 40, -70, 70, -110, 110, 180]
  let best = null

  for (const offset of offsets) {
    const angle = normalizeYaw(desired + offset)
    const clearance = pathfinding.enabled ? clearanceAtAngle(angle) : pathfinding.lookAhead
    const alignment = Math.cos(offset * Math.PI / 180)
    const score = clearance * 0.55 + alignment * 2.2 - Math.abs(offset) * 0.004
    if (!best || score > best.score) best = { angle, clearance, score, offset }
  }

  return best?.angle ?? desired
}

function avoidHostiles () {
  if (!survival.enabled || !spawned || !pos) return false
  const hostiles = getNearbyHostiles()
  const now = Date.now()

  if (!hostiles.length) {
    if (now >= ai.threatLockUntil && ai.mode === 'FLEE') {
      ai.mode = 'WALK'
      ai.threatName = null
      ai.threatDistance = null
      ai.actionUntil = now + randomBetween(behavior.walkMinMs, behavior.walkMaxMs)
      logAi('🛡️ Ancaman menjauh → kembali wandering')
    }
    return false
  }

  const nearest = hostiles[0]
  const threatName = nearest.entity.username || nearest.entity.entityType
  const shouldRefresh = now >= ai.threatLockUntil || ai.threatName !== threatName || nearest.distance <= survival.panicRadius

  ai.mode = 'FLEE'
  ai.actionUntil = Math.max(ai.actionUntil, now + survival.threatLockMs)
  ai.threatLockUntil = now + survival.threatLockMs
  ai.threatName = threatName
  ai.threatDistance = nearest.distance
  lastThreatSnapshot = {
    at: now,
    name: threatName,
    entityType: nearest.entity.entityType,
    runtimeId: nearest.entity.runtimeId,
    distance: nearest.distance,
    position: { ...nearest.entity.position }
  }

  if (shouldRefresh) {
    const fleeYaw = chooseFleeHeading(hostiles)
    setTargetYaw(fleeYaw)
    ai.strafeSign = Math.random() < 0.5 ? -1 : 1
    ai.strafeUntil = now + (nearest.distance <= survival.panicRadius ? 800 : 450)
    logAi(`🛡️ FLEE ${threatName} (${nearest.distance.toFixed(1)} blok) → yaw ${fleeYaw.toFixed(1)}°`)
  }

  return true
}

function getEntityAhead () {
  if (!pos) return null
  const forward = forwardVector(yaw)
  let closest = null
  let closestDistance = Infinity

  for (const entity of entities.values()) {
    if (!entity.position || Date.now() - entity.lastUpdate > 5000) continue
    const dx = entity.position.x - pos.x
    const dz = entity.position.z - pos.z
    const distance = Math.hypot(dx, dz)
    if (distance > perception.forwardEntityDistance) continue

    const forwardDistance = dx * forward.x + dz * forward.z
    if (forwardDistance <= 0.15) continue

    const lateralDistance = Math.abs(dx * forward.z - dz * forward.x)
    if (lateralDistance > 0.8) continue

    if (distance < closestDistance) {
      closest = entity
      closestDistance = distance
    }
  }

  return closest
}

function avoidEntity () {
  if (ai.mode === 'FLEE') return
  const now = Date.now()
  if (now < ai.entityLockUntil) return
  const entity = getEntityAhead()
  if (!entity) return

  ai.strafeSign = Math.random() < 0.5 ? -1 : 1
  ai.strafeUntil = now + randomBetween(500, 850)
  setTargetYaw(yaw + ai.strafeSign * randomBetween(15, 28))
  ai.entityLockUntil = now + randomBetween(900, 1400)
  logAi(`🚶 Hindari ${entity.username || entity.entityType} → ${ai.strafeSign < 0 ? 'kiri' : 'kanan'}`)
}

function feetYOf (p = pos) {
  return p ? p.y - EYE_HEIGHT : 0
}

function looksGroundedFromEyePosition (p) {
  if (!p || !Number.isFinite(p.y)) return false
  const feetY = p.y - EYE_HEIGHT
  return Math.abs(feetY - Math.round(feetY)) <= 1e-4
}

function snapGroundedEyePosition (p, grounded) {
  if (!p || !grounded) return p ? { ...p } : p
  const feetY = p.y - EYE_HEIGHT
  const snappedFeetY = Math.round(feetY)
  if (Math.abs(feetY - snappedFeetY) > 1e-4) return { ...p }
  return { x: p.x, y: snappedFeetY + EYE_HEIGHT, z: p.z }
}

function probeWorldAt (x, y, z) {
  if (!world) return { name: null, info: null, solid: null, water: null }
  const name = world.blockAt(x, y, z)
  if (name === null) return { name: null, info: null, solid: null, water: null }
  return {
    name,
    info: world.blockInfoAt(x, y, z),
    solid: world.isSolid(x, y, z),
    water: world.isWater(x, y, z)
  }
}

function probeAtRelative (distance, yOffset, angle = yaw, lateralOffset = 0, forceAuthoritative = false) {
  const origin = perceptionOrigin(forceAuthoritative)
  if (!origin || !world) return { name: null, info: null, solid: null, water: null }
  const dir = forwardVector(angle)
  const right = { x: Math.cos(angle * Math.PI / 180), z: Math.sin(angle * Math.PI / 180) }
  const feetY = feetYOf(origin)
  const x = origin.x + dir.x * distance + right.x * lateralOffset
  const y = feetY + yOffset
  const z = origin.z + dir.z * distance + right.z * lateralOffset
  return { ...probeWorldAt(x, y, z), x, y, z }
}

function blockAtRelative (distance, yOffset, angle = yaw) {
  return probeAtRelative(distance, yOffset, angle).name
}

function isSolidName (name) {
  // Hanya fallback untuk tempat yang benar-benar cuma punya nama block.
  // Obstacle/path planner harus memakai world.isSolid(x,y,z) agar tanaman,
  // kelp, seagrass, dan non-full collision blocks tidak dianggap tembok.
  if (name === null || name === undefined) return null
  return !world.isAirName(name) && !world.isLiquidName(name)
}

function scanObstacle (force = false) {
  const origin = perceptionOrigin(force)
  if (!origin || !world) return null
  const now = Date.now()
  if (!force && now - ai.lastObstacleCheck < perception.obstacleCheckIntervalMs) return null
  ai.lastObstacleCheck = now

  const d = perception.obstacleDistance
  const lateral = PLAYER_HALF_WIDTH - COLLISION_INSET
  const offsets = [-lateral, 0, lateral]
  const feetProbes = offsets.map(offset => probeAtRelative(d, 0.15, yaw, offset, force))
  const bodyProbes = offsets.map(offset => probeAtRelative(d, 0.95, yaw, offset, force))
  const headProbes = offsets.map(offset => probeAtRelative(d, 1.55, yaw, offset, force))

  const known = [...feetProbes, ...bodyProbes].some(probe => probe.name !== null)
  if (!known) return { known: false, feet: feetProbes[1], body: bodyProbes[1], head: headProbes[1] }

  const feetSolid = feetProbes.some(probe => probe.solid === true)
  const bodySolid = bodyProbes.some(probe => probe.solid === true)
  const headSolid = headProbes.some(probe => probe.solid === true)

  return {
    known: true,
    lowObstacle: feetSolid && !bodySolid,
    highObstacle: feetSolid && bodySolid,
    jumpClear: !bodySolid && !headSolid,
    feet: feetProbes[1],
    body: bodyProbes[1],
    head: headProbes[1],
    widthProbes: {
      feet: feetProbes,
      body: bodyProbes,
      head: headProbes
    }
  }
}

function floorDecodeReliable () {
  const origin = perceptionOrigin()
  if (!origin || !world) return false
  const feetY = feetYOf(origin)
  const probes = [0.08, 0.35, 0.75]
  return probes.some(offset => {
    const y = feetY - offset
    const name = world.blockAt(origin.x, y, origin.z)
    return name !== null && world.isSolid(origin.x, y, origin.z)
  })
}

function clearanceAtAngle (angle) {
  const origin = perceptionOrigin()
  if (!origin || !world) return 0
  const dir = forwardVector(angle)
  const step = Math.max(0.25, pathfinding.sampleStep)
  const canTrustFloor = floorDecodeReliable()

  if (pathfinding.cliffCheck && onGround && !canTrustFloor && !floorDecodeWarningShown) {
    floorDecodeWarningShown = true
    console.log('⚠️ Decoder chunk belum bisa membuktikan lantai di posisi bot; cliff-check planner dinonaktifkan sementara agar semua arah tidak dianggap jurang')
  }

  for (let d = step; d <= pathfinding.lookAhead; d += step) {
    const candidate = {
      x: origin.x + dir.x * d,
      y: origin.y,
      z: origin.z + dir.z * d
    }
    const feetY = feetYOf(candidate)

    // Collision memakai footprint player, bukan titik tengah saja. Ini penting di
    // sudut tembok / edge block, tempat center-ray terlihat kosong tetapi AABB player menyentuh block.
    if (!canOccupy(candidate)) return Math.max(0, d - step)

    // Air boleh dilalui, tapi sedikit dipenalti bila jalur darat setara.
    if (!ai.swimming && pathfinding.waterPenalty > 0 && (
      world.isWater(candidate.x, feetY + 0.15, candidate.z) ||
      world.isWater(candidate.x, feetY + 1.05, candidate.z)
    )) {
      return Math.max(0, d - pathfinding.waterPenalty)
    }

    if (pathfinding.cliffCheck && canTrustFloor && d >= 1.0) {
      const belowName = world.blockAt(candidate.x, feetY - 0.25, candidate.z)
      const fartherBelowName = world.blockAt(candidate.x, feetY - 1.25, candidate.z)
      if (belowName !== null && fartherBelowName !== null &&
          !world.isSolid(candidate.x, feetY - 0.25, candidate.z) &&
          !world.isSolid(candidate.x, feetY - 1.25, candidate.z)) {
        return Math.max(0, d - step)
      }
    }
  }

  return pathfinding.lookAhead
}

function evaluateLocalHeadings () {
  if (!pathfinding.enabled) return []

  const candidates = []
  for (const offset of pathfinding.candidateAngles) {
    const angle = normalizeYaw(yaw + offset)
    const clearance = clearanceAtAngle(angle)
    const turnPenalty = Math.abs(offset) * 0.018
    const reversePenalty = Math.abs(offset) >= 80 ? 0.5 : 0
    const continuityBonus = ai.lastPlannerYaw !== null && Math.abs(normalizeYaw(angle - ai.lastPlannerYaw)) < 10 ? 0.12 : 0
    let serverBlockedPenalty = 0
    if (ai.serverBlockedYaw !== null && Date.now() < ai.serverBlockedUntil) {
      const delta = Math.abs(normalizeYaw(angle - ai.serverBlockedYaw))
      if (delta < recovery.serverBlockedHeadingRadiusDeg) {
        const strength = 1 - (delta / recovery.serverBlockedHeadingRadiusDeg)
        serverBlockedPenalty = recovery.serverBlockedHeadingPenalty * strength
      }
    }
    const jitter = Math.random() * 0.04
    const score = clearance - turnPenalty - reversePenalty - serverBlockedPenalty + continuityBonus + jitter
    candidates.push({ angle, offset, clearance, score, serverBlockedPenalty })
  }
  return candidates.sort((a, b) => b.score - a.score)
}

function chooseLocalHeading () {
  if (!pathfinding.enabled) return normalizeYaw(yaw + (Math.random() < 0.5 ? -45 : 45))

  const [best] = evaluateLocalHeadings()
  if (best) {
    ai.lastPlannerYaw = best.angle
    logAi(`🧭 Planner: ${best.offset >= 0 ? '+' : ''}${best.offset}° (clearance ${best.clearance.toFixed(1)})`)
    return best.angle
  }

  return normalizeYaw(yaw + 45)
}

function avoidBlock () {
  if (ai.swimming) return
  const now = Date.now()
  if (now < ai.obstacleLockUntil) return

  const obstacle = scanObstacle()
  if (!obstacle || !obstacle.known) return

  if (obstacle.lowObstacle && obstacle.jumpClear) {
    if (startJump()) logAi('🦘 AUTO-JUMP: rintangan 1 blok')
    ai.obstacleLockUntil = now + 500
    return
  }

  if (!obstacle.highObstacle) return

  const heading = chooseLocalHeading()
  setTargetYaw(heading)
  ai.obstacleLockUntil = now + 900
}

function chooseNextAction () {
  const now = Date.now()

  if (ai.mode === 'FLEE' && now < ai.threatLockUntil) {
    ai.actionUntil = ai.threatLockUntil
    return
  }

  if (ai.swimming) {
    // Di air jangan idle/random-turn terlalu sering. Pertahankan satu heading agar
    // bot benar-benar punya peluang mencapai tepian daripada bobbing di tempat.
    ai.mode = 'WALK'
    if (ai.swimHeading === null || now >= ai.swimHeadingUntil) {
      ai.swimHeading = normalizeYaw(yaw + randomBetween(-18, 18))
      ai.swimHeadingUntil = now + swimming.escapeHeadingHoldMs
    }
    setTargetYaw(ai.swimHeading)
    ai.actionUntil = now + swimming.escapeHeadingHoldMs
    return
  }

  if (Math.random() < behavior.idleChance) {
    ai.mode = 'IDLE'
    ai.targetYaw = null
    ai.actionUntil = now + randomBetween(behavior.idleMinMs, behavior.idleMaxMs)
    logAi('🧠 AI: idle sebentar')
    return
  }

  ai.mode = 'WALK'
  const gentle = randomBetween(behavior.gentleTurnMinDeg, behavior.gentleTurnMaxDeg)
  setTargetYaw(yaw + gentle)
  ai.actionUntil = now + randomBetween(behavior.walkMinMs, behavior.walkMaxMs)
  logAi(`🧠 AI: jalan, koreksi arah ${gentle.toFixed(1)}°`)
}

function checkStuck () {
  if (!spawned || !pos || ai.swimming || postRespawnSync.active || postRespawnRearm.active) return
  const now = Date.now()
  if (now - ai.lastCheckTime < recovery.stuckCheckMs) return
  ai.lastCheckTime = now

  const authoritativeFresh = authoritativePos && (now - lastAuthoritativeAt <= recovery.authoritativeFreshMs)
  const samplePos = authoritativeFresh ? authoritativePos : pos

  if (!ai.lastCheckPos) {
    ai.lastCheckPos = { ...samplePos }
    return
  }

  const moved = horizontalDistance(samplePos, ai.lastCheckPos)
  ai.lastCheckPos = { ...samplePos }
  const serverHorizontalSpeed = Math.hypot(lastServerDelta?.x || 0, lastServerDelta?.z || 0)

  // Jangan menilai stuck saat bot memang tidak berniat maju, sedang jump/recovery,
  // atau server masih menunjukkan velocity horizontal yang sehat.
  const activelyTryingToWalk = (ai.mode === 'WALK' || ai.mode === 'FLEE') && !ai.jumping
  if (!activelyTryingToWalk || moved >= recovery.minProgress || serverHorizontalSpeed >= recovery.minServerHorizontalSpeed) {
    ai.stuckCount = 0
    ai.recoveryAction = 'none'
    return
  }

  ai.stuckCount++
  logAi(`⚠️ Progress lambat ${ai.stuckCount}/${recovery.samplesBeforeHardRecovery} (${moved.toFixed(3)} blok; server vXZ=${serverHorizontalSpeed.toFixed(3)})`)

  // Sampel pertama hanya observasi. v1.4 langsung jump di sini dan menghasilkan
  // banyak false-positive karena correction server tidak selalu tiba tepat di jendela sampling.
  if (ai.stuckCount === 1) return

  // v1.7: jika server sendiri membuktikan posisi tidak maju, percayai posisi server.
  // World mirror 1.21->1.26 dapat salah melihat sebuah collision sebagai air, jadi
  // ingat heading yang ditolak server agar planner tidak memilih arah itu lagi.
  enterAuthoritativeRebase(`stuck-${ai.stuckCount}`)
  if (ai.stuckCount === 2 || ai.stuckCount >= recovery.samplesBeforeHardRecovery) {
    rememberServerBlockedHeading(`stuck-${ai.stuckCount}`)
  }

  const obstacle = scanObstacle(true)

  if (debug.diagnosticsOnStuck && ai.stuckCount === 2) {
    diagnosticSnapshot('stuck-candidate', { moved, serverHorizontalSpeed, obstacle })
  }

  if (obstacle?.known && obstacle.lowObstacle && obstacle.jumpClear) {
    ai.recoveryAction = 'jump-low-obstacle'
    if (startJump()) logAi('🛠️ Recovery: jump karena obstacle rendah terkonfirmasi')
    ai.stuckCount = 0
    return
  }

  if (ai.stuckCount === 2) {
    ai.recoveryAction = 'replan'
    setTargetYaw(chooseLocalHeading())
    ai.obstacleLockUntil = now + 900
    return
  }

  if (ai.stuckCount === 3) {
    ai.recoveryAction = 'strafe-replan'
    ai.strafeSign = Math.random() < 0.5 ? -1 : 1
    ai.strafeUntil = now + randomBetween(550, 850)
    setTargetYaw(chooseLocalHeading())
    return
  }

  if (ai.stuckCount >= recovery.samplesBeforeHardRecovery) {
    ai.recoveryAction = 'hard-replan'
    const candidates = evaluateLocalHeadings()
    const wide = candidates.find(candidate => Math.abs(candidate.offset) >= 30 && candidate.clearance >= 1.0)
    setTargetYaw(wide?.angle ?? normalizeYaw(yaw + (Math.random() < 0.5 ? -70 : 70)))
    ai.obstacleLockUntil = now + 1200
    ai.stuckCount = 0
    if (debug.diagnosticsOnStuck) diagnosticSnapshot('hard-recovery', { obstacle })
  }
}

function updateAI () {
  const now = Date.now()
  smoothTurn()
  const fleeing = avoidHostiles()
  if (!fleeing) avoidEntity()
  avoidBlock()
  checkStuck()
  if (now >= ai.actionUntil) chooseNextAction()
}

function getEnvironment () {
  const origin = perceptionOrigin()
  if (!origin || !world) {
    const serverSubmerged = Number.isFinite(serverAir) && Number.isFinite(serverMaxAir) && serverAir < serverMaxAir
    return { inWater: serverSubmerged, submerged: serverSubmerged, onGround, serverSubmerged }
  }

  const now = Date.now()
  const feetY = feetYOf(origin)
  const feet = world.blockAt(origin.x, feetY + 0.10, origin.z)
  const body = world.blockAt(origin.x, feetY + 0.85, origin.z)
  // Probe around eye/head level. `origin.y` itself is the movement/eye position.
  const head = world.blockAt(origin.x, origin.y - 0.05, origin.z)
  const feetInfo = world.blockInfoAt(origin.x, feetY + 0.10, origin.z)
  const bodyInfo = world.blockInfoAt(origin.x, feetY + 0.85, origin.z)
  const headInfo = world.blockInfoAt(origin.x, origin.y - 0.05, origin.z)

  const blockWater = world.isWater(origin.x, feetY + 0.10, origin.z) ||
    world.isWater(origin.x, feetY + 0.85, origin.z) ||
    world.isWater(origin.x, origin.y - 0.05, origin.z)
  const headWater = world.isWater(origin.x, origin.y - 0.05, origin.z)

  // The server's `air` metadata is authoritative evidence that the player's head
  // is underwater. This fallback is important because the 1.21 compatibility
  // chunk decoder may miss/flatten liquid storage on newer 1.26.x chunks.
  const serverSubmerged = Number.isFinite(serverAir) && Number.isFinite(serverMaxAir) && serverAir < serverMaxAir

  if (blockWater || serverSubmerged) ai.waterEvidenceUntil = now + 4000

  // Once swimming, keep the water state latched while we're unsupported. This
  // prevents a one-tick decoder miss at the surface from immediately disabling
  // buoyancy. It releases naturally after the bot reaches solid ground.
  const latchedSwimming = ai.swimming && (!onGround || now < ai.waterEvidenceUntil)
  const inWater = blockWater || serverSubmerged || now < ai.waterEvidenceUntil || latchedSwimming
  const submerged = headWater || serverSubmerged
  const support = world.isSolid(origin.x, feetY - 0.08, origin.z) || world.isSolid(origin.x, feetY - 0.35, origin.z)

  return {
    feet,
    body,
    head,
    feetInfo,
    bodyInfo,
    headInfo,
    blockWater,
    serverSubmerged,
    inWater,
    submerged,
    onGround: onGround || support
  }
}

function summarizeProbe (probe) {
  if (!probe) return null
  return {
    name: probe.name ?? null,
    solid: probe.solid ?? null,
    water: probe.water ?? null,
    superimposed: probe.info?.superimposed ?? null,
    waterlogged: probe.info?.waterlogged ?? null,
    boundingBox: probe.info?.boundingBox ?? null,
    shapes: probe.info?.shapes ?? null
  }
}

function nearbyEntitiesSummary (radius = debug.diagnosticsNearbyRadius) {
  if (!pos) return []
  const result = []
  for (const entity of entities.values()) {
    if (!entity.position) continue
    const dx = entity.position.x - pos.x
    const dz = entity.position.z - pos.z
    const distance = Math.hypot(dx, dz)
    if (distance > radius) continue
    result.push({
      name: entity.username || entity.entityType,
      distance: roundNumber(distance, 2),
      dx: roundNumber(dx, 2),
      dz: roundNumber(dz, 2)
    })
  }
  return result.sort((a, b) => a.distance - b.distance).slice(0, 8)
}

function frontProbeSummary () {
  const distances = [0.5, 1.0, 1.5, 2.0]
  return distances.map(distance => ({
    d: distance,
    feet: summarizeProbe(probeAtRelative(distance, 0.15)),
    body: summarizeProbe(probeAtRelative(distance, 0.95)),
    head: summarizeProbe(probeAtRelative(distance, 1.55))
  }))
}

function diagnosticSnapshot (reason = 'periodic', extra = {}) {
  if (!debug.diagnostics || !pos || !world) return
  const now = Date.now()
  const env = getEnvironment()
  const elapsedMs = Math.max(1, now - diagnostics.windowStartedAt)
  const elapsedSec = elapsedMs / 1000
  const plannerTop = evaluateLocalHeadings().slice(0, 4).map(candidate => ({
    offset: candidate.offset,
    clearance: roundNumber(candidate.clearance, 2),
    score: roundNumber(candidate.score, 2),
    serverBlockedPenalty: roundNumber(candidate.serverBlockedPenalty || 0, 2)
  }))

  const avgCorrectionError = diagnostics.corrections > 0
    ? diagnostics.correctionErrorSum / diagnostics.corrections
    : 0

  console.log(`🔬 DIAG [${reason}]`, {
    position: roundVec(pos),
    feetY: roundNumber(feetYOf(pos), 3),
    authoritativePosition: roundVec(authoritativePos),
    authoritativeAgeMs: lastAuthoritativeAt ? now - lastAuthoritativeAt : null,
    yaw: roundNumber(yaw, 1),
    pitch: roundNumber(pitch, 1),
    ai: {
      mode: ai.mode,
      targetYaw: ai.targetYaw === null ? null : roundNumber(ai.targetYaw, 1),
      jumping: ai.jumping,
      swimming: ai.swimming,
      emergencySurfacing: ai.emergencySurfacing,
      stuckCount: ai.stuckCount,
      recoveryAction: ai.recoveryAction,
      threat: ai.threatName,
      threatDistance: ai.threatDistance === null ? null : roundNumber(ai.threatDistance, 2),
      authoritativeRebase: authoritativeRebaseActive(now),
      authoritativeRebaseRemainingMs: Math.max(0, ai.authoritativeRebaseUntil - now),
      serverBlockedYaw: ai.serverBlockedYaw === null ? null : roundNumber(ai.serverBlockedYaw, 1),
      serverBlockedRemainingMs: Math.max(0, ai.serverBlockedUntil - now),
      serverBlockedReason: ai.serverBlockedReason
    },
    safety: {
      nonDestructive: safety.nonDestructive,
      blockedOutboundPackets: client?.__nonDestructiveGuardStats?.blocked || 0,
      lastBlocked: client?.__nonDestructiveGuardStats?.lastBlocked || null
    },
    survival: {
      health: currentHealth,
      lastDamageAmount,
      lastDamageAgeMs: lastDamageAt ? now - lastDamageAt : null,
      dead: lifecycle.dead,
      deathReason: lifecycle.deathReason,
      respawnAttempts: lifecycle.respawnAttempts,
      respawnReadyReceived: lifecycle.respawnReadyReceived,
      respawnActionSent: lifecycle.respawnActionSent,
      respawnClientReadySent: lifecycle.respawnClientReadySent,
      respawnSearchingReceived: lifecycle.respawnSearchingReceived,
      respawnLastState: lifecycle.respawnLastState,
      respawnActionWire: respawnActionCompat.expectedOrdinal,
      respawnActionToken: respawnActionCompat.token,
      respawnActionFace: PROTOCOL_2193_RESPAWN_FACE,
      respawnActionChannel: lifecycle.respawnActionChannel,
      respawnActionTick: lifecycle.respawnActionTick === null ? null : String(lifecycle.respawnActionTick),
      postRespawnSyncActive: postRespawnSync.active,
      postRespawnFeedbackCount: postRespawnSync.feedbackCount,
      postRespawnLargestShift: roundNumber(postRespawnSync.largestShift, 2),
      postRespawnRearmActive: postRespawnRearm.active,
      postRespawnRearmProbePackets: postRespawnRearm.probePackets,
      postRespawnRearmFeedbackCount: postRespawnRearm.feedbackCount,
      postRespawnRearmLargestServerStep: roundNumber(postRespawnRearm.largestServerStep, 3),
      handledTeleportPending,
      handledTeleportAckAgeMs: handledTeleportAckSentAt ? Date.now() - handledTeleportAckSentAt : null,
      clientInputMoveLocked: clientInputLocks.move,
      clientInputLocksRaw: clientInputLocks.raw,
      postRespawnReinitCount: postRespawnRearm.reinitCount,
      postRespawnReinitAgeMs: postRespawnRearm.reinitLastAt ? Date.now() - postRespawnRearm.reinitLastAt : null,
      postRespawnReinitSource: postRespawnRearm.reinitLastSource,
      movementTick: String(tick),
      movementTickSeeded,
      lastThreatSnapshot
    },
    motion: {
      onGround,
      velocity: roundVec({ x: ai.horizontalVelocityX, y: ai.verticalVelocity, z: ai.horizontalVelocityZ }, 4),
      serverDelta: roundVec(lastServerDelta, 4)
    },
    water: {
      inWater: env.inWater,
      submerged: env.submerged,
      blockWater: env.blockWater,
      serverSubmerged: env.serverSubmerged,
      air: serverAir,
      maxAir: serverMaxAir
    },
    blocks: {
      feet: env.feetInfo || env.feet || null,
      body: env.bodyInfo || env.body || null,
      head: env.headInfo || env.head || null,
      front: frontProbeSummary()
    },
    nearbyEntities: nearbyEntitiesSummary(),
    plannerTop,
    network: {
      movementPacketsTotal: movementPacketsSent,
      correctionsTotal: movementCorrections,
      movementPacketsPerSec: roundNumber(diagnostics.packets / elapsedSec, 1),
      correctionsPerSec: roundNumber(diagnostics.corrections / elapsedSec, 2),
      correctionErrorAvg: roundNumber(avgCorrectionError, 3),
      correctionErrorMax: roundNumber(diagnostics.correctionErrorMax, 3),
      correctionErrorLast: roundNumber(diagnostics.lastCorrectionError, 3),
      lastCorrectionAgeMs: lastCorrectionAt ? now - lastCorrectionAt : null,
      authoritativeDistanceTotal: roundNumber(totalAuthoritativeDistance, 1),
      correctionRejectStreak
    },
    world: { ...world.stats(), floorDecodeReliable: floorDecodeReliable() },
    ...extra
  })

  if (reason === 'periodic') {
    diagnostics.windowStartedAt = now
    diagnostics.packets = 0
    diagnostics.corrections = 0
    diagnostics.correctionErrorSum = 0
    diagnostics.correctionErrorMax = 0
    diagnostics.lastPeriodicAt = now
  }
}

function updateSwimState (env) {
  const now = Date.now()

  if (env.inWater && !ai.swimming) {
    ai.swimming = true
    ai.swimStartSent = false
    ai.swimStopPending = false
    ai.jumping = false
    ai.verticalVelocity = Math.max(ai.verticalVelocity, swimming.normalRisePerTick)
    ai.mode = 'WALK'
    ai.swimHeading = yaw
    ai.swimHeadingUntil = now + swimming.escapeHeadingHoldMs
    ai.actionUntil = ai.swimHeadingUntil
    const source = env.serverSubmerged ? 'metadata napas server' : 'blok air'
    logAi(`🏊 Masuk air → mode berenang (${source})`)
    logAi('🌊 Probe air:', {
      feet: env.feetInfo || env.feet,
      body: env.bodyInfo || env.body,
      head: env.headInfo || env.head,
      air: serverAir,
      maxAir: serverMaxAir
    })
    if (debug.diagnostics) diagnosticSnapshot('swim-enter')
  } else if (!env.inWater && ai.swimming) {
    ai.swimming = false
    ai.swimStopPending = true
    ai.swimStartSent = false
    ai.emergencySurfacing = false
    ai.submergedSince = 0
    ai.swimHeading = null
    ai.swimHeadingUntil = 0
    ai.verticalVelocity = 0
    logAi('🏊 Keluar dari air')
    if (debug.diagnostics) diagnosticSnapshot('swim-exit')
  }

  if (env.serverSubmerged) {
    if (!ai.submergedSince) ai.submergedSince = now
    const submergedMs = now - ai.submergedSince
    const lowAir = Number.isFinite(serverAir) && serverAir <= swimming.emergencyAirThreshold
    const sustained = submergedMs >= swimming.emergencyAfterMs
    const shouldEmergency = lowAir || sustained

    if (shouldEmergency && !ai.emergencySurfacing) {
      logAi(`🛟 Emergency surface (${lowAir ? `air ${serverAir}/${serverMaxAir}` : `${submergedMs}ms submerged`})`)
    }
    ai.emergencySurfacing = shouldEmergency
    const rise = shouldEmergency ? swimming.emergencyRisePerTick : swimming.normalRisePerTick
    ai.verticalVelocity = Math.max(ai.verticalVelocity, rise)
  } else {
    const recovered = ai.emergencySurfacing || ai.submergedSince > 0
    ai.submergedSince = 0
    ai.emergencySurfacing = false
    if (recovered && Number.isFinite(serverAir) && serverAir >= serverMaxAir) {
      logAi('🫧 Napas pulih → sudah mencapai udara')
    }
  }
}

function canOccupy (p) {
  if (!world) return true
  const feetY = feetYOf(p)
  const r = PLAYER_HALF_WIDTH - COLLISION_INSET
  const samples = [
    [0, 0],
    [-r, -r], [-r, r], [r, -r], [r, r]
  ]

  for (const [dx, dz] of samples) {
    const x = p.x + dx
    const z = p.z + dz
    const feet = world.blockAt(x, feetY + 0.15, z)
    const body = world.blockAt(x, feetY + 1.05, z)
    if (feet !== null && world.isSolid(x, feetY + 0.15, z)) return false
    if (body !== null && world.isSolid(x, feetY + 1.05, z)) return false
  }

  // Unknown terrain remains permissive; server-collision memory is the fallback
  // when the compatibility decoder incorrectly reports/omits collision.
  return true
}

function calculatePredictedPosition (moveX, moveZ, env) {
  const oldPos = { ...pos }
  const next = { ...pos }
  const yawRad = yaw * Math.PI / 180
  const forwardX = -Math.sin(yawRad)
  const forwardZ = Math.cos(yawRad)
  const rightX = Math.cos(yawRad)
  const rightZ = Math.sin(yawRad)

  // Bedrock damps the local move vector by ~0.98 before moveRelative.
  const localMoveX = moveX * 0.98
  const localMoveZ = moveZ * 0.98

  let accel
  if (ai.swimming || env.inWater) accel = 0.02
  else if (env.onGround) accel = movement.walkSpeed
  else accel = 0.02

  ai.horizontalVelocityX += (forwardX * localMoveZ + rightX * localMoveX) * accel
  ai.horizontalVelocityZ += (forwardZ * localMoveZ + rightZ * localMoveX) * accel

  // Position is advanced by the PRE-friction velocity. Packet delta below is the
  // POST-friction velocity, matching Bedrock's simulation order.
  let requestedX = ai.horizontalVelocityX
  let requestedZ = ai.horizontalVelocityZ
  let horizontalCollision = false

  const horizontalCandidate = { x: next.x + requestedX, y: next.y, z: next.z + requestedZ }
  if (canOccupy(horizontalCandidate)) {
    next.x = horizontalCandidate.x
    next.z = horizontalCandidate.z
  } else {
    requestedX = 0
    requestedZ = 0
    ai.horizontalVelocityX = 0
    ai.horizontalVelocityZ = 0
    horizontalCollision = true
  }

  // Vertical position uses the velocity that existed at the START of this tick.
  // Gravity/drag is applied afterwards and becomes packet delta / next-tick velocity.
  let requestedY = ai.verticalVelocity
  let verticalCollision = false

  if (!ai.swimming && env.onGround && !ai.jumping && requestedY <= 0) {
    // Ground supports the player: don't incorrectly move the eye position down.
    requestedY = 0
    verticalCollision = true
    ai.verticalVelocity = 0
  } else if (requestedY !== 0) {
    const verticalCandidate = { x: next.x, y: next.y + requestedY, z: next.z }
    const feetY = feetYOf(verticalCandidate)

    if (requestedY > 0) {
      const head = world.blockAt(verticalCandidate.x, verticalCandidate.y + 0.18, verticalCandidate.z)
      if (head !== null && world.isSolid(verticalCandidate.x, verticalCandidate.y + 0.18, verticalCandidate.z)) {
        requestedY = 0
        ai.verticalVelocity = 0
        verticalCollision = true
      } else {
        next.y = verticalCandidate.y
      }
    } else {
      const floor = world.blockAt(verticalCandidate.x, feetY - 0.02, verticalCandidate.z)
      if (floor !== null && world.isSolid(verticalCandidate.x, feetY - 0.02, verticalCandidate.z)) {
        requestedY = 0
        ai.verticalVelocity = 0
        verticalCollision = true
        onGround = true
      } else {
        next.y = verticalCandidate.y
      }
    }
  }

  // Post-move drag/gravity. This is what vanilla reports in PlayerAuthInput.delta.
  if (ai.swimming || env.inWater) {
    ai.horizontalVelocityX *= 0.8
    ai.horizontalVelocityZ *= 0.8
    if (ai.swimming) {
      if (env.submerged || ai.emergencySurfacing) {
        const rise = ai.emergencySurfacing ? swimming.emergencyRisePerTick : swimming.normalRisePerTick
        const cap = ai.emergencySurfacing ? 0.10 : 0.065
        ai.verticalVelocity = Math.min(cap, Math.max(ai.verticalVelocity * 0.72 + rise, rise))
      } else {
        // Di permukaan jangan terus dipompa ke atas. Bias kecil cukup untuk menjaga
        // kepala di atas air tanpa gerakan pogo naik-turun seperti v1.5.
        ai.verticalVelocity = Math.max(swimming.surfaceBiasPerTick, ai.verticalVelocity * 0.35)
      }
    } else {
      ai.verticalVelocity = ai.verticalVelocity * 0.8 - 0.005
    }
  } else {
    const xzDrag = env.onGround ? GROUND_FRICTION_XZ : AIR_FRICTION_XZ
    ai.horizontalVelocityX *= xzDrag
    ai.horizontalVelocityZ *= xzDrag
    ai.verticalVelocity = (ai.verticalVelocity - movement.gravity) * movement.verticalDrag
  }

  // Mirror the canonical ground rule enough for packet flags. A support collision
  // keeps us grounded even though post-tick delta.y contains gravity for next tick.
  if (verticalCollision && requestedY <= 0) onGround = true
  else if (requestedY > 0) onGround = false

  return {
    next,
    delta: {
      x: ai.horizontalVelocityX,
      y: ai.verticalVelocity,
      z: ai.horizontalVelocityZ
    },
    horizontalCollision,
    verticalCollision: verticalCollision || env.onGround
  }
}

function buildInputData ({ walking, jumping, swimming, submerged, horizontalCollision, verticalCollision }) {
  // Bedrock 1.26.51 memakai []InputData, bukan bitflags object lama.
  // Beberapa flag di bawah bukan "tombol"; vanilla mengirimnya sebagai bagian
  // dari state hasil simulasi/collision pada tick tersebut.
  const inputs = ['block_breaking_delay_enabled']

  if (walking) inputs.push('up')
  if (horizontalCollision) inputs.push('horizontal_collision')
  if (verticalCollision || onGround) inputs.push('vertical_collision')
  if (receivedServerData) inputs.push('received_server_data')
  if (handledTeleportPending) inputs.push('handled_teleport')

  if (jumping) {
    inputs.push('jump_down', 'jumping', 'want_up', 'jump_current_raw')
    if (!ai.jumpStartSent) {
      inputs.push('start_jumping', 'jump_pressed_raw')
      ai.jumpStartSent = true
    }
  } else if (ai.jumpReleasePending) {
    inputs.push('jump_released_raw')
    ai.jumpReleasePending = false
  }

  if (swimming) {
    inputs.push('auto_jumping_in_water', 'want_up', 'ascend')
    if (submerged) inputs.push('want_up_slow', 'jump_down', 'jumping', 'jump_current_raw')
    if (!ai.swimStartSent) {
      inputs.push('start_swimming')
      ai.swimStartSent = true
    }
  } else if (ai.swimStopPending) {
    inputs.push('stop_swimming')
    ai.swimStopPending = false
  }

  return [...new Set(inputs)]
}


function consumeMovementAckFlags (inputData, outgoingTick, context = 'movement') {
  if (receivedServerData && inputData.includes('received_server_data')) {
    receivedServerData = false
  }

  if (handledTeleportPending && inputData.includes('handled_teleport')) {
    const reason = handledTeleportReason
    handledTeleportPending = false
    handledTeleportReason = null
    handledTeleportAckSentAt = Date.now()
    console.log('🧷 handled_teleport ACK dikirim via PlayerAuthInput', {
      context,
      tick: String(outgoingTick),
      reason
    })
  }
}

function updateSelfAirMetadata (packet) {
  if (!packet || runtimeId === null || String(packet.runtime_entity_id) !== String(runtimeId)) return

  let airChanged = false
  let maxAirChanged = false

  for (const entry of packet.metadata || []) {
    if (!entry) continue
    if (entry.key === 'air' || entry.key === 7 || String(entry.key) === '7') {
      const value = Number(entry.value)
      if (Number.isFinite(value)) {
        serverAir = value
        airChanged = true
      }
    } else if (entry.key === 'max_airdata_max_air' || entry.key === 42 || String(entry.key) === '42') {
      const value = Number(entry.value)
      if (Number.isFinite(value) && value > 0) {
        serverMaxAir = value
        maxAirChanged = true
      }
    }
  }

  if (!airChanged && !maxAirChanged) return

  if (!Number.isFinite(serverMaxAir) || serverMaxAir <= 0) serverMaxAir = 300
  const underwater = Number.isFinite(serverAir) && serverAir < serverMaxAir

  if (underwater) {
    ai.waterEvidenceUntil = Date.now() + 4000
    const now = Date.now()
    if (now - ai.lastWaterDiagnostic >= 1500) {
      ai.lastWaterDiagnostic = now
      const env = getEnvironment()
      console.log(`🫧 Napas ${serverAir}/${serverMaxAir} → kepala terdeteksi di bawah air`, {
        feet: env.feetInfo || env.feet,
        body: env.bodyInfo || env.body,
        head: env.headInfo || env.head
      })
    }
  }
}


function decodeClientInputLocks (packet = {}) {
  const candidate = packet?.locks ?? packet?.input_lock_component_data ?? packet?.input_lock_component ?? packet?.data ?? null

  if (candidate && typeof candidate === 'object' && !Array.isArray(candidate)) {
    return {
      raw: candidate,
      move: Boolean(candidate.move),
      jump: Boolean(candidate.jump),
      sneak: Boolean(candidate.sneak),
      mount: Boolean(candidate.mount),
      dismount: Boolean(candidate.dismount),
      rotation: Boolean(candidate.rotation),
      updatedAt: Date.now()
    }
  }

  const n = Number(candidate)
  if (Number.isFinite(n)) {
    return {
      raw: n,
      move: Boolean(n & 0x02),
      jump: Boolean(n & 0x04),
      sneak: Boolean(n & 0x08),
      mount: Boolean(n & 0x10),
      dismount: Boolean(n & 0x20),
      rotation: Boolean(n & 0x40),
      updatedAt: Date.now()
    }
  }

  return {
    raw: candidate,
    move: false,
    jump: false,
    sneak: false,
    mount: false,
    dismount: false,
    rotation: false,
    updatedAt: Date.now()
  }
}

function calculatePostRespawnRearmProbe (moveX, moveZ) {
  // During re-arm the server's last correction is the authoritative physics
  // state. Do not run normal gravity here: in the failing case BDS reports a
  // frozen airborne pose (on_ground=false, delta=0) and repeatedly corrects a
  // client that starts falling locally. Probe only the horizontal input while
  // keeping Y and vertical delta anchored to the server state.
  const anchor = authoritativePos ? { ...authoritativePos } : { ...pos }
  const yawRad = yaw * Math.PI / 180
  const forwardX = -Math.sin(yawRad)
  const forwardZ = Math.cos(yawRad)
  const rightX = Math.cos(yawRad)
  const rightZ = Math.sin(yawRad)
  const localMoveX = moveX * 0.98
  const localMoveZ = moveZ * 0.98
  const grounded = Boolean(postRespawnRearm.authoritativeOnGround)
  const accel = grounded ? movement.walkSpeed : 0.02
  let preX = (forwardX * localMoveZ + rightX * localMoveX) * accel
  let preZ = (forwardZ * localMoveZ + rightZ * localMoveX) * accel
  let horizontalCollision = false
  const next = { x: anchor.x + preX, y: anchor.y, z: anchor.z + preZ }

  if (!canOccupy(next)) {
    next.x = anchor.x
    next.z = anchor.z
    preX = 0
    preZ = 0
    horizontalCollision = true
  }

  const xzDrag = grounded ? GROUND_FRICTION_XZ : AIR_FRICTION_XZ
  const serverY = Number(postRespawnRearm.authoritativeDelta?.y)
  const stableServerY = Number.isFinite(serverY) && Math.abs(serverY) <= 0.25 ? serverY : 0

  // Re-arm is intentionally stateless between ticks. Each probe is predicted
  // from the latest server pose, preventing local X/Z drift while acceptance is
  // still unknown.
  ai.horizontalVelocityX = 0
  ai.horizontalVelocityZ = 0
  ai.verticalVelocity = 0
  onGround = grounded

  return {
    next,
    delta: { x: preX * xzDrag, y: stableServerY, z: preZ * xzDrag },
    horizontalCollision,
    verticalCollision: grounded
  }
}

function sendMovement () {
  // start_game saja belum cukup: tunggu setidaknya satu posisi authoritative
  // (MovePlayer/respawn/correction) sebelum mulai client-authoritative movement.
  if (!client || !spawned || !movementReady || runtimeId === null || !pos) return

  if (postRespawnSync.active) {
    maybeCompletePostRespawnSync()
    if (!postRespawnSync.active) return

    const hold = authoritativePos ? { ...authoritativePos } : { ...pos }
    const inputData = buildInputData({
      walking: false,
      jumping: false,
      swimming: false,
      submerged: false,
      horizontalCollision: false,
      verticalCollision: onGround
    })
    const yawRad = yaw * Math.PI / 180

    try {
      const outgoingTick = ++tick
      const delta = { x: 0, y: 0, z: 0 }
      client.queue('player_auth_input', {
        pitch,
        yaw,
        position: hold,
        move_vector: { x: 0, z: 0 },
        head_yaw: yaw,
        input_data: inputData,
        input_mode: 'mouse',
        play_mode: 'screen',
        interaction_model: 'crosshair',
        interact_rotation: { x: pitch, z: yaw },
        tick: outgoingTick,
        delta,
        analogue_move_vector: { x: 0, z: 0 },
        camera_orientation: {
          x: -Math.sin(yawRad),
          y: 0,
          z: Math.cos(yawRad)
        },
        raw_move_vector: { x: 0, z: 0 }
      })
      movementPacketsSent++
      diagnostics.packets++
      predictionHistory.set(String(outgoingTick), { position: { ...hold }, delta, onGround, sentAt: Date.now() })
      while (predictionHistory.size > Math.max(32, debug.predictionHistorySize || 256)) {
        predictionHistory.delete(predictionHistory.keys().next().value)
      }
      consumeMovementAckFlags(inputData, outgoingTick, 'post-respawn-sync')
    } catch (err) {
      console.error('❌ Gagal kirim neutral post-respawn PlayerAuthInput:', err?.stack || err)
    }
    return
  }

  if (postRespawnRearm.active) {
    const now = Date.now()
    if (!clientInputLocks.move && now >= postRespawnRearm.maxUntil) {
      // Do not release normal wandering until the server has proven that movement
      // after respawn is accepted. Retry the controlled probe instead of entering
      // the old false stuck/recovery loop.
      postRespawnRearm.retryCount++
      postRespawnRearm.startedAt = now
      postRespawnRearm.warmupUntil = now + 350
      postRespawnRearm.maxUntil = now + 5000
      postRespawnRearm.probeStartedAt = 0
      postRespawnRearm.probePackets = 0
      postRespawnRearm.baselinePosition = authoritativePos ? { ...authoritativePos } : { ...pos }
      postRespawnRearm.latestPosition = postRespawnRearm.baselinePosition ? { ...postRespawnRearm.baselinePosition } : null
      predictionHistory.clear()
      correctionRejectStreak = 0
      lastCorrectionAuthoritativePos = authoritativePos ? { ...authoritativePos } : null
      ai.verticalVelocity = 0
      ai.horizontalVelocityX = 0
      ai.horizontalVelocityZ = 0
      postRespawnRearm.authoritativeOnGround = Boolean(onGround)
      postRespawnRearm.authoritativeDelta = { ...lastServerDelta }
      sendPostRespawnReinitialize(`retry-${postRespawnRearm.retryCount}`)
      postRespawnRearm.warmupUntil = now + 650
      console.warn(`⚠️ Post-respawn movement re-arm belum dibuktikan server; retry #${postRespawnRearm.retryCount}`)
    }

    if (clientInputLocks.move) {
      postRespawnRearm.inputLockWaitSince ||= now
      postRespawnRearm.maxUntil = Math.max(postRespawnRearm.maxUntil, now + 5000)
      if (!postRespawnRearm.inputLockWaitLogged) {
        postRespawnRearm.inputLockWaitLogged = true
        console.warn('🔒 Server mengunci input MOVE; re-arm menunggu unlock dan hanya mengirim neutral PlayerAuthInput', {
          locks: clientInputLocks.raw
        })
      }
    } else if (postRespawnRearm.inputLockWaitSince) {
      console.log(`🔓 Server membuka input MOVE setelah ${now - postRespawnRearm.inputLockWaitSince}ms; mulai warmup re-arm`)
      postRespawnRearm.inputLockWaitSince = 0
      postRespawnRearm.inputLockWaitLogged = false
      postRespawnRearm.warmupUntil = now + 350
      postRespawnRearm.maxUntil = now + 5000
      postRespawnRearm.probeStartedAt = 0
      postRespawnRearm.probePackets = 0
      postRespawnRearm.baselinePosition = authoritativePos ? { ...authoritativePos } : { ...pos }
    }

    const probing = !clientInputLocks.move && now >= postRespawnRearm.warmupUntil
    let next
    let delta
    let horizontalCollision = false
    let verticalCollision = onGround
    let moveX = 0
    let moveZ = 0

    if (probing) {
      postRespawnRearm.probeStartedAt ||= now
      moveZ = 0.35
      const predicted = calculatePostRespawnRearmProbe(moveX, moveZ)
      next = predicted.next
      delta = predicted.delta
      horizontalCollision = predicted.horizontalCollision
      verticalCollision = predicted.verticalCollision
    } else {
      next = authoritativePos ? { ...authoritativePos } : { ...pos }
      delta = { x: 0, y: 0, z: 0 }
    }

    const inputData = buildInputData({
      walking: probing,
      jumping: false,
      swimming: false,
      submerged: false,
      horizontalCollision,
      verticalCollision
    })
    const yawRad = yaw * Math.PI / 180

    try {
      const outgoingTick = ++tick
      client.queue('player_auth_input', {
        pitch,
        yaw,
        position: next,
        move_vector: { x: moveX, z: moveZ },
        head_yaw: yaw,
        input_data: inputData,
        input_mode: 'mouse',
        play_mode: 'screen',
        interaction_model: 'crosshair',
        interact_rotation: { x: pitch, z: yaw },
        tick: outgoingTick,
        delta,
        analogue_move_vector: { x: moveX, z: moveZ },
        camera_orientation: {
          x: -Math.sin(yawRad),
          y: 0,
          z: Math.cos(yawRad)
        },
        raw_move_vector: { x: moveX, z: moveZ }
      })
      movementPacketsSent++
      diagnostics.packets++
      if (probing) postRespawnRearm.probePackets++
      predictionHistory.set(String(outgoingTick), { position: { ...next }, delta: { ...delta }, onGround, sentAt: Date.now() })
      while (predictionHistory.size > Math.max(32, debug.predictionHistorySize || 256)) {
        predictionHistory.delete(predictionHistory.keys().next().value)
      }

      if (!postRespawnRearm.firstPacketLogged || (probing && postRespawnRearm.probePackets === 1)) {
        console.log(probing ? '🧪 Post-respawn movement probe dikirim:' : '🧪 Post-respawn re-arm neutral packet:', {
          tick: String(outgoingTick),
          tickSeededFromStartGame: movementTickSeeded,
          position: next,
          delta,
          move: { x: moveX, z: moveZ },
          inputData
        })
        postRespawnRearm.firstPacketLogged = true
      }

      pos = { ...next }
      consumeMovementAckFlags(inputData, outgoingTick, probing ? 'post-respawn-rearm-probe' : 'post-respawn-rearm-neutral')
    } catch (err) {
      console.error('❌ Gagal kirim post-respawn movement re-arm PlayerAuthInput:', err?.stack || err)
    }
    return
  }

  // During a proven server-side rejection, don't let local prediction run a full
  // block through the collision before the next correction arrives.
  capPredictionDrift()
  updateAI()
  updateJumpState()

  const env = getEnvironment()
  updateSwimState(env)

  const walking = ai.mode === 'WALK' || ai.mode === 'FLEE'
  const now = Date.now()
  let moveX = 0
  let moveZ = 0

  if (now < ai.strafeUntil && ai.strafeSign !== 0) {
    moveX = ai.strafeSign * 0.45
    moveZ = 0.70
  } else if (walking) {
    moveZ = 1.0
  }

  const { next, delta, horizontalCollision, verticalCollision } = calculatePredictedPosition(moveX, moveZ, env)
  const inputData = buildInputData({
    walking,
    jumping: ai.jumping,
    swimming: ai.swimming,
    submerged: env.submerged,
    horizontalCollision,
    verticalCollision
  })

  const yawRad = yaw * Math.PI / 180

  try {
    // Vanilla increments the client tick before constructing PlayerAuthInput.
    const outgoingTick = ++tick
    client.queue('player_auth_input', {
      pitch,
      yaw,
      position: next,
      move_vector: { x: moveX, z: moveZ },
      head_yaw: yaw,
      input_data: inputData,
      input_mode: 'mouse',
      play_mode: 'screen',
      interaction_model: 'crosshair',
      interact_rotation: { x: pitch, z: yaw },
      tick: outgoingTick,
      delta,
      analogue_move_vector: { x: moveX, z: moveZ },
      camera_orientation: {
        x: -Math.sin(yawRad),
        y: 0,
        z: Math.cos(yawRad)
      },
      raw_move_vector: { x: moveX, z: moveZ }
    })

    movementPacketsSent++
    diagnostics.packets++
    predictionHistory.set(String(outgoingTick), {
      position: { ...next },
      delta: { ...delta },
      onGround,
      sentAt: Date.now()
    })
    while (predictionHistory.size > Math.max(32, debug.predictionHistorySize || 256)) {
      predictionHistory.delete(predictionHistory.keys().next().value)
    }
    if (movementPacketsSent === 1) {
      console.log('➡️ PlayerAuthInput pertama dikirim:', {
        tick: String(outgoingTick),
        position: next,
        feetY: Number(feetYOf(next).toFixed(5)),
        delta,
        move: { x: moveX, z: moveZ },
        onGround,
        horizontalCollision,
        verticalCollision,
        inputData
      })
    }

    // Server tetap berhak mengoreksi melalui MovePlayer/CorrectPlayerMovePrediction,
    // tetapi di antara correction kita harus mempertahankan prediksi lokal antartick.
    pos = next

    // Acknowledgment correction/teleport hanya berlaku satu kali pada packet berikutnya.
    consumeMovementAckFlags(inputData, outgoingTick, 'normal-movement')

    if (debug.logMovement) {
      console.log('➡️ movement', { pos, delta, onGround, inputData })
    }
  } catch (err) {
    console.error('❌ Gagal encode/kirim player_auth_input:', err?.stack || err)
  }
}

function cleanupEntities () {
  const now = Date.now()
  for (const [key, entity] of entities) {
    if (now - entity.lastUpdate > 15000) entities.delete(key)
  }
}

function startRuntimeTimers () {
  clearRuntimeTimers()
  setManagedInterval(sendMovement, movement.tickMs)
  setManagedInterval(cleanupEntities, 10000)
  setManagedInterval(() => {
    if (world && pos) world.clearFarChunks(pos, config.world.chunkKeepRadius)
  }, 15000)

  if (debug.diagnostics) {
    setManagedInterval(() => diagnosticSnapshot('periodic'), debug.diagnosticsIntervalMs)
  }

  if (testing.forceJumpTest) {
    console.log(`🧪 Forced jump test aktif (${testing.forceJumpIntervalMs}ms)`)
    setManagedTimeout(() => {
      setManagedInterval(() => {
        if (spawned) startJump()
      }, testing.forceJumpIntervalMs)
    }, 1500)
  }
}

function respawnStateNumber (state) {
  if (typeof state === 'number') return state
  const map = {
    SearchingForSpawn: 0,
    ReadyToSpawn: 1,
    ClientReadyToSpawn: 2,
    searching_for_spawn: 0,
    ready_to_spawn: 1,
    client_ready_to_spawn: 2
  }
  return map[state] ?? Number(state)
}

function zeroBlockPosition () {
  return { x: 0, y: 0, z: 0 }
}

function resetLifecycleAfterRespawn () {
  lifecycle.dead = false
  lifecycle.deathAt = 0
  lifecycle.deathReason = null
  lifecycle.deathMessages = []
  lifecycle.respawnRequestSent = false
  lifecycle.respawnReadyReceived = false
  lifecycle.respawnActionSent = false
  lifecycle.respawnClientReadySent = false
  lifecycle.respawnSearchingReceived = false
  lifecycle.respawnAttempts = 0
  lifecycle.respawnPosition = null
  lifecycle.respawnLastState = null
  lifecycle.respawnStateHistory = []
  lifecycle.respawnActionLastAt = 0
  lifecycle.respawnActionChannel = null
  lifecycle.respawnActionTick = null
  lifecycle.finalizePending = false
}

function resetPostRespawnSync () {
  postRespawnSync.active = false
  postRespawnSync.startedAt = 0
  postRespawnSync.minUntil = 0
  postRespawnSync.maxUntil = 0
  postRespawnSync.feedbackSeen = false
  postRespawnSync.firstFeedbackAt = 0
  postRespawnSync.lastFeedbackAt = 0
  postRespawnSync.feedbackCount = 0
  postRespawnSync.initialPosition = null
  postRespawnSync.latestPosition = null
  postRespawnSync.largestShift = 0
  postRespawnSync.source = null
}

function resetPostRespawnRearm () {
  postRespawnRearm.active = false
  postRespawnRearm.startedAt = 0
  postRespawnRearm.maxUntil = 0
  postRespawnRearm.warmupUntil = 0
  postRespawnRearm.baselinePosition = null
  postRespawnRearm.latestPosition = null
  postRespawnRearm.probeStartedAt = 0
  postRespawnRearm.probePackets = 0
  postRespawnRearm.feedbackCount = 0
  postRespawnRearm.largestServerStep = 0
  postRespawnRearm.source = null
  postRespawnRearm.firstPacketLogged = false
  postRespawnRearm.retryCount = 0
  postRespawnRearm.authoritativeOnGround = false
  postRespawnRearm.authoritativeDelta = { x: 0, y: 0, z: 0 }
  postRespawnRearm.inputLockWaitSince = 0
  postRespawnRearm.inputLockWaitLogged = false
  postRespawnRearm.reinitCount = 0
  postRespawnRearm.reinitLastAt = 0
  postRespawnRearm.reinitLastSource = null
}

function beginPostRespawnRearm (initialPosition = null) {
  const now = Date.now()
  resetPostRespawnRearm()
  postRespawnRearm.active = true
  postRespawnRearm.startedAt = now
  postRespawnRearm.warmupUntil = now + 650
  postRespawnRearm.maxUntil = now + 5000
  postRespawnRearm.baselinePosition = initialPosition ? { ...initialPosition } : (authoritativePos ? { ...authoritativePos } : (pos ? { ...pos } : null))
  postRespawnRearm.latestPosition = postRespawnRearm.baselinePosition ? { ...postRespawnRearm.baselinePosition } : null
  postRespawnRearm.authoritativeOnGround = Boolean(onGround)
  postRespawnRearm.authoritativeDelta = { ...lastServerDelta }
  if (clientInputLocks.move) postRespawnRearm.inputLockWaitSince = now

  predictionHistory.clear()
  correctionRejectStreak = 0
  lastCorrectionAuthoritativePos = authoritativePos ? { ...authoritativePos } : null
  resetAiState()
  ai.mode = 'IDLE'
  ai.targetYaw = null
  ai.actionUntil = postRespawnRearm.maxUntil
  movementReady = Boolean(pos)

  sendPostRespawnReinitialize('begin-rearm')
  postRespawnRearm.warmupUntil = Math.max(postRespawnRearm.warmupUntil, Date.now() + 650)

  console.log('🧪 Post-respawn movement re-arm aktif; local-player re-init dikirim, AI tetap ditahan sampai server menerima probe movement')
}

function completePostRespawnRearm (reason = 'server accepted movement', position = null, source = null) {
  if (!postRespawnRearm.active) return
  const finalPosition = position || postRespawnRearm.latestPosition || authoritativePos || pos
  const summary = {
    reason,
    source: source || postRespawnRearm.source,
    probePackets: postRespawnRearm.probePackets,
    feedbackCount: postRespawnRearm.feedbackCount,
    largestServerStep: Number(postRespawnRearm.largestServerStep.toFixed(3)),
    retries: postRespawnRearm.retryCount,
    position: finalPosition,
    tick: String(tick)
  }

  if (finalPosition) {
    pos = { ...finalPosition }
    authoritativePos = { ...finalPosition }
    lastAuthoritativeAt = Date.now()
  }
  predictionHistory.clear()
  correctionRejectStreak = 0
  lastCorrectionAuthoritativePos = authoritativePos ? { ...authoritativePos } : null
  resetPostRespawnRearm()
  resetAiState()
  chooseNextAction()
  console.log('✅ Post-respawn movement re-arm berhasil; wandering dilepas', summary)
}

function notePostRespawnRearmFeedback (position, delta = null, source = 'authoritative', authoritativeOnGround = null) {
  if (!postRespawnRearm.active || !position) return
  const baseline = postRespawnRearm.baselinePosition
  const serverStep = baseline ? horizontalDistance(position, baseline) : 0
  const serverSpeed = delta ? Math.hypot(Number(delta.x) || 0, Number(delta.z) || 0) : 0
  postRespawnRearm.latestPosition = { ...position }
  if (delta) postRespawnRearm.authoritativeDelta = { ...delta }
  if (typeof authoritativeOnGround === 'boolean') postRespawnRearm.authoritativeOnGround = authoritativeOnGround
  postRespawnRearm.feedbackCount++
  postRespawnRearm.largestServerStep = Math.max(postRespawnRearm.largestServerStep, serverStep)
  postRespawnRearm.source = source

  if (postRespawnRearm.probeStartedAt && (serverStep >= 0.06 || serverSpeed >= 0.015)) {
    completePostRespawnRearm('authoritative movement observed', position, source)
  }
}

function beginPostRespawnSync (initialPosition = null) {
  const now = Date.now()
  const minMs = Math.max(500, Number(survival.postRespawnSyncMinMs) || 2200)
  const maxMs = Math.max(minMs + 500, Number(survival.postRespawnSyncMaxMs) || 4200)

  postRespawnSync.active = true
  postRespawnSync.startedAt = now
  postRespawnSync.minUntil = now + minMs
  postRespawnSync.maxUntil = now + maxMs
  postRespawnSync.feedbackSeen = false
  postRespawnSync.firstFeedbackAt = 0
  postRespawnSync.lastFeedbackAt = 0
  postRespawnSync.feedbackCount = 0
  postRespawnSync.initialPosition = initialPosition ? { ...initialPosition } : (pos ? { ...pos } : null)
  postRespawnSync.latestPosition = initialPosition ? { ...initialPosition } : (authoritativePos ? { ...authoritativePos } : (pos ? { ...pos } : null))
  postRespawnSync.largestShift = 0
  postRespawnSync.source = null

  predictionHistory.clear()
  correctionRejectStreak = 0
  lastCorrectionAuthoritativePos = null
  receivedServerData = false
  handledTeleportPending = false
  resetAiState()
  ai.mode = 'IDLE'
  ai.targetYaw = null
  ai.actionUntil = postRespawnSync.maxUntil
  movementReady = Boolean(pos)

  console.log(`🧭 Post-respawn sync aktif (${minMs}–${maxMs}ms); AI ditahan sampai posisi server stabil`)
}

function notePostRespawnFeedback (position, source = 'authoritative') {
  if (!postRespawnSync.active || !position) return
  const now = Date.now()
  const base = postRespawnSync.initialPosition
  const shift = base ? distance3D(base, position) : 0
  const previousLargestShift = postRespawnSync.largestShift
  postRespawnSync.feedbackSeen = true
  postRespawnSync.firstFeedbackAt ||= now
  postRespawnSync.lastFeedbackAt = now
  postRespawnSync.feedbackCount++
  postRespawnSync.latestPosition = { ...position }
  postRespawnSync.largestShift = Math.max(postRespawnSync.largestShift, shift)
  postRespawnSync.source = source

  // Some servers/add-ons implement a respawn-return teleport through
  // CorrectPlayerMovePrediction instead of MovePlayer(mode=teleport). Bedrock's
  // HandledTeleport input flag tells the server that the client consumed a
  // teleport and client-auth movement may resume. Treat a multi-block
  // post-respawn correction as teleport-equivalent and ACK it on the next
  // PlayerAuthInput. Also preserve ClientAckServerData for the same correction.
  if (shift >= 2 && String(source).includes('CorrectPlayerMovePrediction')) {
    if (!handledTeleportPending) {
      console.log(`🧷 Post-respawn teleport-equivalent terdeteksi (${shift.toFixed(2)} blok); queue handled_teleport ACK`)
    }
    handledTeleportPending = true
    handledTeleportReason = `${source} shift=${shift.toFixed(2)}`
    receivedServerData = true
  }

  if (postRespawnSync.feedbackCount <= 2 || shift >= previousLargestShift + 0.5) {
    console.log(`🧭 Post-respawn authoritative #${postRespawnSync.feedbackCount} (${source}, shift=${shift.toFixed(2)} blok)`, position)
  }
}

function completePostRespawnSync (reason = 'settled') {
  if (!postRespawnSync.active) return
  const finalPosition = postRespawnSync.latestPosition || authoritativePos || pos
  const summary = {
    feedbackCount: postRespawnSync.feedbackCount,
    largestShift: Number(postRespawnSync.largestShift.toFixed(2)),
    source: postRespawnSync.source,
    position: finalPosition
  }

  if (finalPosition) {
    pos = { ...finalPosition }
    authoritativePos = { ...finalPosition }
    lastAuthoritativeAt = Date.now()
  }

  predictionHistory.clear()
  correctionRejectStreak = 0
  lastCorrectionAuthoritativePos = authoritativePos ? { ...authoritativePos } : null
  ai.authoritativeRebaseUntil = 0
  ai.serverBlockedYaw = null
  ai.serverBlockedUntil = 0
  ai.serverBlockedReason = null
  // Preserve one-shot movement ACK flags across sync -> re-arm. A large
  // authoritative teleport/correction may require the first neutral auth-input
  // after settle to acknowledge server data before movement is accepted.
  resetPostRespawnSync()
  resetAiState()
  console.log(`✅ Post-respawn sync selesai (${reason}); masuk movement re-arm`, summary)
  beginPostRespawnRearm(finalPosition)
}

function maybeCompletePostRespawnSync () {
  if (!postRespawnSync.active) return
  const now = Date.now()
  if (now >= postRespawnSync.maxUntil) {
    completePostRespawnSync(postRespawnSync.feedbackSeen ? 'max settle window' : 'no authoritative feedback fallback')
    return
  }
  const quietMs = Math.max(100, Number(survival.postRespawnSyncQuietMs) || 350)
  if (postRespawnSync.feedbackSeen && now >= postRespawnSync.minUntil && now - postRespawnSync.lastFeedbackAt >= quietMs) {
    completePostRespawnSync('authoritative position settled')
  }
}

function finishRespawn (source = 'unknown', authoritativePosition = null) {
  if (!lifecycle.dead) return

  if (authoritativePosition) {
    pos = { ...authoritativePosition }
    authoritativePos = { ...authoritativePosition }
    lastAuthoritativeAt = Date.now()
    movementReady = true
  }

  spawned = true
  sawDeathState = false
  onGround = false
  currentHealth = Math.max(1, currentHealth || 20)
  lastHealth = currentHealth
  resetAiState()
  resetLifecycleAfterRespawn()
  beginPostRespawnSync(authoritativePosition || pos)
  console.log(`✅ Respawn handshake selesai (${source}); menunggu sinkronisasi posisi authoritative`)
  if (debug.diagnostics && pos) diagnosticSnapshot('respawn-handshake-complete', { source })
}

function validRuntimeIdForRespawn () {
  if (runtimeId === null || runtimeId === undefined) return null
  try {
    return BigInt(runtimeId) !== 0n ? runtimeId : null
  } catch {
    return Number(runtimeId) !== 0 ? runtimeId : null
  }
}

function maybeAdoptRespawnRuntimeId (packetRuntimeId) {
  // Some vanilla/BDS respawn packets use 0 as the runtime ID. Never replace a
  // valid local player runtime ID with 0: doing so makes subsequent respawn
  // packets target entity 0 instead of the player.
  if (packetRuntimeId === null || packetRuntimeId === undefined) return
  let nonZero = false
  try { nonZero = BigInt(packetRuntimeId) !== 0n } catch { nonZero = Number(packetRuntimeId) !== 0 }
  if (!nonZero) return

  if (runtimeId === null || runtimeId === undefined) {
    runtimeId = packetRuntimeId
    console.log(`♻️ Runtime ID respawn diadopsi: ${String(runtimeId)}`)
  }
}

function sendRespawnPlayerAction (trigger = 'server-ready') {
  const rid = validRuntimeIdForRespawn()
  if (!client || rid === null || !lifecycle.dead) return
  if (!lifecycle.respawnReadyReceived || !lifecycle.respawnClientReadySent || lifecycle.respawnActionSent) return

  const params = makeRespawnPlayerActionParams(rid, respawnActionCompat.token)

  try {
    const queued = client.queue('player_action', params)
    if (queued === false) throw new Error('non-destructive guard rejected respawn PlayerAction')

    lifecycle.respawnActionSent = true
    lifecycle.respawnActionLastAt = Date.now()
    lifecycle.respawnActionChannel = 'player_action'
    lifecycle.respawnActionTick = null
    console.log(
      `♻️ Respawn final PlayerAction dikirim (${trigger}, runtime=${String(rid)}, ` +
      `wire=${respawnActionCompat.expectedOrdinal}, pos=0,0,0, result=0,0,0, face=${PROTOCOL_2193_RESPAWN_FACE}, token=${respawnActionCompat.token})`
    )

    // The working Mineflayer-Bedrock flow does not wait for a fourth explicit
    // respawn-completion packet after this final PlayerAction. Give BDS a short
    // grace window for authoritative feedback; if none arrives, accept the
    // ReadyToSpawn position and resume locally.
    if (!lifecycle.finalizePending) {
      lifecycle.finalizePending = true
      setManagedTimeout(() => {
        if (!lifecycle.dead || !canFinalizeRespawn()) return
        finishRespawn('final PlayerAction grace elapsed', lifecycle.respawnPosition || pos)
      }, Math.max(200, Number(survival.respawnFinalizeGraceMs) || 1200))
    }
  } catch (err) {
    lifecycle.respawnActionSent = false
    lifecycle.respawnActionChannel = null
    lifecycle.respawnActionTick = null
    console.error('❌ Gagal mengirim PlayerAction(respawn):', err?.stack || err)
  }
}

function sendRespawnClientReady (trigger = 'timer') {
  const rid = validRuntimeIdForRespawn()
  if (!client || rid === null || !lifecycle.dead) return
  if (lifecycle.respawnReadyReceived || lifecycle.respawnActionSent) return

  // BDS 1.26.51 used by this bot has repeatedly demonstrated this transition:
  // server SearchingForSpawn(0) -> client ClientReadyToSpawn(2) -> server
  // ReadyToSpawn(1). Keep that observed ordering, but combine it with the
  // corrected protocol-2193 PlayerAction payload (wire 6, face 0) as the final
  // step. Position=origin matches the request that successfully reached state 1
  // in v1.7.6; the server supplies the real respawn position in state 1.
  const maxRequestAttempts = Math.min(Math.max(1, Number(survival.respawnMaxRetries) || 1), 2)
  if (lifecycle.respawnAttempts >= maxRequestAttempts) return

  lifecycle.respawnAttempts++
  lifecycle.respawnRequestSent = true
  lifecycle.respawnClientReadySent = true

  try {
    client.queue('respawn', makeRespawnClientReadyParams(rid, zeroBlockPosition()))
    console.log(
      `♻️ Respawn request ClientReadyToSpawn dikirim (${trigger}, attempt ` +
      `${lifecycle.respawnAttempts}/${maxRequestAttempts}, runtime=${String(rid)}, pos=0,0,0)`
    )
  } catch (err) {
    lifecycle.respawnClientReadySent = false
    console.error('❌ Gagal mengirim Respawn(ClientReadyToSpawn):', err?.stack || err)
    return
  }

  if (lifecycle.respawnAttempts < maxRequestAttempts) {
    setManagedTimeout(() => {
      if (!lifecycle.dead || lifecycle.respawnReadyReceived || lifecycle.respawnActionSent) return
      if (lifecycle.respawnLastState === null || lifecycle.respawnLastState === 0) {
        sendRespawnClientReady('single-retry')
      }
    }, Math.max(1000, survival.respawnRetryMs * 2))
  }
}

function canFinalizeRespawn () {
  return lifecycle.dead &&
    lifecycle.respawnClientReadySent &&
    lifecycle.respawnReadyReceived &&
    lifecycle.respawnActionSent
}

function scheduleRespawnHandshake () {
  if (!survival.enabled || !lifecycle.dead) return

  setManagedTimeout(() => {
    if (lifecycle.dead && !lifecycle.respawnRequestSent) sendRespawnClientReady('initial-fallback')
  }, survival.respawnInitialDelayMs)

  setManagedTimeout(() => {
    if (!lifecycle.dead) return
    console.error(`⏱️ Respawn timeout setelah ${survival.respawnTimeoutMs}ms; paksa reconnect sebagai fallback`)
    try {
      disconnectBlackBox?.markLocalDisconnect('Respawn handshake timeout', 'respawn-timeout')
      client?.disconnect('Respawn handshake timeout', true)
    } catch (err) {
      console.error('❌ Gagal memutus koneksi untuk respawn fallback:', err?.message || err)
    }
  }, survival.respawnTimeoutMs)
}

function markDead (reason = 'unknown', details = {}) {
  const firstDetection = !lifecycle.dead

  lifecycle.dead = true
  lifecycle.deathAt ||= Date.now()
  if (reason && reason !== 'unknown') lifecycle.deathReason = reason
  if (Array.isArray(details.messages) && details.messages.length) lifecycle.deathMessages = [...details.messages]

  if (firstDetection && debug.diagnostics && pos) {
    diagnosticSnapshot('death', {
      deathReason: lifecycle.deathReason,
      deathMessages: lifecycle.deathMessages,
      health: currentHealth,
      lastDamageAmount,
      lastDamageAgeMs: lastDamageAt ? Date.now() - lastDamageAt : null,
      lastThreatSnapshot,
      lastDeathInfo,
      nearbyEntitiesAtDeath: nearbyEntitiesSummary(12)
    })
  }

  spawned = false
  movementReady = false
  onGround = false
  ai.verticalVelocity = 0
  ai.horizontalVelocityX = 0
  ai.horizontalVelocityZ = 0
  ai.jumping = false
  ai.jumpReleasePending = false
  ai.swimming = false
  ai.emergencySurfacing = false
  predictionHistory.clear()
  correctionRejectStreak = 0
  lastCorrectionAuthoritativePos = null
  resetPostRespawnSync()
  resetPostRespawnRearm()

  if (firstDetection) {
    console.log(`💀 Bot mati (${lifecycle.deathReason || reason}), mulai auto-respawn...`)
    scheduleRespawnHandshake()
  } else if (debug.logRawDeathPackets && details.source) {
    console.log(`☠️ Death detail tambahan (${details.source}):`, details)
  }
}

function handleRespawn (packet) {
  const state = respawnStateNumber(packet.state)

  // Initial join juga dapat memuat RespawnPacket. Jangan menjalankan death flow
  // sebelum bot pernah benar-benar spawn.
  if (!hasEverSpawned && !lifecycle.dead) {
    if (packet.position && (state === 1 || state === 2)) {
      pos = { ...packet.position }
      onGround = looksGroundedFromEyePosition(pos)
      if (onGround) pos = snapGroundedEyePosition(pos, true)
      movementReady = true
    }
    if (debug.logPackets) console.log(`♻️ Respawn state ${state} saat initial join (tidak dianggap mati)`)
    return
  }

  if (!lifecycle.dead && state === 0) {
    sawDeathState = true
    markDead('respawn/searching', { source: 'respawn' })
  }

  if (!lifecycle.dead) return

  if (packet.position) lifecycle.respawnPosition = { ...packet.position }
  maybeAdoptRespawnRuntimeId(packet.runtime_entity_id)
  lifecycle.respawnLastState = Number.isFinite(state) ? state : null
  lifecycle.respawnStateHistory.push({ state: lifecycle.respawnLastState, at: Date.now() })
  if (lifecycle.respawnStateHistory.length > 12) lifecycle.respawnStateHistory.shift()

  console.log('♻️ Respawn packet dari server:', {
    state,
    position: packet.position,
    runtimeEntityId: packet.runtime_entity_id !== undefined ? String(packet.runtime_entity_id) : undefined,
    keptRuntimeId: runtimeId !== null && runtimeId !== undefined ? String(runtimeId) : null
  })

  if (state === 0) {
    lifecycle.respawnSearchingReceived = true
    // This is the only request ordering that has actually advanced the target
    // BDS from state 0 to state 1 in our controlled tests.
    if (!lifecycle.respawnRequestSent) sendRespawnClientReady('server-searching')
  } else if (state === 1) {
    lifecycle.respawnReadyReceived = true
    console.log('♻️ Server ReadyToSpawn diterima')
    // Observed target-server transition: client state2 -> server state1.
    // Finalise with the standalone PlayerAction used by the working
    // Mineflayer-Bedrock implementation. The target StartGame explicitly
    // reports server_authoritative_block_breaking=false.
    setManagedTimeout(() => sendRespawnPlayerAction('server-ready'), survival.respawnActionDelayMs)
  } else if (state === 2 && canFinalizeRespawn()) {
    finishRespawn('server final Respawn state 2', packet.position || lifecycle.respawnPosition)
  }
}

function scheduleReconnect (why = 'connection closed') {
  if (shuttingDown || !reconnect.enabled || reconnectTimer) return
  const delays = reconnect.delaysMs.length ? reconnect.delaysMs : [5000]
  const delay = delays[Math.min(reconnectAttempt, delays.length - 1)]
  reconnectAttempt++

  console.log(`🔁 ${why}. Coba konek lagi dalam ${(delay / 1000).toFixed(1)} detik...`)
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null
    connectBot().catch(err => {
      console.error('❌ Reconnect gagal:', err?.stack || err)
      scheduleReconnect('reconnect error')
    })
  }, delay)
}

function resetConnectionState () {
  clearRuntimeTimers()
  entities.clear()
  runtimeId = null
  pos = null
  yaw = 0
  pitch = 0
  tick = 0n
  startGameCurrentTick = null
  rewindHistorySize = null
  movementTickSeeded = false
  spawned = false
  readyHandshakeSent = false
  movementPacketsSent = 0
  movementCorrections = 0
  sawDeathState = false
  hasEverSpawned = false
  onGround = false
  receivedServerData = false
  handledTeleportPending = false
  handledTeleportReason = null
  handledTeleportAckSentAt = 0
  clientInputLocks = {
    raw: null,
    move: false,
    jump: false,
    sneak: false,
    mount: false,
    dismount: false,
    rotation: false,
    updatedAt: 0
  }
  movementReady = false
  world = new World()
  serverAir = null
  serverMaxAir = 300
  authoritativePos = null
  lastAuthoritativeAt = 0
  lastServerDelta = { x: 0, y: 0, z: 0 }
  lastCorrectionAt = 0
  totalAuthoritativeDistance = 0
  currentHealth = 20
  lastHealth = 20
  lastDamageAt = 0
  lastDamageAmount = 0
  lastDeathInfo = null
  lastThreatSnapshot = null
  floorDecodeWarningShown = false
  predictionHistory.clear()
  resetLifecycleAfterRespawn()
  resetPostRespawnSync()
  resetPostRespawnRearm()
  diagnostics.windowStartedAt = Date.now()
  diagnostics.packets = 0
  diagnostics.corrections = 0
  diagnostics.correctionErrorSum = 0
  diagnostics.correctionErrorMax = 0
  diagnostics.lastCorrectionError = 0
  diagnostics.lastPeriodicAt = 0
  resetAiState()
}

function recordAuthoritativeState (position, delta = null) {
  if (position) {
    if (authoritativePos) {
      const step = horizontalDistance(position, authoritativePos)
      // Teleports/resets shouldn't inflate travel statistics.
      if (step < 16) totalAuthoritativeDistance += step
    }
    authoritativePos = { ...position }
    lastAuthoritativeAt = Date.now()
  }
  if (delta) lastServerDelta = { ...delta }
}

function observeServerMovementRejection (serverPosition, correctionError) {
  if (!serverPosition) return
  if (postRespawnSync.active || postRespawnRearm.active) {
    correctionRejectStreak = 0
    lastCorrectionAuthoritativePos = { ...serverPosition }
    return
  }
  const activelyWalking = (ai.mode === 'WALK' || ai.mode === 'FLEE') && !ai.jumping && !ai.swimming
  const prior = lastCorrectionAuthoritativePos || authoritativePos
  const serverStep = prior ? horizontalDistance(serverPosition, prior) : null
  lastCorrectionAuthoritativePos = { ...serverPosition }

  if (!activelyWalking) {
    correctionRejectStreak = 0
    return
  }

  const rejected = Number.isFinite(correctionError) &&
    correctionError >= recovery.serverRejectMinCorrectionError &&
    serverStep !== null && serverStep <= recovery.serverRejectMaxAuthoritativeStep

  if (rejected) correctionRejectStreak++
  else if (serverStep !== null && serverStep >= recovery.serverRejectResetStep) {
    correctionRejectStreak = 0
    ai.authoritativeRebaseUntil = 0
  } else correctionRejectStreak = Math.max(0, correctionRejectStreak - 1)

  if (correctionRejectStreak >= recovery.serverRejectSamples) {
    enterAuthoritativeRebase(`server-reject x${correctionRejectStreak}`)
    if (correctionRejectStreak === recovery.serverRejectSamples) {
      rememberServerBlockedHeading(`server-reject x${correctionRejectStreak}`)
      logAi(`🧱 Server menolak movement berulang di yaw ${yaw.toFixed(1)}°; arah ini ditandai blocked sementara`)
      if (debug.diagnostics) diagnosticSnapshot('server-movement-rejected', {
        serverStep: roundNumber(serverStep, 3),
        correctionError: roundNumber(correctionError, 3)
      })
    }
  }
}

function disconnectStateSnapshot (thisClient) {
  const now = Date.now()
  return {
    finalTransport: thisClient?.options?.transport || 'auto/unknown',
    spawned,
    movementReady,
    hasEverSpawned,
    runtimeId: runtimeId === null ? null : String(runtimeId),
    health: currentHealth,
    dead: lifecycle.dead,
    deathReason: lifecycle.deathReason,
    deathAgeMs: lifecycle.deathAt ? now - lifecycle.deathAt : null,
    respawnAttempts: lifecycle.respawnAttempts,
    respawnRequestSent: lifecycle.respawnRequestSent,
    respawnReadyReceived: lifecycle.respawnReadyReceived,
    respawnActionSent: lifecycle.respawnActionSent,
    respawnClientReadySent: lifecycle.respawnClientReadySent,
    respawnSearchingReceived: lifecycle.respawnSearchingReceived,
    respawnLastState: lifecycle.respawnLastState,
    respawnStateHistory: lifecycle.respawnStateHistory.map(entry => ({ state: entry.state, ageMs: now - entry.at })),
    respawnActionWire: respawnActionCompat.expectedOrdinal,
    respawnActionToken: respawnActionCompat.token,
    respawnActionFace: PROTOCOL_2193_RESPAWN_FACE,
    respawnActionChannel: lifecycle.respawnActionChannel,
    respawnActionTick: lifecycle.respawnActionTick === null ? null : String(lifecycle.respawnActionTick),
    respawnMapperShimActive: respawnActionCompat.mapperShimActive,
    position: roundVec(pos),
    authoritativePosition: roundVec(authoritativePos),
    authoritativeAgeMs: lastAuthoritativeAt ? now - lastAuthoritativeAt : null,
    lastServerDelta: roundVec(lastServerDelta, 4),
    onGround,
    movementPacketsSent,
    movementCorrections,
    lastCorrectionAgeMs: lastCorrectionAt ? now - lastCorrectionAt : null,
    correctionErrorLast: roundNumber(diagnostics.lastCorrectionError, 3),
    correctionErrorMax: roundNumber(diagnostics.correctionErrorMax, 3),
    correctionRejectStreak,
    aiMode: ai.mode,
    stuckCount: ai.stuckCount,
    recoveryAction: ai.recoveryAction,
    postRespawnSyncActive: postRespawnSync.active,
    postRespawnFeedbackCount: postRespawnSync.feedbackCount,
    postRespawnLargestShift: roundNumber(postRespawnSync.largestShift, 2),
    postRespawnSyncSource: postRespawnSync.source,
    postRespawnRearmActive: postRespawnRearm.active,
    postRespawnRearmProbePackets: postRespawnRearm.probePackets,
    postRespawnRearmFeedbackCount: postRespawnRearm.feedbackCount,
    postRespawnRearmLargestServerStep: roundNumber(postRespawnRearm.largestServerStep, 3),
    handledTeleportPending,
    handledTeleportAckAgeMs: handledTeleportAckSentAt ? Date.now() - handledTeleportAckSentAt : null,
    postRespawnReinitCount: postRespawnRearm.reinitCount,
    postRespawnReinitAgeMs: postRespawnRearm.reinitLastAt ? Date.now() - postRespawnRearm.reinitLastAt : null,
    postRespawnReinitSource: postRespawnRearm.reinitLastSource,
    movementTick: String(tick),
    movementTickSeeded,
    startGameCurrentTick: startGameCurrentTick !== null && startGameCurrentTick !== undefined ? String(startGameCurrentTick) : null,
    rewindHistorySize,
    authoritativeRebase: authoritativeRebaseActive(now),
    safetyBlockedPackets: thisClient?.__nonDestructiveGuardStats?.blocked || 0,
    safetyLastBlocked: thisClient?.__nonDestructiveGuardStats?.lastBlocked || null,
    reconnectAttempt
  }
}

async function connectBot () {
  if (shuttingDown) return
  resetConnectionState()

  console.log(`\n🤖 Minecraft-Bedrock-AFK v${packageVersion}`)
  console.log(`🔎 Menghubungkan ${server.username} ke ${server.host}:${server.port}`)
  console.log(`   Instance ID: ${instance.id} (${instance.source}${instance.persistent ? ', persistent' : ', ephemeral'})`)
  console.log(`   Transport: ${server.transport}`)
  console.log(`   Respawn wire action: ${respawnActionCompat.expectedOrdinal} via token '${respawnActionCompat.token}'${respawnActionCompat.mapperShimActive ? ' (compat shim aktif)' : ''}`)
  if (respawnActionCompat.mapperShimActive) {
    console.warn(`⚠️ Mapper protocol symbolic respawn=${respawnActionCompat.symbolicRespawnOrdinal}; protocol 2193 butuh ${respawnActionCompat.expectedOrdinal}. Compatibility shim digunakan.`)
  }

  const options = {
    host: server.host,
    port: server.port,
    username: server.username,
    offline: server.offline,
    connectTimeout: server.connectTimeout,
    pingTimeout: server.pingTimeout,
    followPort: server.followPort,
    autoInitPlayer: true
  }

  // Jangan isi transport jika 'auto': bedrock-protocol 3.60.1 akan melakukan
  // discovery RakNet + NetherNet dan memilih yang berhasil.
  if (server.transport === 'raknet' || server.transport === 'nethernet') {
    options.transport = server.transport
  }

  client = bedrock.createClient(options)
  installNonDestructiveGuard(client, {
    enabled: safety.nonDestructive,
    logBlocked: safety.logBlockedPackets
  })
  const thisClient = client

  disconnectBlackBox = debug.disconnectBlackBox
    ? createDisconnectBlackBox({
        username: server.username,
        host: server.host,
        port: server.port,
        requestedTransport: server.transport,
        pingTimeoutMs: server.pingTimeout,
        logFile: debug.disconnectBlackBoxFile,
        maxLogBytes: debug.disconnectBlackBoxMaxBytes,
        maxPacketHistory: debug.disconnectPacketHistory
      })
    : null
  const thisBlackBox = disconnectBlackBox

  if (safety.nonDestructive) {
    console.log('🛡️ Non-destructive safety guard aktif (break/place/use-on-block/attack/commands diblokir)')
  }

  thisClient.on('connect_allowed', () => {
    thisBlackBox?.markPhase('connect_allowed')
  })

  thisClient.on('session', () => {
    thisBlackBox?.markPhase('session')
  })

  thisClient.on('login', () => {
    thisBlackBox?.markPhase('login')
  })

  thisClient.on('connect', () => {
    thisBlackBox?.markPhase('connect')
    console.log(`🔌 Transport terhubung (${thisClient.options.transport || 'auto'})`)
  })

  thisClient.on('join', () => {
    thisBlackBox?.markPhase('join')
    console.log('✅ Bot berhasil login ke server')
  })

  thisClient.on('start_game', packet => {
    runtimeId = packet.runtime_entity_id
    thisClient.clientRuntimeId = runtimeId

    startGameCurrentTick = packet.current_tick ?? null
    rewindHistorySize = packet.rewind_history_size ?? null
    if (startGameCurrentTick !== null && startGameCurrentTick !== undefined) {
      try {
        const seed = typeof startGameCurrentTick === 'bigint' ? startGameCurrentTick : BigInt(startGameCurrentTick)
        if (seed >= 0n) {
          tick = seed
          movementTickSeeded = true
        }
      } catch (err) {
        console.warn('⚠️ Gagal memakai start_game.current_tick sebagai movement tick seed:', startGameCurrentTick, err?.message || err)
      }
    }

    if (packet.player_position) pos = { ...packet.player_position }
    if (packet.rotation) {
      // Bedrock vec2f memakai field { x, z }: x=pitch, z=yaw.
      pitch = packet.rotation.x || 0
      yaw = packet.rotation.z || 0
    }

    console.log(`🆔 Runtime ID: ${String(runtimeId)}`)
    if (pos) console.log(`📍 Posisi awal: ${pos.x.toFixed(2)}, ${pos.y.toFixed(2)}, ${pos.z.toFixed(2)}`)
    console.log('🕒 Movement tick correlation:', {
      startGameCurrentTick: startGameCurrentTick !== null && startGameCurrentTick !== undefined ? String(startGameCurrentTick) : null,
      nextPlayerAuthInputTick: String(tick + 1n),
      rewindHistorySize,
      seeded: movementTickSeeded
    })
    const movementSettings = packet.movement_settings || packet.movement_settings_data || packet.player_movement_settings || null
    if (movementSettings) console.log('🎛️ Server movement settings:', movementSettings)
    if (packet.server_authoritative_block_breaking !== undefined) {
      console.log(`🎛️ Server authoritative block actions: ${Boolean(packet.server_authoritative_block_breaking)}`)
    }
  })

  thisClient.once('available_commands', () => {
    sendClientReadyHandshake('available_commands')
  })

  thisClient.on('play_status', packet => {
    if (packet.status === 'player_spawn') {
      const respawnPhase = lifecycle.dead || postRespawnSync.active || postRespawnRearm.active
      console.log('🎮 play_status: player_spawn', {
        respawnPhase,
        dead: lifecycle.dead,
        postRespawnSync: postRespawnSync.active,
        postRespawnRearm: postRespawnRearm.active
      })
      if (respawnPhase) {
        sendPostRespawnReinitialize('play_status/player_spawn')
      } else {
        // Fallback jika server tidak mengirim available_commands dalam urutan normal.
        sendClientReadyHandshake('play_status/player_spawn')
      }
    } else if (debug.logPackets) {
      console.log('🎮 play_status:', packet.status)
    }
  })

  thisClient.on('spawn', () => {
    thisBlackBox?.markPhase('spawn')
    const wasRespawning = lifecycle.dead
    hasEverSpawned = true
    reconnectAttempt = 0

    if (wasRespawning) {
      finishRespawn('bedrock-protocol spawn event', pos)
    } else {
      spawned = true
      resetAiState()
      chooseNextAction()
    }

    startRuntimeTimers()
    console.log(wasRespawning
      ? '🌍 Spawn event setelah kematian diterima'
      : `🌍 Bot masuk dunia. Transport final: ${thisClient.options.transport}`)
    console.log(movementReady
      ? '🤖 AI wandering aktif'
      : '🤖 AI siap; menunggu posisi authoritative pertama sebelum mengirim movement')
  })

  // Black-box ringan: nama packet + timestamp saja, bukan payload besar.
  // bedrock-protocol memancarkan event 'packet' untuk semua packet inbound.
  thisClient.on('packet', packet => {
    thisBlackBox?.recordPacket(packet)
  })

  thisClient.on('update_client_input_locks', packet => {
    const previousMove = clientInputLocks.move
    clientInputLocks = decodeClientInputLocks(packet)
    console.log('🔐 Server UpdateClientInputLocks:', {
      move: clientInputLocks.move,
      jump: clientInputLocks.jump,
      sneak: clientInputLocks.sneak,
      rotation: clientInputLocks.rotation,
      raw: clientInputLocks.raw,
      packet
    })

    if (postRespawnRearm.active && previousMove && !clientInputLocks.move) {
      const now = Date.now()
      postRespawnRearm.inputLockWaitSince = postRespawnRearm.inputLockWaitSince || now
      postRespawnRearm.warmupUntil = now + 350
      postRespawnRearm.maxUntil = now + 5000
    }
  })

  thisClient.on('set_entity_motion', packet => {
    const id = packet.runtime_entity_id ?? packet.runtime_id ?? packet.entity_runtime_id
    if (runtimeId !== null && id !== undefined && String(id) !== String(runtimeId)) return
    const motion = packet.velocity || packet.motion || packet.delta || null
    if (motion && (postRespawnSync.active || postRespawnRearm.active || lifecycle.dead)) {
      console.log('🧲 Server SetEntityMotion untuk bot:', {
        tick: packet.tick !== undefined ? String(packet.tick) : null,
        motion,
        postRespawnSync: postRespawnSync.active,
        postRespawnRearm: postRespawnRearm.active,
        dead: lifecycle.dead
      })
    }
    if (motion && postRespawnRearm.active) {
      postRespawnRearm.authoritativeDelta = { ...motion }
      ai.verticalVelocity = 0
    }
  })

  thisClient.on('packet_violation_warning', packet => {
    console.error('🚨 Server packet_violation_warning:', packet)
    thisBlackBox?.recordViolation(packet)
  })

  thisClient.on('heartbeat', () => {
    thisBlackBox?.recordHeartbeat()
  })

  thisClient.on('add_player', packet => {
    trackEntity({
      runtimeId: packet.runtime_id,
      uniqueId: packet.unique_id,
      username: packet.username,
      entityType: 'player',
      position: packet.position
    })
  })

  thisClient.on('add_entity', packet => {
    trackEntity({
      runtimeId: packet.runtime_id,
      uniqueId: packet.unique_id,
      entityType: packet.entity_type,
      position: packet.position
    })
  })

  thisClient.on('move_entity', packet => {
    updateEntityPosition(packet.runtime_entity_id, packet.position)
  })

  thisClient.on('move_player', packet => {
    const id = packet.runtime_id ?? packet.runtime_entity_id
    const idIsSelf = runtimeId === null || id === undefined || String(id) === String(runtimeId)
    const idIsRespawnSentinel = lifecycle.dead && (String(id) === '0' || packet.mode === 'reset' || packet.mode === 'respawn')

    if (!idIsSelf && !idIsRespawnSentinel) {
      updateEntityPosition(id, packet.position)
      return
    }

    if (lifecycle.dead && canFinalizeRespawn() && packet.position) {
      console.log('♻️ MovePlayer setelah final respawn:', {
        runtimeId: id !== undefined ? String(id) : null,
        mode: packet.mode,
        position: packet.position
      })
    }

    if (typeof packet.on_ground === 'boolean') onGround = packet.on_ground
    if (packet.position) {
      pos = snapGroundedEyePosition(packet.position, onGround)
      movementReady = true
      recordAuthoritativeState(pos, packet.delta || null)
      notePostRespawnFeedback(pos, `MovePlayer/${packet.mode || 'normal'}`)
      notePostRespawnRearmFeedback(pos, packet.delta || null, `MovePlayer/${packet.mode || 'normal'}`, packet.on_ground)
    }
    if (typeof packet.yaw === 'number') yaw = packet.yaw
    if (typeof packet.pitch === 'number') pitch = packet.pitch

    if (canFinalizeRespawn() && packet.position) {
      finishRespawn('MovePlayer authoritative', pos)
    }

    // reset/teleport adalah authoritative correction besar. Local velocity lama
    // tidak boleh dibawa melewati teleport dan client harus ack handled_teleport.
    if (packet.mode === 'reset' || packet.mode === 'teleport') {
      ai.verticalVelocity = 0
      ai.horizontalVelocityX = 0
      ai.horizontalVelocityZ = 0
      handledTeleportPending = true
      handledTeleportReason = `MovePlayer/${packet.mode}`
    }
  })

  thisClient.on('remove_entity', packet => {
    const uniqueId = packet.entity_id_self
    if (uniqueId === undefined) return

    for (const [key, entity] of entities) {
      if (entity.uniqueId !== undefined && String(entity.uniqueId) === String(uniqueId)) {
        entities.delete(key)
        break
      }
    }
  })

  thisClient.on('set_entity_data', packet => {
    updateSelfAirMetadata(packet)
  })

  thisClient.on('level_chunk', packet => {
    const ok = world.loadLevelChunk(packet)
    if (debug.logChunks && ok) console.log(`🧱 Chunk ${packet.x},${packet.z} loaded`)
  })

  thisClient.on('subchunk', async packet => {
    await world.loadSubChunk(packet)
  })

  thisClient.on('correct_player_move_prediction', packet => {
    if (packet.prediction_type && packet.prediction_type !== 'player') return
    movementCorrections++
    diagnostics.corrections++
    lastCorrectionAt = Date.now()
    const correctionTick = packet.tick !== undefined ? String(packet.tick) : null
    const predictedAtTick = correctionTick ? predictionHistory.get(correctionTick) : null
    const comparisonPosition = predictedAtTick?.position || pos
    const correctionError = packet.position && comparisonPosition ? distance3D(comparisonPosition, packet.position) : 0
    diagnostics.lastCorrectionError = correctionError
    diagnostics.correctionErrorSum += correctionError
    diagnostics.correctionErrorMax = Math.max(diagnostics.correctionErrorMax, correctionError)
    observeServerMovementRejection(packet.position, correctionError)

    if (correctionTick) predictionHistory.delete(correctionTick)

    if (correctionError >= debug.largeCorrectionDistance && !postRespawnSync.active) {
      console.log(`⚠️ Koreksi server besar (${correctionError.toFixed(2)} blok, ${predictedAtTick ? 'tick-aligned' : 'fallback-current'})`)
      if (debug.diagnostics) diagnosticSnapshot('large-correction', {
        correctionTick,
        serverPosition: packet.position,
        predictedPosition: roundVec(comparisonPosition),
        predictionAgeMs: predictedAtTick ? Date.now() - predictedAtTick.sentAt : null,
        tickAligned: Boolean(predictedAtTick),
        correctionError: roundNumber(correctionError, 3)
      })
    }

    if (movementCorrections <= 3 || movementCorrections % debug.correctionLogEvery === 0) {
      console.log(`↩️ Server movement correction #${movementCorrections}`, {
        tick: packet.tick !== undefined ? String(packet.tick) : '?',
        position: packet.position,
        delta: packet.delta,
        onGround: packet.on_ground
      })
    }

    if (typeof packet.on_ground === 'boolean') onGround = packet.on_ground
    if (packet.position) {
      pos = snapGroundedEyePosition(packet.position, onGround)
      movementReady = true
      recordAuthoritativeState(pos, packet.delta || null)
      notePostRespawnFeedback(pos, 'CorrectPlayerMovePrediction')
      notePostRespawnRearmFeedback(pos, packet.delta || null, 'CorrectPlayerMovePrediction', packet.on_ground)
      if (canFinalizeRespawn()) {
        finishRespawn('CorrectPlayerMovePrediction authoritative', pos)
      }
    }
    // Untuk prediction_type=player, rotation pada correction bukan authoritative
    // look rotation; pertahankan yaw/pitch lokal. Rotation dipakai untuk vehicle.

    // Correction adalah authoritative state dari server. Working Bedrock clients
    // mengakui data ini pada PlayerAuthInput berikutnya dan membuang velocity
    // prediksi lokal lama agar correction tidak langsung dilawan lagi.
    receivedServerData = true
    ai.verticalVelocity = 0
    ai.horizontalVelocityX = 0
    ai.horizontalVelocityZ = 0
  })

  thisClient.on('set_health', packet => {
    const nextHealth = Number(packet.health)
    if (!Number.isFinite(nextHealth)) return

    lastHealth = currentHealth
    currentHealth = nextHealth
    if (nextHealth < lastHealth) {
      lastDamageAt = Date.now()
      lastDamageAmount = lastHealth - nextHealth
      const nearestHostile = getNearbyHostiles(12)[0]
      lastThreatSnapshot = nearestHostile
        ? {
            at: lastDamageAt,
            name: nearestHostile.entity.username || nearestHostile.entity.entityType,
            entityType: nearestHostile.entity.entityType,
            runtimeId: nearestHostile.entity.runtimeId,
            distance: nearestHostile.distance,
            position: { ...nearestHostile.entity.position }
          }
        : null
      if (debug.logHealthChanges) {
        console.log(`❤️ HP ${lastHealth} → ${nextHealth} (-${lastDamageAmount})`, {
          nearestThreat: lastThreatSnapshot
        })
      }
    } else if (debug.logHealthChanges && nextHealth > lastHealth) {
      console.log(`💚 HP ${lastHealth} → ${nextHealth}`)
    }

    if (nextHealth <= 0) {
      markDead('set_health/0', { source: 'set_health' })
    } else if (canFinalizeRespawn()) {
      finishRespawn('SetHealth positive after handshake', lifecycle.respawnPosition || pos)
    }
  })

  thisClient.on('entity_event', packet => {
    const isSelf = runtimeId !== null && String(packet.runtime_entity_id) === String(runtimeId)
    if (!isSelf) return

    if (packet.event_id === 'death_animation' || packet.event_id === 3) {
      markDead('entity_event/death_animation', { source: 'entity_event', packet })
    } else if (packet.event_id === 'respawn' || packet.event_id === 18) {
      if (canFinalizeRespawn()) {
        finishRespawn('EntityEvent(respawn)', lifecycle.respawnPosition)
      }
    }
  })

  thisClient.on('death_info', packet => {
    lastDeathInfo = {
      cause: packet.cause || 'death_info',
      messages: Array.isArray(packet.messages) ? [...packet.messages] : [],
      at: Date.now()
    }
    if (debug.logRawDeathPackets) console.log('☠️ death_info raw:', lastDeathInfo)
    markDead(lastDeathInfo.cause, { source: 'death_info', messages: lastDeathInfo.messages })
  })

  thisClient.on('respawn', handleRespawn)

  thisClient.on('text', packet => {
    console.log(`[CHAT] ${packet.source_name || 'SERVER'}: ${packet.message || ''}`)
    if (debug.logRawDeathPackets && typeof packet.message === 'string' && packet.message.includes('death.')) {
      console.log('☠️ death text raw:', {
        type: packet.type,
        category: packet.category,
        needsTranslation: packet.needs_translation,
        message: packet.message,
        parameters: packet.parameters || []
      })
    }
  })

  thisClient.on('kick', reason => {
    thisBlackBox?.recordKick(reason)
    console.error('👢 Bot di-kick:', reason)
  })

  thisClient.on('error', err => {
    thisBlackBox?.recordError(err)
    console.error('❌ Connection error:', err?.stack || err)
  })

  thisClient.once('close', (...closeArgs) => {
    // Report harus dibuat sebelum state/timer dibersihkan agar penyebab terakhir
    // dan snapshot movement/respawn masih utuh.
    const disconnectReport = thisBlackBox?.report(disconnectStateSnapshot(thisClient), closeArgs)
    if (disconnectReport) console.error(`🔎 Disconnect classification: ${disconnectReport.classification}`)
    if (disconnectBlackBox === thisBlackBox) disconnectBlackBox = null
    if (client === thisClient) client = null
    spawned = false
    clearRuntimeTimers()
    console.log('🔴 Client ditutup')
    scheduleReconnect('koneksi terputus')
  })
}

function shutdown () {
  if (shuttingDown) return
  shuttingDown = true
  console.log('\n🛑 Menghentikan bot...')
  clearRuntimeTimers()
  if (reconnectTimer) clearTimeout(reconnectTimer)
  reconnectTimer = null

  try {
    if (client && !client._closed) {
      disconnectBlackBox?.markLocalDisconnect('Bot shutting down', 'shutdown')
      client.disconnect('Bot shutting down', true)
    }
  } catch {}

  setTimeout(() => process.exit(0), 100).unref()
}

process.on('SIGINT', shutdown)
process.on('SIGTERM', shutdown)

connectBot().catch(err => {
  console.error('❌ Startup error:', err?.stack || err)
  scheduleReconnect('startup error')
})
