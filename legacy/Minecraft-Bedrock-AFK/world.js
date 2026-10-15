const REGISTRY_VERSION = 'bedrock_1.26.51'

function normalizeBlockName (name) {
  if (!name) return name
  return String(name).replace(/^minecraft:/, '')
}

class World {
  constructor (dependencies = {}) {
    // Defaults remain the installed engine. Injection allows cache/decode
    // fixtures to avoid loading the bot, protocol, registry or auth runtime.
    const createRegistry = dependencies.createRegistry || require('prismarine-registry')
    const createChunkColumn = dependencies.createChunkColumn || require('prismarine-chunk')
    this.Vec3 = dependencies.Vec3 || require('vec3')
    const registry = createRegistry(REGISTRY_VERSION)
    this.registry = registry

    // prismarine-chunk 1.41 belum punya decoder khusus 1.26.
    // Decoder 1.21 masih dipakai sebagai compatibility layer untuk chunk modern.
    const chunkRegistry = Object.create(registry)
    if (chunkRegistry.version) {
      chunkRegistry.version = { ...chunkRegistry.version, majorVersion: '1.21' }
    }

    this.ChunkColumn = createChunkColumn(chunkRegistry)
    this.columns = new Map()
    this.blockOverrides = new Map()
    this.loadedChunks = 0
  }

  chunkKey (x, z) { return `${x},${z}` }
  blockKey (x, y, z) { return `${x},${y},${z}` }

  loadLevelChunk (packet) {
    try {
      const cx = packet.x
      const cz = packet.z
      if (packet.sub_chunk_count < 0) return false
      const column = new this.ChunkColumn({ x: cx, z: cz })
      column.networkDecodeNoCache(packet.payload, packet.sub_chunk_count)
      // A failed replacement must not discard an already decoded column.
      this.columns.set(this.chunkKey(cx, cz), column)
      this.loadedChunks++
      return true
    } catch (err) {
      console.error(`❌ Gagal decode chunk ${packet.x},${packet.z}:`, err.message)
      return false
    }
  }

  async loadSubChunk (packet) {
    try {
      for (const entry of packet.entries || []) {
        if (entry.result && entry.result !== 'success') continue

        const cx = packet.origin.x + entry.dx
        const cz = packet.origin.z + entry.dz
        const cy = packet.origin.y + entry.dy
        const column = this.columns.get(this.chunkKey(cx, cz))
        if (!column) continue

        await column.networkDecodeSubChunkNoCache(cy, entry.payload)
      }
    } catch (err) {
      console.error('❌ Gagal decode subchunk:', err.message)
    }
  }

  _rawBlockAt (x, y, z) {
    x = Math.floor(x)
    y = Math.floor(y)
    z = Math.floor(z)

    const cx = Math.floor(x / 16)
    const cz = Math.floor(z / 16)
    const column = this.columns.get(this.chunkKey(cx, cz))
    if (!column) return null

    try {
      const localX = ((x % 16) + 16) % 16
      const localZ = ((z % 16) + 16) % 16
      return column.getBlock(new this.Vec3(localX, y, localZ)) || null
    } catch {
      return null
    }
  }

  blockAt (x, y, z) {
    x = Math.floor(x)
    y = Math.floor(y)
    z = Math.floor(z)

    const override = this.blockOverrides.get(this.blockKey(x, y, z))
    if (override) return normalizeBlockName(override)

    const block = this._rawBlockAt(x, y, z)
    return normalizeBlockName(block?.name || null)
  }

  blockInfoAt (x, y, z) {
    x = Math.floor(x)
    y = Math.floor(y)
    z = Math.floor(z)

    const override = this.blockOverrides.get(this.blockKey(x, y, z))
    if (override) {
      const name = normalizeBlockName(override)
      return { name, superimposed: null, waterlogged: this.isWaterName(name), boundingBox: null }
    }

    const block = this._rawBlockAt(x, y, z)
    if (!block) return null

    return {
      name: normalizeBlockName(block.name),
      superimposed: normalizeBlockName(block.superimposed?.name || null),
      waterlogged: Boolean(block.computedStates?.waterlogged),
      boundingBox: block.boundingBox,
      shapes: Array.isArray(block.shapes) ? block.shapes.length : null
    }
  }

  isUnknown (x, y, z) { return this.blockAt(x, y, z) === null }

  isAirName (name) {
    name = normalizeBlockName(name)
    return name === 'air' || name === 'cave_air' || name === 'void_air'
  }

  isLiquidName (name) {
    name = normalizeBlockName(name)
    return name === 'water' || name === 'flowing_water' || name === 'lava' || name === 'flowing_lava'
  }

  isWaterName (name) {
    name = normalizeBlockName(name)
    return name === 'water' || name === 'flowing_water'
  }

  isAir (x, y, z) {
    const block = this.blockAt(x, y, z)
    return block !== null && this.isAirName(block)
  }

  isWater (x, y, z) {
    const info = this.blockInfoAt(x, y, z)
    if (!info) return false
    return this.isWaterName(info.name) || this.isWaterName(info.superimposed) || info.waterlogged
  }

  isSolid (x, y, z) {
    const info = this.blockInfoAt(x, y, z)
    if (!info) return false
    if (this.isAirName(info.name) || this.isLiquidName(info.name)) return false

    // Bedrock chunks can have a solid/plant primary layer plus water in a
    // superimposed layer. Use the actual collision description instead of
    // assuming every non-air block is a full cube (kelp/seagrass are not).
    if (info.boundingBox === 'empty') return false
    if (info.shapes === 0) return false
    return true
  }

  setBlockOverride (x, y, z, name) {
    this.blockOverrides.set(
      this.blockKey(Math.floor(x), Math.floor(y), Math.floor(z)),
      normalizeBlockName(name)
    )
  }

  clearFarChunks (botPos, radius = 8) {
    if (!botPos) return
    const bx = Math.floor(botPos.x / 16)
    const bz = Math.floor(botPos.z / 16)

    for (const key of this.columns.keys()) {
      const [cx, cz] = key.split(',').map(Number)
      if (Math.max(Math.abs(cx - bx), Math.abs(cz - bz)) > radius) {
        this.columns.delete(key)
      }
    }
  }

  stats () {
    return { chunks: this.columns.size, blocks: this.blockOverrides.size }
  }
}

module.exports = World
module.exports.normalizeBlockName = normalizeBlockName
