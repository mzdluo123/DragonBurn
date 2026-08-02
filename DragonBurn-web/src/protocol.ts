export interface RadarPlayer {
  slot: number;
  name: string;
  team: number;
  alive: boolean;
  health: number;
  weapon: string;
  x: number;
  y: number;
  z: number;
  yaw: number;
}

export interface RadarSnapshot {
  v: 1;
  type: "snapshot";
  seq: number;
  gameTick: number;
  serverTimeMs: number;
  inGame: boolean;
  map: string;
  local: RadarPlayer | null;
  players: RadarPlayer[];
}

export interface MapLayer {
  id: string;
  minZ: number;
  maxZ: number;
  image: string;
}

export interface MapMetadata {
  v: 1;
  map: string;
  icon: string;
  position: { x: number; y: number };
  scale: number;
  rotate: number;
  zoom: number;
  layers: MapLayer[];
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isFiniteNumber = (value: unknown): value is number =>
  typeof value === "number" && Number.isFinite(value);

function parsePlayer(value: unknown): RadarPlayer | null {
  if (!isRecord(value)) return null;
  const { slot, name, team, alive, health, weapon, x, y, z, yaw } = value;
  if (
    !Number.isInteger(slot) ||
    typeof name !== "string" ||
    !Number.isInteger(team) ||
    typeof alive !== "boolean" ||
    !Number.isInteger(health) ||
    typeof weapon !== "string" ||
    !isFiniteNumber(x) ||
    !isFiniteNumber(y) ||
    !isFiniteNumber(z) ||
    !isFiniteNumber(yaw)
  ) return null;
  return { slot: slot as number, name, team: team as number, alive, health: health as number, weapon, x, y, z, yaw };
}

export function parseSnapshot(value: unknown): RadarSnapshot | null {
  if (!isRecord(value) || value.v !== 1 || value.type !== "snapshot") return null;
  if (
    !Number.isSafeInteger(value.seq) ||
    !Number.isSafeInteger(value.gameTick) ||
    !Number.isSafeInteger(value.serverTimeMs) ||
    typeof value.inGame !== "boolean" ||
    typeof value.map !== "string" ||
    !Array.isArray(value.players) ||
    value.players.length > 64
  ) return null;

  const local = value.local === null ? null : parsePlayer(value.local);
  if (value.local !== null && local === null) return null;
  const players: RadarPlayer[] = [];
  for (const item of value.players) {
    const player = parsePlayer(item);
    if (!player) return null;
    players.push(player);
  }

  if (!value.inGame && (local !== null || players.length !== 0)) return null;
  return {
    v: 1,
    type: "snapshot",
    seq: value.seq as number,
    gameTick: value.gameTick as number,
    serverTimeMs: value.serverTimeMs as number,
    inGame: value.inGame,
    map: value.map,
    local,
    players,
  };
}

export function parseSnapshotText(text: string): RadarSnapshot | null {
  try {
    return parseSnapshot(JSON.parse(text));
  } catch {
    return null;
  }
}

export function parseMapMetadata(value: unknown): MapMetadata | null {
  if (!isRecord(value) || value.v !== 1 || typeof value.map !== "string" || typeof value.icon !== "string") return null;
  if (!isRecord(value.position) || !isFiniteNumber(value.position.x) || !isFiniteNumber(value.position.y)) return null;
  if (!isFiniteNumber(value.scale) || value.scale <= 0 || !isFiniteNumber(value.rotate) || !isFiniteNumber(value.zoom)) return null;
  if (!Array.isArray(value.layers) || value.layers.length === 0 || value.layers.length > 8) return null;
  const layers: MapLayer[] = [];
  for (const layer of value.layers) {
    if (!isRecord(layer) || typeof layer.id !== "string" || typeof layer.image !== "string" || !isFiniteNumber(layer.minZ) || !isFiniteNumber(layer.maxZ) || layer.minZ >= layer.maxZ) return null;
    layers.push({ id: layer.id, image: layer.image, minZ: layer.minZ, maxZ: layer.maxZ });
  }
  return {
    v: 1,
    map: value.map,
    icon: value.icon,
    position: { x: value.position.x, y: value.position.y },
    scale: value.scale,
    rotate: value.rotate,
    zoom: value.zoom,
    layers,
  };
}
