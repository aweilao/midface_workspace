import { LogEntry, RenderBlock, ScriptRuntimeSnapshot } from "./types";
import ScriptWorker from "./scriptWorker?worker&inline";

type ScriptMode = "query" | "selection";

type WorkerSuccess = {
  ok: true;
  blocks: unknown;
};

type WorkerFailure = {
  ok: false;
  error: string;
};

export function runUserScript({
  code,
  mode,
  ids = [],
  snapshot,
  timeoutMs = 5000,
}: {
  code: string;
  mode: ScriptMode;
  ids?: string[];
  snapshot: ScriptRuntimeSnapshot;
  timeoutMs?: number;
}): Promise<RenderBlock[]> {
  return new Promise((resolve, reject) => {
    const worker = new ScriptWorker();
    const timeout = window.setTimeout(() => {
      worker.terminate();
      reject(new Error(`script timed out after ${timeoutMs}ms`));
    }, timeoutMs);

    worker.onmessage = (event: MessageEvent<WorkerSuccess | WorkerFailure>) => {
      window.clearTimeout(timeout);
      worker.terminate();
      if (event.data.ok) {
        resolve(normalizeRenderBlocks(event.data.blocks));
      } else {
        reject(new Error(event.data.error));
      }
    };
    worker.onerror = (event: ErrorEvent) => {
      window.clearTimeout(timeout);
      worker.terminate();
      reject(new Error(event.message));
    };
    worker.postMessage({ code, mode, ids, snapshot });
  });
}

function normalizeRenderBlocks(value: unknown): RenderBlock[] {
  const rawBlocks = Array.isArray(value) ? value : [value];
  const blocks: RenderBlock[] = [];
  for (const block of rawBlocks) {
    if (!isRecord(block) || typeof block.type !== "string") {
      blocks.push({ type: "json", title: "Result", value: block });
      continue;
    }
    if (block.type === "text" && typeof block.text === "string") {
      blocks.push({ type: "text", title: readTitle(block), text: block.text });
    } else if (block.type === "json") {
      blocks.push({ type: "json", title: readTitle(block), value: block.value });
    } else if (block.type === "table" && Array.isArray(block.rows)) {
      blocks.push({
        type: "table",
        title: readTitle(block),
        columns: Array.isArray(block.columns) ? block.columns.filter((item): item is string => typeof item === "string") : undefined,
        rows: block.rows.filter(isRecord),
      });
    } else if (block.type === "log-table" && Array.isArray(block.logs)) {
      blocks.push({
        type: "log-table",
        title: readTitle(block),
        logs: block.logs.filter(isLogEntry),
      });
    } else if (block.type === "id-list" && Array.isArray(block.ids)) {
      blocks.push({ type: "id-list", title: readTitle(block), ids: block.ids.map(String) });
    } else if (block.type === "id-colors" && Array.isArray(block.items)) {
      blocks.push({
        type: "id-colors",
        title: readTitle(block),
        items: normalizeIdColorItems(block.items),
      });
    } else {
      blocks.push({ type: "json", title: readTitle(block) || block.type, value: block });
    }
  }
  return blocks;
}

function normalizeIdColorItems(items: unknown[]) {
  const byId = new Map<string, { id: string; color: string }>();
  for (const item of items) {
    if (!isRecord(item) || item.id === undefined || typeof item.color !== "string") {
      continue;
    }
    const id = String(item.id);
    byId.set(id, { id, color: String(item.color) });
  }
  return Array.from(byId.values());
}

export function extractIdColors(blocks: RenderBlock[]) {
  const colors: Record<string, string> = {};
  for (const block of blocks) {
    if (block.type !== "id-colors") {
      continue;
    }
    for (const item of block.items) {
      colors[String(item.id)] = item.color;
    }
  }
  return colors;
}

function readTitle(value: Record<string, unknown>) {
  return typeof value.title === "string" ? value.title : undefined;
}

function isLogEntry(value: unknown): value is LogEntry {
  return (
    isRecord(value) &&
    typeof value.seq === "number" &&
    Array.isArray(value.tags) &&
    isRecord(value.properties)
  );
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}
