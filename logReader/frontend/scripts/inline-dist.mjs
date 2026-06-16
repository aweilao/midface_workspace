import { readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const distDir = path.join(root, "dist");
const assetsDir = path.join(distDir, "assets");
const indexPath = path.join(distDir, "index.html");

const indexHtml = await readFile(indexPath, "utf8");
await readdir(assetsDir);
const scriptMatch = indexHtml.match(/<script[^>]*src="\.\/assets\/([^"]+\.js)"[^>]*><\/script>/);
const styleMatch = indexHtml.match(/<link[^>]*href="\.\/assets\/([^"]+\.css)"[^>]*>/);
const jsFile = scriptMatch?.[1];
const cssFile = styleMatch?.[1];

if (!jsFile) {
  throw new Error("cannot find built js asset in dist/index.html");
}
if (!cssFile) {
  throw new Error("cannot find built css asset in dist/index.html");
}

const [js, css] = await Promise.all([
  readFile(path.join(assetsDir, jsFile), "utf8"),
  readFile(path.join(assetsDir, cssFile), "utf8"),
]);

const scriptTag = scriptMatch[0];
const styleTag = styleMatch[0];

let nextHtml = indexHtml
  .replace(scriptTag, () => `<script type="module">\n${js}\n</script>`)
  .replace(styleTag, () => `<style>\n${css}\n</style>`);

if (
  nextHtml === indexHtml ||
  nextHtml.includes(scriptTag) ||
  nextHtml.includes(styleTag)
) {
  throw new Error("failed to inline dist assets");
}

nextHtml = nextHtml.replace(
  "</head>",
  "    <!-- JS/CSS are inlined so this file can be opened directly with file://. -->\n  </head>",
);

await writeFile(indexPath, nextHtml, "utf8");
console.log(`inlined ${cssFile} and ${jsFile} into dist/index.html`);
