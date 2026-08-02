import { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { groupVisiblePlayers, interpolatePlayer, worldToScene } from "./radarMath";
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
  meshes: [THREE.InstancedMesh, THREE.InstancedMesh, THREE.InstancedMesh, THREE.InstancedMesh];
}

const playerColor = (team: number) => new THREE.Color(team === 2 ? 0xe8a23a : team === 3 ? 0x55a7e8 : 0xd8d8d8);

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

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x080b10);
    const camera = new THREE.OrthographicCamera(-0.5, 0.5, 0.5, -0.5, 0, 10);
    camera.position.z = 5;

    const planeGeometry = new THREE.PlaneGeometry(1, 1);
    const mapMaterial = new THREE.MeshBasicMaterial({ color: 0xffffff });
    const plane = new THREE.Mesh(planeGeometry, mapMaterial);
    plane.position.z = 0;
    scene.add(plane);

    const circleGeometry = new THREE.CircleGeometry(0.012, 16);
    const triangleGeometry = new THREE.BufferGeometry();
    triangleGeometry.setAttribute("position", new THREE.Float32BufferAttribute([
      0, 0.024, 0,
      -0.008, 0.008, 0,
      0.008, 0.008, 0,
    ], 3));
    const currentCircleMaterial = new THREE.MeshBasicMaterial({ vertexColors: true });
    const currentArrowMaterial = new THREE.MeshBasicMaterial({ vertexColors: true });
    const otherCircleMaterial = new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.25, depthWrite: false });
    const otherArrowMaterial = new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.25, depthWrite: false });

    const currentCircles = new THREE.InstancedMesh(circleGeometry, currentCircleMaterial, 64);
    const currentArrows = new THREE.InstancedMesh(triangleGeometry, currentArrowMaterial, 64);
    const otherCircles = new THREE.InstancedMesh(circleGeometry, otherCircleMaterial, 64);
    const otherArrows = new THREE.InstancedMesh(triangleGeometry, otherArrowMaterial, 64);
    const meshes: SceneResources["meshes"] = [currentCircles, currentArrows, otherCircles, otherArrows];
    meshes.forEach((mesh, index) => {
      mesh.count = 0;
      mesh.position.z = index < 2 ? 2 : 1;
      mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
      scene.add(mesh);
    });

    const resources: SceneResources = {
      renderer,
      scene,
      camera,
      mapMaterial,
      geometries: [planeGeometry, circleGeometry, triangleGeometry],
      materials: [mapMaterial, currentCircleMaterial, currentArrowMaterial, otherCircleMaterial, otherArrowMaterial],
      meshes,
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

    const matrix = new THREE.Matrix4();
    const position = new THREE.Vector3();
    const rotation = new THREE.Quaternion();
    const scale = new THREE.Vector3(1, 1, 1);
    const zAxis = new THREE.Vector3(0, 0, 1);

    const updateGroup = (players: RadarPlayer[], circles: THREE.InstancedMesh, arrows: THREE.InstancedMesh, metadata: MapMetadata) => {
      circles.count = players.length;
      arrows.count = players.length;
      players.forEach((player, index) => {
        const point = worldToScene(player.x, player.y, metadata);
        position.set(point.x, point.y, 0);
        rotation.identity();
        matrix.compose(position, rotation, scale);
        circles.setMatrixAt(index, matrix);
        circles.setColorAt(index, playerColor(player.team));
        rotation.setFromAxisAngle(zAxis, THREE.MathUtils.degToRad(-player.yaw));
        matrix.compose(position, rotation, scale);
        arrows.setMatrixAt(index, matrix);
        arrows.setColorAt(index, playerColor(player.team));
      });
      circles.instanceMatrix.needsUpdate = true;
      arrows.instanceMatrix.needsUpdate = true;
      if (circles.instanceColor) circles.instanceColor.needsUpdate = true;
      if (arrows.instanceColor) arrows.instanceColor.needsUpdate = true;
    };

    let animationFrame = 0;
    const animate = () => {
      const metadata = mapRef.current;
      const selected = layerRef.current;
      const { previous, current, receivedAt } = snapshotsRef.current;
      if (metadata && selected && current) {
        const duration = previous ? Math.min(Math.max(current.serverTimeMs - previous.serverTimeMs, 1), 250) : 1;
        const alpha = Math.min(Math.max((performance.now() - receivedAt) / duration, 0), 1);
        const previousBySlot = new Map(previous?.players.map((player) => [player.slot, player]) ?? []);
        const players = current.players.map((player) => interpolatePlayer(previousBySlot.get(player.slot), player, alpha));
        if (current.local) {
          players.push(interpolatePlayer(previous?.local ?? undefined, current.local, alpha));
        }
        const groups = groupVisiblePlayers(players, metadata, selected);
        updateGroup(groups.current, currentCircles, currentArrows, metadata);
        updateGroup(groups.other, otherCircles, otherArrows, metadata);
      } else {
        meshes.forEach((mesh) => { mesh.count = 0; });
      }
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
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, []);

  return <div className="radar-scene" ref={hostRef} aria-label="Map radar" />;
}
