import { useEffect, useMemo, useState } from "react";
import RadarScene from "./RadarScene";
import { layerForAltitude } from "./radarMath";
import { parseMapMetadata, type MapMetadata } from "./protocol";
import { useRadarSocket } from "./useRadarSocket";

type MapStatus = "idle" | "loading" | "ready" | "stale" | "unavailable" | "error";

export default function App() {
  const { snapshot, status, protocolError } = useRadarSocket();
  const [metadata, setMetadata] = useState<MapMetadata | null>(null);
  const [mapStatus, setMapStatus] = useState<MapStatus>("idle");
  const [selectedLayer, setSelectedLayer] = useState<string | null>(null);
  const [manualLayer, setManualLayer] = useState(false);
  const [renderError, setRenderError] = useState<string | null>(null);
  const [mapImageError, setMapImageError] = useState<string | null>(null);

  const mapName = snapshot?.map ?? "";
  useEffect(() => {
    setMetadata(null);
    setSelectedLayer(null);
    setManualLayer(false);
    setMapImageError(null);
    if (!mapName) {
      setMapStatus("idle");
      return;
    }

    const controller = new AbortController();
    setMapStatus("loading");
    fetch(`/api/maps/${encodeURIComponent(mapName)}`, { cache: "no-store", signal: controller.signal })
      .then(async (response) => {
        if (response.status === 404) {
          setMapStatus("unavailable");
          return;
        }
        if (!response.ok) throw new Error(`Map metadata HTTP ${response.status}`);
        const parsed = parseMapMetadata(await response.json());
        if (!parsed) throw new Error("Invalid map metadata");
        setMetadata(parsed);
        setMapStatus(response.headers.get("X-WebRadar-Stale") === "1" ? "stale" : "ready");
      })
      .catch((error: unknown) => {
        if ((error as { name?: string }).name !== "AbortError") setMapStatus("error");
      });
    return () => controller.abort();
  }, [mapName]);

  useEffect(() => {
    if (!metadata || manualLayer) return;
    const localLayer = snapshot?.local ? layerForAltitude(snapshot.local.z, metadata) : null;
    setSelectedLayer(localLayer ?? metadata.layers[0]?.id ?? null);
  }, [metadata, snapshot, manualLayer]);

  const players = useMemo(() => {
    if (!snapshot) return [];
    return snapshot.local ? [{ ...snapshot.local, name: `${snapshot.local.name} (you)` }, ...snapshot.players] : snapshot.players;
  }, [snapshot]);

  const mapMessage = mapStatus === "loading" ? "Downloading map resources…"
    : mapStatus === "unavailable" ? "Map resource unavailable"
    : mapStatus === "stale" ? "Using the last verified cached map manifest"
    : mapStatus === "error" ? "Map metadata could not be loaded"
    : null;

  return (
    <main className="app-shell">
      <header className="topbar">
        <div>
          <h1>VoidSpectre Radar</h1>
          <p>{snapshot ? snapshot.map : "Waiting for game state"}</p>
        </div>
        <span className={`status status-${status}`}>{status}</span>
      </header>

      <section className="content">
        <div className="map-panel">
          <RadarScene
            snapshot={snapshot}
            map={metadata}
            selectedLayer={selectedLayer}
            onRenderError={setRenderError}
            onMapError={setMapImageError}
          />
          <div className="map-controls">
            <label htmlFor="floor">Floor</label>
            <select
              id="floor"
              value={selectedLayer ?? ""}
              disabled={!metadata}
              onChange={(event) => {
                setManualLayer(true);
                setSelectedLayer(event.target.value);
              }}
            >
              {metadata?.layers.map((layer) => <option key={layer.id} value={layer.id}>{layer.id}</option>)}
            </select>
            {manualLayer && <button type="button" onClick={() => setManualLayer(false)}>Follow player</button>}
          </div>
          <div className="messages" aria-live="polite">
            {protocolError && <p className="error">{protocolError}</p>}
            {renderError && <p className="error">{renderError}</p>}
            {mapImageError && <p className="error">{mapImageError}</p>}
            {mapMessage && <p>{mapMessage}</p>}
          </div>
        </div>

        <aside className="players-panel">
          <h2>Players <span>{players.length}</span></h2>
          <div className="player-list">
            {players.map((player) => (
              <article className={`player-row team-${player.team} ${player.alive ? "" : "dead"}`} key={`${player.slot}-${player.name}`}>
                <div className="player-title">
                  <strong>{player.name || "Unknown"}</strong>
                  <span>{player.alive ? `${player.health} HP` : "Dead"}</span>
                </div>
                <div className="player-detail">
                  <span>{player.weapon || "unarmed"}</span>
                  <span>{Math.round(player.x)}, {Math.round(player.y)}, {Math.round(player.z)}</span>
                </div>
              </article>
            ))}
            {!players.length && <p className="empty">No current player snapshot</p>}
          </div>
        </aside>
      </section>
    </main>
  );
}
