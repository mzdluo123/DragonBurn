import { useEffect, useRef, useState } from "react";
import { isSnapshotExpired } from "./radarMath";
import { parseSnapshotText, type RadarSnapshot } from "./protocol";

export type SocketStatus = "connecting" | "connected" | "disconnected";

export interface RadarSocketState {
  snapshot: RadarSnapshot | null;
  status: SocketStatus;
  protocolError: string | null;
}

const reconnectDelays = [250, 500, 1000, 2000, 5000] as const;

export function useRadarSocket(): RadarSocketState {
  const [snapshot, setSnapshot] = useState<RadarSnapshot | null>(null);
  const [status, setStatus] = useState<SocketStatus>("connecting");
  const [protocolError, setProtocolError] = useState<string | null>(null);
  const lastSequence = useRef<number | null>(null);
  const lastMessageAt = useRef(0);

  useEffect(() => {
    let socket: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let reconnectAttempt = 0;
    let disposed = false;

    const connect = () => {
      if (disposed) return;
      setStatus("connecting");
      const scheme = window.location.protocol === "https:" ? "wss:" : "ws:";
      socket = new WebSocket(`${scheme}//${window.location.host}/ws`);

      socket.onopen = () => {
        reconnectAttempt = 0;
        setStatus("connected");
      };
      socket.onmessage = (event) => {
        if (typeof event.data !== "string") {
          setProtocolError("Received a non-text radar message");
          return;
        }
        const next = parseSnapshotText(event.data);
        if (!next) {
          setProtocolError("Received an invalid or unsupported radar protocol message");
          return;
        }
        setProtocolError(null);
        lastMessageAt.current = performance.now();
        if (lastSequence.current !== next.seq) {
          lastSequence.current = next.seq;
          setSnapshot(next.inGame ? next : null);
        }
      };
      socket.onclose = () => {
        if (disposed) return;
        setStatus("disconnected");
        setSnapshot(null);
        lastSequence.current = null;
        lastMessageAt.current = 0;
        const delay = reconnectDelays[Math.min(reconnectAttempt, reconnectDelays.length - 1)];
        reconnectAttempt += 1;
        reconnectTimer = window.setTimeout(connect, delay);
      };
      socket.onerror = () => socket?.close();
    };

    connect();
    const expiryTimer = window.setInterval(() => {
      if (lastMessageAt.current !== 0 && isSnapshotExpired(lastMessageAt.current, performance.now())) {
        lastMessageAt.current = 0;
        setSnapshot(null);
      }
    }, 50);

    return () => {
      disposed = true;
      window.clearInterval(expiryTimer);
      if (reconnectTimer !== null) window.clearTimeout(reconnectTimer);
      lastSequence.current = null;
      lastMessageAt.current = 0;
      socket?.close();
    };
  }, []);

  return { snapshot, status, protocolError };
}
