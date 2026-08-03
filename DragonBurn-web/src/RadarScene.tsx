import { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { formatWeaponLabel, groupVisiblePlayers, interpolatePlayer, worldToScene, yawToMapRotation } from "./radarMath";
import type { MapMetadata, RadarPlayer, RadarSnapshot } from "./protocol";

interface RadarSceneProps {
  snapshot: RadarSnapshot | null;
  map: MapMetadata | null;
  selectedLayer: string | null;
  onRenderError: (message: string | null) => void;
  onMapError: (message: string | null) => void;
}

interface SceneResources {
  renderer: THREE.WebGLRenderer;
  scene: THREE.Scene;
  camera: THREE.OrthographicCamera;
  mapMaterial: THREE.MeshBasicMaterial;
  geometries: THREE.BufferGeometry[];
  materials: THREE.Material[];
}

interface PlayerMarker {
  root: HTMLDivElement;
  direction: HTMLDivElement;
  name: HTMLSpanElement;
  weapon: HTMLSpanElement;
  health: HTMLSpanElement;
  lastFrame: number;
}

function createPlayerMarker(layer: HTMLDivElement): PlayerMarker {
  const root = document.createElement("div");
  root.className = "radar-player";

  const symbol = document.createElement("div");
  symbol.className = "radar-player-symbol";
  const direction = document.createElement("div");
  direction.className = "radar-player-direction";
  symbol.appendChild(direction);

  const label = document.createElement("div");
  label.className = "radar-player-label";
  const name = document.createElement("span");
  name.className = "radar-player-name";
  const details = document.createElement("span");
  details.className = "radar-player-details";
  const weapon = document.createElement("span");
  weapon.className = "radar-player-weapon";
  const health = document.createElement("span");
  health.className = "radar-player-health";
  details.append(weapon, health);
  label.append(name, details);
  root.append(symbol, label);
  layer.appendChild(root);

  return { root, direction, name, weapon, health, lastFrame: 0 };
}

export default function RadarScene({ snapshot, map, selectedLayer, onRenderError, onMapError }: RadarSceneProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const resourcesRef = useRef<SceneResources | null>(null);
  const mapRef = useRef<MapMetadata | null>(map);
  const layerRef = useRef<string | null>(selectedLayer);
  const snapshotsRef = useRef<{ previous: RadarSnapshot | null; current: RadarSnapshot | null; receivedAt: number }>({
    previous: null,
    current: null,
    receivedAt: performance.now(),
  });
  const callbacksRef = useRef({ onRenderError, onMapError });
  const [rendererReady, setRendererReady] = useState(false);
  callbacksRef.current = { onRenderError, onMapError };

  useEffect(() => {
    const state = snapshotsRef.current;
    if (snapshot?.seq !== state.current?.seq) {
      snapshotsRef.current = { previous: state.current, current: snapshot, receivedAt: performance.now() };
    }
  }, [snapshot]);

  useEffect(() => {
    mapRef.current = map;
    layerRef.current = selectedLayer;
    const resources = resourcesRef.current;
    if (!resources) return;

    resources.mapMaterial.map?.dispose();
    resources.mapMaterial.map = null;
    resources.mapMaterial.needsUpdate = true;
    if (!map || !selectedLayer) return;
    const layer = map.layers.find(({ id }) => id === selectedLayer);
    if (!layer) return;

    let active = true;
    callbacksRef.current.onMapError(null);
    new THREE.TextureLoader().load(
      layer.image,
      (texture) => {
        if (!active) {
          texture.dispose();
          return;
        }
        texture.colorSpace = THREE.SRGBColorSpace;
        resources.mapMaterial.map = texture;
        resources.mapMaterial.needsUpdate = true;
      },
      undefined,
      () => {
        if (active) callbacksRef.current.onMapError("Map image could not be loaded");
      },
    );
    return () => { active = false; };
  }, [map, selectedLayer, rendererReady]);

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;

    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: "high-performance" });
    } catch {
      callbacksRef.current.onRenderError("WebGL 2 is required");
      return;
    }
    if (!renderer.capabilities.isWebGL2) {
      renderer.dispose();
      callbacksRef.current.onRenderError("WebGL 2 is required");
      return;
    }
    callbacksRef.current.onRenderError(null);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    host.appendChild(renderer.domElement);

    const markerLayer = document.createElement("div");
    markerLayer.className = "radar-players";
    markerLayer.setAttribute("aria-label", "Players on map");
    host.appendChild(markerLayer);
    const markers = new Map<number, PlayerMarker>();

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x080b10);
    const camera = new THREE.OrthographicCamera(-0.5, 0.5, 0.5, -0.5, 0, 10);
    camera.position.z = 5;

    const planeGeometry = new THREE.PlaneGeometry(1, 1);
    const mapMaterial = new THREE.MeshBasicMaterial({ color: 0xffffff });
    const plane = new THREE.Mesh(planeGeometry, mapMaterial);
    scene.add(plane);

    const resources: SceneResources = {
      renderer,
      scene,
      camera,
      mapMaterial,
      geometries: [planeGeometry],
      materials: [mapMaterial],
    };
    resourcesRef.current = resources;
    setRendererReady(true);

    const resize = () => {
      const width = Math.max(host.clientWidth, 1);
      const height = Math.max(host.clientHeight, 1);
      renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
      renderer.setSize(width, height, false);
    };
    const resizeObserver = new ResizeObserver(resize);
    resizeObserver.observe(host);
    resize();

    const updateMarker = (player: RadarPlayer, otherFloor: boolean, metadata: MapMetadata, frame: number) => {
      let marker = markers.get(player.slot);
      if (!marker) {
        marker = createPlayerMarker(markerLayer);
        markers.set(player.slot, marker);
      }

      const point = worldToScene(player.x, player.y, metadata);
      marker.root.style.left = `${(point.x + 0.5) * 100}%`;
      marker.root.style.top = `${(0.5 - point.y) * 100}%`;
      marker.direction.style.transform = `rotate(${yawToMapRotation(player.yaw)}deg)`;

      const className = `radar-player team-${player.team}${otherFloor ? " other-floor" : ""}${player.slot === -1 ? " local-player" : ""}`;
      if (marker.root.className !== className) marker.root.className = className;
      const displayName = player.name || "Unknown";
      if (marker.name.textContent !== displayName) marker.name.textContent = displayName;
      const weapon = formatWeaponLabel(player.weapon);
      if (marker.weapon.textContent !== weapon) marker.weapon.textContent = weapon;
      const health = `${player.health} HP`;
      if (marker.health.textContent !== health) marker.health.textContent = health;
      marker.health.className = `radar-player-health${player.health <= 25 ? " low" : ""}`;
      marker.root.hidden = false;
      marker.lastFrame = frame;
    };

    let animationFrame = 0;
    let markerFrame = 0;
    const animate = () => {
      markerFrame += 1;
      const metadata = mapRef.current;
      const selected = layerRef.current;
      const { previous, current, receivedAt } = snapshotsRef.current;
      if (metadata && selected && current) {
        const duration = previous ? Math.min(Math.max(current.serverTimeMs - previous.serverTimeMs, 1), 250) : 1;
        const alpha = Math.min(Math.max((performance.now() - receivedAt) / duration, 0), 1);
        const previousBySlot = new Map(previous?.players.map((player) => [player.slot, player]) ?? []);
        const players = current.players.map((player) => interpolatePlayer(previousBySlot.get(player.slot), player, alpha));
        if (current.local) players.push(interpolatePlayer(previous?.local ?? undefined, current.local, alpha));

        const groups = groupVisiblePlayers(players, metadata, selected);
        groups.current.forEach((player) => updateMarker(player, false, metadata, markerFrame));
        groups.other.forEach((player) => updateMarker(player, true, metadata, markerFrame));
      }
      markers.forEach((marker) => {
        if (marker.lastFrame !== markerFrame) marker.root.hidden = true;
      });
      renderer.render(scene, camera);
      animationFrame = requestAnimationFrame(animate);
    };
    animationFrame = requestAnimationFrame(animate);

    return () => {
      cancelAnimationFrame(animationFrame);
      resizeObserver.disconnect();
      resourcesRef.current = null;
      mapMaterial.map?.dispose();
      resources.geometries.forEach((geometry) => geometry.dispose());
      resources.materials.forEach((material) => material.dispose());
      markers.clear();
      markerLayer.remove();
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, []);

  return <div className="radar-scene" ref={hostRef} aria-label="Map radar" />;
}
