#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { access, readFile } from "node:fs/promises";
import { createServer } from "node:http";
import { once } from "node:events";
import { resolve } from "node:path";
import { createInterface } from "node:readline";
import { setTimeout as sleep } from "node:timers/promises";

const [recordingArgument, portArgument = "16668", speedArgument = "1", mapRootArgument] = process.argv.slice(2);

if (!recordingArgument) {
  console.error("用法：./replay-ws.mjs <录制文件.jsonl> [端口=16668] [倍速=1]");
  process.exit(1);
}

const recording = resolve(recordingArgument);
const port = Number(portArgument);
const speed = Number(speedArgument);
const mapRoot = resolve(mapRootArgument ?? "DragonBurn-usermode/Resources/WebRadarMaps");

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  console.error(`无效端口：${portArgument}`);
  process.exit(1);
}
if (!Number.isFinite(speed) || speed <= 0) {
  console.error(`无效倍速：${speedArgument}`);
  process.exit(1);
}

try {
  await access(recording);
} catch {
  console.error(`无法读取录制文件：${recording}`);
  process.exit(1);
}

function encodeFrame(opcode, payload = Buffer.alloc(0)) {
  const body = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  let header;

  if (body.length < 126) {
    header = Buffer.from([0x80 | opcode, body.length]);
  } else if (body.length <= 0xffff) {
    header = Buffer.allocUnsafe(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(body.length, 2);
  } else {
    header = Buffer.allocUnsafe(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(body.length), 2);
  }

  return Buffer.concat([header, body], header.length + body.length);
}

function closeFrame(code, reason) {
  const reasonBytes = Buffer.from(reason).subarray(0, 123);
  const payload = Buffer.allocUnsafe(2 + reasonBytes.length);
  payload.writeUInt16BE(code, 0);
  reasonBytes.copy(payload, 2);
  return encodeFrame(0x8, payload);
}

async function sendFrame(socket, opcode, payload) {
  if (socket.destroyed || socket.writableEnded) throw new Error("客户端已断开");
  if (!socket.write(encodeFrame(opcode, payload))) await once(socket, "drain");
}

function handleClientFrames(socket) {
  let buffered = Buffer.alloc(0);

  socket.on("data", (chunk) => {
    buffered = Buffer.concat([buffered, chunk], buffered.length + chunk.length);

    while (buffered.length >= 2) {
      const opcode = buffered[0] & 0x0f;
      const masked = (buffered[1] & 0x80) !== 0;
      let length = buffered[1] & 0x7f;
      let offset = 2;

      if (length === 126) {
        if (buffered.length < 4) return;
        length = buffered.readUInt16BE(2);
        offset = 4;
      } else if (length === 127) {
        if (buffered.length < 10) return;
        const largeLength = buffered.readBigUInt64BE(2);
        if (largeLength > BigInt(Number.MAX_SAFE_INTEGER)) {
          socket.end(closeFrame(1009, "frame too large"));
          return;
        }
        length = Number(largeLength);
        offset = 10;
      }

      const maskLength = masked ? 4 : 0;
      if (buffered.length < offset + maskLength + length) return;
      const mask = masked ? buffered.subarray(offset, offset + 4) : null;
      offset += maskLength;
      const payload = Buffer.from(buffered.subarray(offset, offset + length));
      buffered = buffered.subarray(offset + length);

      if (mask) {
        for (let index = 0; index < payload.length; index++) {
          payload[index] ^= mask[index % 4];
        }
      }

      if (opcode === 0x8) {
        socket.end(encodeFrame(0x8, payload));
        return;
      }
      if (opcode === 0x9) socket.write(encodeFrame(0x0a, payload));
    }
  });
}

async function replay(socket, signal) {
  const input = createReadStream(recording, { encoding: "utf8" });
  const lines = createInterface({ input, crlfDelay: Infinity });
  let firstRecordedAt;
  let replayStartedAt;
  let messages = 0;

  try {
    for await (const line of lines) {
      if (!line.trim()) continue;

      const entry = JSON.parse(line);
      if (entry.event !== "message") continue;
      if (entry.type !== "text" && entry.type !== "binary") {
        throw new Error(`序号 ${entry.sequence} 的消息类型无效`);
      }

      const recordedAt = Date.parse(entry.recordedAt);
      if (!Number.isFinite(recordedAt)) {
        throw new Error(`序号 ${entry.sequence} 的 recordedAt 无效`);
      }

      if (firstRecordedAt === undefined) {
        firstRecordedAt = recordedAt;
        replayStartedAt = performance.now();
      } else {
        const targetElapsed = (recordedAt - firstRecordedAt) / speed;
        const delay = targetElapsed - (performance.now() - replayStartedAt);
        if (delay > 0) await sleep(delay, undefined, { signal });
      }

      const payload = entry.type === "binary"
        ? Buffer.from(entry.data, "base64")
        : Buffer.from(entry.data);
      await sendFrame(socket, entry.type === "binary" ? 0x2 : 0x1, payload);
      messages++;
    }

    if (messages === 0) throw new Error("录制文件中没有 message 事件");
    console.error(`客户端重放完成：${messages} 条消息`);
    socket.end(closeFrame(1000, "replay complete"));
  } catch (error) {
    if (error.name === "AbortError" || socket.destroyed || socket.writableEnded) return;
    console.error(`重放失败：${error.message}`);
    socket.end(closeFrame(1011, "replay failed"));
  } finally {
    lines.close();
    input.destroy();
  }
}

const clients = new Set();

const manifest = JSON.parse(await readFile(resolve(mapRoot, "available.json"), "utf8"));

function mapDefinition(mapName) {
  if (!/^[a-z0-9_]+$/.test(mapName)) return null;
  const definition = manifest.maps?.[mapName];
  const info = definition?.radar_info;
  if (!definition || !info || !Array.isArray(definition.radar_paths)) return null;

  const sources = new Map(definition.radar_paths.map((url) => {
    const fileName = url.split("/").at(-1) ?? "";
    const prefix = `${mapName}_`;
    const suffix = "_radar_psd.png";
    const id = fileName.startsWith(prefix) && fileName.endsWith(suffix)
      ? fileName.slice(prefix.length, -suffix.length).replace(/_+$/, "") || "main"
      : null;
    return [id, url];
  }).filter(([id]) => id));

  const sections = info.verticalsections;
  const layers = sections
    ? Object.entries(sections).map(([name, section]) => ({
      id: name === "default" ? "main" : name,
      minZ: section.AltitudeMin,
      maxZ: section.AltitudeMax,
    }))
    : [...sources.keys()].map((id) => ({ id, minZ: -Number.MAX_VALUE, maxZ: Number.MAX_VALUE }));
  if (!layers.length || layers.some(({ id }) => !sources.has(id))) return null;

  return {
    v: 1,
    map: mapName,
    icon: `/api/maps/${mapName}/icon`,
    position: { x: info.pos_x, y: info.pos_y },
    scale: info.scale,
    rotate: info.rotate ?? 0,
    zoom: info.zoom ?? 0,
    layers: layers.map((layer) => ({ ...layer, image: `/api/maps/${mapName}/layers/${layer.id}` })),
  };
}

async function serveMap(request, response) {
  const pathname = new URL(request.url, "http://127.0.0.1").pathname;
  const match = pathname.match(/^\/api\/maps\/([a-z0-9_]+)(?:\/(icon)|\/layers\/([a-z0-9_]+))?$/);
  if (!match) return false;

  const [, mapName, icon, layerId] = match;
  const metadata = mapDefinition(mapName);
  if (!metadata) {
    response.writeHead(404).end();
    return true;
  }

  if (!icon && !layerId) {
    response.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
    response.end(JSON.stringify(metadata));
    return true;
  }

  const assetName = icon ? "icon.png" : `${layerId}.png`;
  try {
    const image = await readFile(resolve(mapRoot, "assets", mapName, assetName));
    response.writeHead(200, { "Content-Type": "image/png", "Cache-Control": "no-store" });
    response.end(image);
  } catch {
    response.writeHead(404).end();
  }
  return true;
}

const server = createServer(async (request, response) => {
  if (await serveMap(request, response)) return;
  response.writeHead(426, { "Content-Type": "text/plain; charset=utf-8" });
  response.end("请通过 WebSocket 连接 /ws\n");
});

server.on("upgrade", (request, socket) => {
  if (request.url !== "/ws" || request.headers.upgrade?.toLowerCase() !== "websocket") {
    socket.end("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
    return;
  }

  const key = request.headers["sec-websocket-key"];
  if (typeof key !== "string") {
    socket.end("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
    return;
  }

  const accept = createHash("sha1")
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest("base64");
  socket.write([
    "HTTP/1.1 101 Switching Protocols",
    "Upgrade: websocket",
    "Connection: Upgrade",
    `Sec-WebSocket-Accept: ${accept}`,
    "\r\n",
  ].join("\r\n"));

  console.error(`客户端已连接：${socket.remoteAddress}`);
  clients.add(socket);
  socket.on("error", (error) => {
    if (!socket.destroyed) console.error(`客户端连接错误：${error.message}`);
  });
  handleClientFrames(socket);
  const controller = new AbortController();
  socket.once("close", () => {
    clients.delete(socket);
    controller.abort();
  });
  void replay(socket, controller.signal);
});

server.on("error", (error) => {
  console.error(`服务器错误：${error.message}`);
  process.exitCode = 1;
});

server.listen(port, "127.0.0.1", () => {
  console.error(`重放服务已启动：ws://127.0.0.1:${port}/ws`);
  console.error(`录制文件：${recording}（${speed}x）`);
  console.error("每个客户端连接后从头开始重放；按 Ctrl+C 停止");
});

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    console.error(`收到 ${signal}，正在停止重放服务…`);
    server.close(() => process.exit(0));
    for (const socket of clients) socket.destroy();
    server.closeAllConnections();
  });
}
