'use strict'

// Strict non-destructive outbound safety layer.
// This module is intentionally independent from the AI. Even if future code
// accidentally tries to mine/place/use-on-block/attack, the packet is rejected
// immediately before bedrock-protocol can serialize/send it.

const ALWAYS_BLOCKED_PACKETS = new Set([
  // Modern Bedrock uses InventoryTransaction for placing/using items on blocks
  // and attacking entities. Blocking the whole packet is deliberately strict.
  'inventory_transaction',
  'item_stack_request',

  // Explicit world editing / command surfaces that this AFK bot never needs.
  'command_request',
  'command_block_update',
  'structure_block_update',
  'block_entity_data',
  'lectern_update',
  'agent_action'
])

const BLOCKED_PLAYER_ACTIONS = new Set([
  'start_break',
  'abort_break',
  'stop_break',
  'get_updated_block',
  'creative_player_destroy_block',
  'crack_break',
  'predict_break',
  'continue_break',
  'start_item_use_on',
  'stop_item_use_on',
  'start_spin_attack',
  'stop_spin_attack',

  // Newer Mojang naming, kept here so the guard remains fail-safe if the
  // protocol mapper changes from Prismarine's legacy labels.
  'start_destroy_block',
  'abort_destroy_block',
  'stop_destroy_block',
  'creative_destroy_block',
  'deny_destroy_block',
  'crack_block',
  'predict_destroy_block',
  'continue_destroy_block'
])

const DANGEROUS_AUTH_INPUT_FLAGS = new Set([
  'item_interact',
  'item_stack_request',
  'start_using_item',
  'start_spin_attack',
  'stop_spin_attack'
])

const SAFE_AUTH_BLOCK_ACTIONS = new Set([
  // Respawn is safe/non-destructive. `stop_sleeping` is the protocol-2193
  // compatibility token used by the current shifted minecraft-data mapper to
  // serialise wire action 6 (Respawn).
  'respawn',
  'stop_sleeping'
])

const SAFE_INTERACT_ACTIONS = new Set([
  'mouse_over_entity',
  'leave_vehicle',
  'npc_open',
  'open_inventory',
  // Numeric values of the same four actions in the current Bedrock mapper.
  '3', '4', '5', '6'
])

function normalizedAction (value) {
  if (value === undefined || value === null) return ''
  return String(value).trim().toLowerCase()
}

function hasMeaningfulPayload (value) {
  if (value === undefined || value === null || value === false) return false
  if (Array.isArray(value)) return value.length > 0
  if (typeof value === 'object') return Object.keys(value).length > 0
  return true
}

function inspectOutboundPacket (name, params = {}) {
  const packetName = String(name || '').trim().toLowerCase()

  if (ALWAYS_BLOCKED_PACKETS.has(packetName)) {
    return { allowed: false, reason: `packet ${packetName} is disabled by non-destructive mode` }
  }

  if (packetName === 'player_action') {
    // Require symbolic action names. This bot uses symbolic names and failing
    // closed on raw numeric IDs prevents mapper/version ambiguity from turning
    // a supposedly safe numeric action into block destruction.
    if (typeof params.action !== 'string') {
      return { allowed: false, reason: 'numeric/unknown player_action is fail-closed in non-destructive mode' }
    }
    const action = normalizedAction(params.action)
    if (!action) {
      return { allowed: false, reason: 'player_action without a recognised action is fail-closed' }
    }
    if (BLOCKED_PLAYER_ACTIONS.has(action)) {
      return { allowed: false, reason: `destructive player_action ${action}` }
    }
  }

  if (packetName === 'interact') {
    const action = normalizedAction(params.action_id)
    // The current 1.26.x Interact packet is no longer the normal attack/block
    // interaction path, but unknown future action IDs are rejected fail-closed.
    if (!SAFE_INTERACT_ACTIONS.has(action)) {
      return { allowed: false, reason: `unknown/non-whitelisted interact action ${action || '<empty>'}` }
    }
  }

  if (packetName === 'player_auth_input') {
    const flags = Array.isArray(params.input_data) ? params.input_data.map(normalizedAction) : []
    const dangerousFlags = flags.filter(flag => DANGEROUS_AUTH_INPUT_FLAGS.has(flag))
    if (dangerousFlags.length > 0) {
      return { allowed: false, reason: `dangerous PlayerAuthInput flags: ${dangerousFlags.join(', ')}` }
    }

    // Server-authoritative movement carries selected player actions inside the
    // optional block_action array. Permit only the non-destructive Respawn
    // action; any block manipulation remains fail-closed.
    const blockActions = Array.isArray(params.block_action) ? params.block_action : []
    if (flags.includes('block_action') || blockActions.length > 0) {
      if (!flags.includes('block_action') || blockActions.length === 0) {
        return { allowed: false, reason: 'PlayerAuthInput block_action flag/payload mismatch' }
      }
      for (const entry of blockActions) {
        const action = normalizedAction(entry?.action)
        if (!SAFE_AUTH_BLOCK_ACTIONS.has(action)) {
          return { allowed: false, reason: `destructive/unknown PlayerAuthInput block_action ${action || '<empty>'}` }
        }
        const p = entry?.position
        const face = Number(entry?.face)
        const zeroPosition = p && Number(p.x) === 0 && Number(p.y) === 0 && Number(p.z) === 0
        if (!zeroPosition || face !== 0) {
          return { allowed: false, reason: 'respawn PlayerAuthInput block_action must use zero position and face 0' }
        }
      }
    }

    for (const field of ['item_interaction_data', 'item_stack_request']) {
      if (hasMeaningfulPayload(params[field])) {
        return { allowed: false, reason: `PlayerAuthInput carries ${field}` }
      }
    }
  }

  return { allowed: true, reason: null }
}

function installNonDestructiveGuard (client, options = {}) {
  if (!client || client.__nonDestructiveGuardInstalled) return client

  const enabled = options.enabled !== false
  const logBlocked = options.logBlocked !== false
  const logger = typeof options.logger === 'function' ? options.logger : console.warn
  const stats = { blocked: 0, lastBlocked: null }

  Object.defineProperty(client, '__nonDestructiveGuardInstalled', {
    value: true,
    configurable: false,
    enumerable: false,
    writable: false
  })
  Object.defineProperty(client, '__nonDestructiveGuardStats', {
    value: stats,
    configurable: false,
    enumerable: false,
    writable: false
  })

  if (!enabled) return client

  for (const methodName of ['queue', 'write']) {
    if (typeof client[methodName] !== 'function') continue
    const original = client[methodName].bind(client)

    client[methodName] = function guardedOutbound (name, params, ...rest) {
      const decision = inspectOutboundPacket(name, params)
      if (!decision.allowed) {
        stats.blocked++
        stats.lastBlocked = {
          at: Date.now(),
          method: methodName,
          packet: String(name),
          reason: decision.reason
        }
        if (logBlocked) {
          logger(`🛑 SAFETY: outbound ${methodName}('${name}') diblokir — ${decision.reason}`)
        }
        return false
      }
      return original(name, params, ...rest)
    }
  }

  return client
}

module.exports = {
  inspectOutboundPacket,
  installNonDestructiveGuard,
  ALWAYS_BLOCKED_PACKETS,
  BLOCKED_PLAYER_ACTIONS,
  DANGEROUS_AUTH_INPUT_FLAGS,
  SAFE_AUTH_BLOCK_ACTIONS
}
