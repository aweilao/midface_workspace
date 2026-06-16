import { formatLookupValue } from "./colorMaps";
import { LogEntry, LogsBySeqResponse, ParseResult, SearchQuery, SearchResponse } from "./types";

const DEFAULT_LIMIT = 100;
const MAX_LIMIT = 1000;

type PropertyKey = `${string}\u0000${string}`;

export type LogStore = {
  fileId: string;
  fileName: string;
  createdAt: string;
  logs: LogEntry[];
  tagIndex: Map<string, number[]>;
  propertyIndex: Map<PropertyKey, number[]>;
  parseResult: ParseResult;
  search(query: SearchQuery): SearchResponse;
  logsBySeq(seqs: number[]): LogsBySeqResponse;
};

export async function parseJsonlFile(file: File): Promise<LogStore> {
  const text = await file.text();
  const lines = text.split(/\r?\n/);
  const logs: LogEntry[] = [];
  const tagIndex = new Map<string, number[]>();
  const propertyIndex = new Map<PropertyKey, number[]>();
  const createdAt = new Date().toISOString();

  lines.forEach((raw, lineIndex) => {
    const line = raw.trim();
    if (!line) {
      return;
    }
    let parsed: Partial<LogEntry>;
    try {
      parsed = JSON.parse(line) as Partial<LogEntry>;
    } catch (err) {
      throw new Error(`parse line ${lineIndex + 1}: ${toMessage(err)}`);
    }
    const entry: LogEntry = {
      seq: logs.length,
      tags: Array.isArray(parsed.tags) ? parsed.tags.filter((item): item is string => typeof item === "string") : [],
      properties: isRecord(parsed.properties) ? parsed.properties : {},
    };
    logs.push(entry);

    for (const tag of uniqueStrings(entry.tags)) {
      appendIndex(tagIndex, tag, entry.seq);
    }
    for (const [name, value] of Object.entries(entry.properties)) {
      appendIndex(propertyIndex, makePropertyKey(name, value), entry.seq);
    }
  });

  if (!logs.length) {
    throw new Error("jsonl contains no log entries");
  }

  const fileId = `local-${Date.now().toString(36)}`;
  const parseResult: ParseResult = {
    file_id: fileId,
    log_count: logs.length,
    tag_keys: tagIndex.size,
    property_keys: propertyIndex.size,
    created_at: createdAt,
  };

  const store: LogStore = {
    fileId,
    fileName: file.name,
    createdAt,
    logs,
    tagIndex,
    propertyIndex,
    parseResult,
    search(query) {
      return searchStore(store, query);
    },
    logsBySeq(seqs) {
      return logsBySeq(store, seqs);
    },
  };
  return store;
}

function searchStore(store: LogStore, query: SearchQuery): SearchResponse {
  const tags = uniqueStrings(query.tags ?? []);
  const properties = query.properties ?? {};
  const lists: number[][] = [];

  for (const tag of tags) {
    const matches = store.tagIndex.get(tag);
    if (!matches) {
      return emptySearch(store.fileId, query);
    }
    lists.push(matches);
  }

  const propertyKeys = Object.entries(properties)
    .map(([name, value]) => makePropertyKey(name, value))
    .sort();
  for (const key of propertyKeys) {
    const matches = store.propertyIndex.get(key);
    if (!matches) {
      return emptySearch(store.fileId, query);
    }
    lists.push(matches);
  }

  let candidates: number[];
  if (!lists.length) {
    candidates = store.logs.map((_, index) => index);
  } else {
    lists.sort((a, b) => a.length - b.length);
    candidates = [...lists[0]];
    for (const list of lists.slice(1)) {
      candidates = intersectSorted(candidates, list);
      if (!candidates.length) {
        break;
      }
    }
  }

  const offset = normalizeOffset(query.offset);
  const limit = normalizeLimit(query.limit);
  const start = Math.min(offset, candidates.length);
  const indices = candidates.slice(start, start + limit);
  return {
    file_id: store.fileId,
    total: candidates.length,
    offset,
    limit,
    indices,
    logs: indices.map((seq) => store.logs[seq]),
  };
}

function logsBySeq(store: LogStore, seqs: number[]): LogsBySeqResponse {
  const seen = new Set<number>();
  const missing: number[] = [];
  const logs: LogEntry[] = [];
  for (const seq of seqs) {
    if (!Number.isInteger(seq) || seen.has(seq)) {
      continue;
    }
    seen.add(seq);
    const log = store.logs[seq];
    if (!log) {
      missing.push(seq);
      continue;
    }
    logs.push(log);
  }
  return {
    file_id: store.fileId,
    total: logs.length,
    missing,
    logs,
  };
}

function emptySearch(fileId: string, query: SearchQuery): SearchResponse {
  return {
    file_id: fileId,
    total: 0,
    offset: normalizeOffset(query.offset),
    limit: normalizeLimit(query.limit),
    indices: [],
    logs: [],
  };
}

function appendIndex<T>(map: Map<T, number[]>, key: T, seq: number) {
  const list = map.get(key);
  if (list) {
    list.push(seq);
  } else {
    map.set(key, [seq]);
  }
}

function makePropertyKey(name: string, value: unknown): PropertyKey {
  return `${name}\u0000${propertyValueKey(value)}`;
}

function propertyValueKey(value: unknown) {
  if (value === null) {
    return "null";
  }
  if (typeof value === "string") {
    return value;
  }
  if (value === undefined) {
    return "";
  }
  if (typeof value === "object") {
    return JSON.stringify(value);
  }
  return formatLookupValue(value);
}

function intersectSorted(a: number[], b: number[]) {
  const result: number[] = [];
  let i = 0;
  let j = 0;
  while (i < a.length && j < b.length) {
    if (a[i] === b[j]) {
      result.push(a[i]);
      i += 1;
      j += 1;
    } else if (a[i] < b[j]) {
      i += 1;
    } else {
      j += 1;
    }
  }
  return result;
}

function normalizeOffset(value: unknown) {
  const number = Number(value);
  if (!Number.isFinite(number) || number < 0) {
    return 0;
  }
  return Math.trunc(number);
}

function normalizeLimit(value: unknown) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) {
    return DEFAULT_LIMIT;
  }
  return Math.min(MAX_LIMIT, Math.max(1, Math.trunc(number)));
}

function uniqueStrings(values: string[]) {
  return Array.from(new Set(values.map((value) => value.trim()).filter(Boolean)));
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function toMessage(err: unknown) {
  return err instanceof Error ? err.message : String(err);
}
