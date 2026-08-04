import { describe, expect, it } from "vitest";
import { parseSnapshot } from "./protocol";
import { formatWeaponLabel, groupVisiblePlayers, isSnapshotExpired, layerForAltitude, worldToScene, yawToMapRotation } from "./radarMath";
import type { MapMetadata, RadarPlayer, RadarSnapshot } from "./protocol";

const player = (slot: number, overrides: Partial<RadarPlayer> = {}): RadarPlayer => ({
  slot,
  name: `Player ${slot}`,
  team: slot % 2 ? 2 : 3,
  alive: true,
  health: 100,
  weapon: "ak47",
  x: -2476,
  y: 3239,
  z: 0,
  yaw: 90,
  ...overrides,
});

const snapshot: RadarSnapshot = {
  v: 1,
  type: "snapshot",
  seq: 1,
  gameTick: 2,
  serverTimeMs: 3,
  inGame: true,
  map: "de_test",
  local: player(-1),
  players: [player(0)],
};

const map: MapMetadata = {
  v: 1,
  map: "de_test",
  icon: "/api/maps/de_test/icon",
  position: { x: -2476, y: 3239 },
  scale: 4.4,
  rotate: 1,
  zoom: 1,
  layers: [
    { id: "lower", minZ: -1000, maxZ: 0, image: "/lower" },
    { id: "main", minZ: 0, maxZ: 1000, image: "/main" },
  ],
};

describe("snapshot protocol", () => {
  it("accepts the complete v1 contract", () => expect(parseSnapshot(snapshot)).toEqual(snapshot));
  it("rejects unsupported versions", () => expect(parseSnapshot({ ...snapshot, v: 2 })).toBeNull());
  it("rejects missing fields", () => {
    const { map: _map, ...missingMap } = snapshot;
    expect(parseSnapshot(missingMap)).toBeNull();
  });
  it("rejects non-finite player values", () => expect(parseSnapshot({ ...snapshot, players: [player(0, { x: Number.NaN })] })).toBeNull());
});

describe("radar contracts", () => {
  it("maps the overview origin exactly", () => expect(worldToScene(-2476, 3239, map)).toEqual({ x: -0.5, y: 0.5 }));
  it("maps Source yaw to a north-up marker", () => {
    expect(yawToMapRotation(0)).toBe(90);
    expect(yawToMapRotation(90)).toBe(0);
    expect(yawToMapRotation(180)).toBe(270);
    expect(yawToMapRotation(-90)).toBe(180);
  });
  it("formats weapon values for map labels", () => {
    expect(formatWeaponLabel("weapon_ak47")).toBe("AK47");
    expect(formatWeaponLabel("ct_knife")).toBe("CT KNIFE");
    expect(formatWeaponLabel("Weapon_None")).toBe("UNARMED");
  });
  it("uses half-open altitude ranges", () => expect(layerForAltitude(0, map)).toBe("main"));
  it("caps instances at 64", () => {
    const groups = groupVisiblePlayers(Array.from({ length: 65 }, (_, index) => player(index)), map, "main");
    expect(groups.current).toHaveLength(64);
    expect(groups.other).toHaveLength(0);
  });
  it("separates opaque and faded layers", () => {
    const groups = groupVisiblePlayers([player(0, { z: 10 }), player(1, { z: -10 })], map, "main");
    expect(groups.current.map(({ slot }) => slot)).toEqual([0]);
    expect(groups.other.map(({ slot }) => slot)).toEqual([1]);
  });
  it("expires exactly at 500 ms", () => {
    expect(isSnapshotExpired(1000, 1499)).toBe(false);
    expect(isSnapshotExpired(1000, 1500)).toBe(true);
  });
  it("removes dead players even while health is delayed", () => {
    const dead = player(7, { alive: false, health: 100, weapon: "awp" });
    const parsed = parseSnapshot({ ...snapshot, players: [dead] });
    expect(parsed?.players[0]).toMatchObject({ alive: false, health: 100, weapon: "awp" });
    expect(groupVisiblePlayers(parsed!.players, map, "main").current).toHaveLength(0);
  });
  it("removes zero-health players even if the alive flag is stale", () => {
    const groups = groupVisiblePlayers([player(8, { alive: true, health: 0 })], map, "main");
    expect(groups.current).toHaveLength(0);
    expect(groups.other).toHaveLength(0);
  });
});
