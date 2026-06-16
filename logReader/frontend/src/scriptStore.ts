import { SavedScript, SavedScriptKind } from "./types";

const STORAGE_KEY = "logreader.savedScripts.v2";

export const DEFAULT_QUERY_SCRIPT = `async function run() {
  const seqs = lodash.intersection(
    tagSeqs("step1"),
    tagSeqs("face-analyze")
  ).slice(0, 20);

  return [
    text("Summary", "Matched " + seqs.length + " logs on this page"),
    logTable("Step1 logs", logs(seqs))
  ];
}`;

export const DEFAULT_SELECTION_SCRIPT = `async function run(ids) {
  const seqs = lodash.uniq(ids.flatMap((id) => propSeqs("face_id", id))).slice(0, 100);

  return [
    idList("Selected ids", ids),
    logTable("Matched logs", logs(seqs))
  ];
}`;

export function loadSavedScripts(): SavedScript[] {
  const raw = window.localStorage.getItem(STORAGE_KEY);
  if (!raw) {
    return defaultScripts();
  }
  try {
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) {
      return defaultScripts();
    }
    const scripts = parsed.filter(isSavedScript);
    return scripts.length ? scripts : defaultScripts();
  } catch {
    return defaultScripts();
  }
}

export function persistSavedScripts(scripts: SavedScript[]) {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(scripts, null, 2));
}

export function makeSavedScript(kind: SavedScriptKind, name: string, code: string): SavedScript {
  const now = new Date().toISOString();
  return {
    id: `${slugify(name || kind)}-${Date.now().toString(36)}`,
    name: name.trim() || "Untitled script",
    kind,
    code,
    created_at: now,
    updated_at: now,
  };
}

export function exportScripts(scripts: SavedScript[]) {
  const blob = new Blob([JSON.stringify({ version: 2, scripts }, null, 2)], {
    type: "application/json",
  });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = "logreader-scripts.json";
  anchor.click();
  URL.revokeObjectURL(url);
}

export async function importScripts(file: File): Promise<SavedScript[]> {
  const parsed = JSON.parse(await file.text()) as unknown;
  const payload = isRecord(parsed) && Array.isArray(parsed.scripts) ? parsed.scripts : parsed;
  if (!Array.isArray(payload)) {
    throw new Error("scripts json must be an array or { scripts }");
  }
  const scripts = payload.filter(isSavedScript);
  if (!scripts.length) {
    throw new Error("no valid scripts found");
  }
  return scripts;
}

function defaultScripts(): SavedScript[] {
  const now = new Date().toISOString();
  return [
    {
      id: "default-query",
      name: "Step1 logs",
      kind: "query",
      code: DEFAULT_QUERY_SCRIPT,
      created_at: now,
      updated_at: now,
    },
    {
      id: "default-selection",
      name: "Selected face logs",
      kind: "selection",
      code: DEFAULT_SELECTION_SCRIPT,
      created_at: now,
      updated_at: now,
    },
  ];
}

function isSavedScript(value: unknown): value is SavedScript {
  return (
    isRecord(value) &&
    typeof value.id === "string" &&
    typeof value.name === "string" &&
    (value.kind === "query" || value.kind === "selection") &&
    typeof value.code === "string" &&
    typeof value.created_at === "string" &&
    typeof value.updated_at === "string"
  );
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function slugify(value: string) {
  return value
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 40) || "script";
}
