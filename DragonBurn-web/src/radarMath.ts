import type { MapMetadata, RadarPlayer } from "./protocol";

export const MAX_RADAR_PLAYERS = 64;
export const SNAPSHOT_TTL_MS = 500;

export function yawToMapRotation(yaw: number): number {
  const rotation = (90 - yaw) % 360;
  return rotation < 0 ? rotation + 360 : rotation;
}

export function formatWeaponLabel(weapon: string): string {
  const normalized = weapon.trim().replace(/^weapon_/i, "");
  if (!normalized || normalized.toLowerCase() === "none") return "UNARMED";
  return normalized.replaceAll("_", " ").toUpperCase();
}

export function worldToScene(x: number, y: number, map: MapMetadata): { x: number; y: number } {
  const fx = (x - map.position.x) / map.scale / 1024;
  const fy = (map.position.y - y) / map.scale / 1024;
  return { x: fx - 0.5, y: 0.5 - fy };
}

export function layerForAltitude(z: number, map: MapMetadata): string | null {
  return map.layers.find((layer) => z >= layer.minZ && z < layer.maxZ)?.id ?? null;
}

export function isSnapshotExpired(lastSequenceAt: number, now: number): boolean {
  return now - lastSequenceAt >= SNAPSHOT_TTL_MS;
}

export function interpolateAngle(from: number, to: number, alpha: number): number {
  const delta = ((to - from + 540) % 360) - 180;
  return from + delta * alpha;
}

export function interpolatePlayer(previous: RadarPlayer | undefined, current: RadarPlayer, alpha: number): RadarPlayer {
  if (!previous) return current;
  return {
    ...current,
    x: previous.x + (current.x - previous.x) * alpha,
    y: previous.y + (current.y - previous.y) * alpha,
    z: previous.z + (current.z - previous.z) * alpha,
    yaw: interpolateAngle(previous.yaw, current.yaw, alpha),
  };
}

export interface PlayerGroups {
  current: RadarPlayer[];
  other: RadarPlayer[];
}

export function groupVisiblePlayers(players: readonly RadarPlayer[], map: MapMetadata, selectedLayer: string): PlayerGroups {
  const result: PlayerGroups = { current: [], other: [] };
  for (const player of players) {
    if (!player.alive || result.current.length + result.other.length >= MAX_RADAR_PLAYERS) continue;
    const target = layerForAltitude(player.z, map) === selectedLayer ? result.current : result.other;
    target.push(player);
  }
  return result;
}
