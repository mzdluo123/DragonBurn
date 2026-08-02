import { mkdir, readdir, readFile, writeFile } from "node:fs/promises";
import { dirname, extname, join, relative, resolve, sep } from "node:path";

const [inputArgument, outputArgument] = process.argv.slice(2);
if (!inputArgument || !outputArgument) {
  throw new Error("Usage: node embed-assets.mjs <dist-directory> <output-header>");
}

const inputRoot = resolve(inputArgument);
const outputPath = resolve(outputArgument);
const mimeTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".svg", "image/svg+xml"],
  [".png", "image/png"],
  [".webp", "image/webp"],
  [".ico", "image/x-icon"],
  [".woff2", "font/woff2"],
]);

async function collect(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  entries.sort((left, right) => left.name.localeCompare(right.name, "en"));
  const files = [];
  for (const entry of entries) {
    const absolute = join(directory, entry.name);
    if (entry.isDirectory()) files.push(...await collect(absolute));
    else if (entry.isFile()) files.push(absolute);
  }
  return files;
}

const files = await collect(inputRoot);
if (!files.some((file) => relative(inputRoot, file).split(sep).join("/") === "index.html")) {
  throw new Error("dist directory does not contain index.html");
}

const lines = [
  "#pragma once",
  "",
  "#include <cstddef>",
  "#include <string_view>",
  "",
  "namespace WebRadarAssets",
  "{",
  "    struct Asset",
  "    {",
  "        std::string_view path;",
  "        std::string_view mime;",
  "        const unsigned char* data;",
  "        std::size_t size;",
  "    };",
  "",
];

const assets = [];
for (const [index, file] of files.entries()) {
  const url = `/${relative(inputRoot, file).split(sep).join("/")}`;
  const mime = mimeTypes.get(extname(file).toLowerCase()) ?? "application/octet-stream";
  const bytes = await readFile(file);
  lines.push(`    inline constexpr unsigned char AssetBytes${index}[] = {`);
  for (let offset = 0; offset < bytes.length; offset += 16) {
    lines.push(`        ${[...bytes.subarray(offset, offset + 16)].map((byte) => `0x${byte.toString(16).padStart(2, "0")}`).join(", ")},`);
  }
  if (bytes.length === 0) lines.push("        0x00,");
  lines.push("    };", "");
  assets.push({ url, mime, index, size: bytes.length });
}

lines.push("    inline constexpr Asset Assets[] = {");
for (const asset of assets) {
  lines.push(`        { "${asset.url}", "${asset.mime}", AssetBytes${asset.index}, ${asset.size} },`);
}
lines.push(
  "    };",
  "",
  "    inline constexpr const Asset* Find(const std::string_view path)",
  "    {",
  "        const std::string_view normalized = path == \"/\" ? \"/index.html\" : path;",
  "        for (const auto& asset : Assets)",
  "            if (asset.path == normalized)",
  "                return &asset;",
  "        return nullptr;",
  "    }",
  "}",
  "",
);

await mkdir(dirname(outputPath), { recursive: true });
await writeFile(outputPath, lines.join("\n"), "utf8");
