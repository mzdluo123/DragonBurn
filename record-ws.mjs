#!/usr/bin/env node

import { createWriteStream } from "node:fs";
import { resolve } from "node:path";

const DEFAULT_URL = "ws://100.67.185.6:16668/ws";
const url = process.argv[2] ?? DEFAULT_URL;
const output = resolve(
  process.argv[3] ?? `ws-recording-${new Date().toISOString().replaceAll(":", "-")}.jsonl`,
);

let sequence = 0;
let stopping = false;
let stopRequested = false;
const stream = createWriteStream(output, { flags: "wx", encoding: "utf8" });

function record(event) {
  stream.write(`${JSON.stringify({
    sequence: sequence++,
    recordedAt: new Date().toISOString(),
    ...event,
  })}\n`);
}

function finish(exitCode) {
  if (stopping) return;
  stopping = true;
  stream.end(() => {
    console.error(`录制已保存：${output}`);
    process.exitCode = exitCode;
  });
}

stream.on("error", (error) => {
  console.error(`无法写入录制文件 ${output}：${error.message}`);
  process.exitCode = 1;
});

console.error(`正在连接 ${url}`);
console.error(`录制文件：${output}`);

const socket = new WebSocket(url);
socket.binaryType = "arraybuffer";

socket.addEventListener("open", () => {
  record({ event: "open", url });
  console.error("WebSocket 已连接；按 Ctrl+C 停止录制");
});

socket.addEventListener("message", ({ data }) => {
  if (typeof data === "string") {
    record({ event: "message", type: "text", data });
    return;
  }

  record({
    event: "message",
    type: "binary",
    encoding: "base64",
    data: Buffer.from(data).toString("base64"),
  });
});

socket.addEventListener("error", () => {
  record({ event: "error" });
  console.error("WebSocket 连接发生错误");
});

socket.addEventListener("close", ({ code, reason, wasClean }) => {
  record({ event: "close", code, reason, wasClean });
  console.error(`WebSocket 已关闭（code=${code}${reason ? `，reason=${reason}` : ""}）`);
  finish(code === 1000 || stopRequested ? 0 : 1);
});

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    if (stopping) return;
    stopRequested = true;
    console.error(`收到 ${signal}，正在停止录制…`);
    if (socket.readyState === WebSocket.OPEN) {
      socket.close(1000, "recorder stopped");
    } else {
      finish(0);
    }
  });
}
