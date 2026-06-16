export type LogEntry = {
  seq: number;
  tags: string[];
  properties: Record<string, unknown>;
};

export type ParseResult = {
  file_id: string;
  log_count: number;
  tag_keys: number;
  property_keys: number;
  created_at: string;
};

export type SearchQuery = {
  tags?: string[];
  properties?: Record<string, unknown>;
  offset?: number;
  limit?: number;
};

export type SearchResponse = {
  file_id: string;
  total: number;
  offset: number;
  limit: number;
  indices: number[];
  logs: LogEntry[];
};

export type LogsBySeqResponse = {
  file_id: string;
  total: number;
  missing: number[];
  logs: LogEntry[];
};

export type ColorMapProfile = {
  id: string;
  label: string;
  tags: string[];
  idProperty: string;
};

export type ColorMapRecord = {
  profileId: string;
  id: string;
  rgb: [number, number, number];
  hex: string;
  log: LogEntry;
};

export type ScriptRuntimeSnapshot = {
  logs: LogEntry[];
  selectedIds: string[];
  colorMaps: ColorMapRecord[];
};

export type RenderBlock =
  | { type: "text"; title?: string; text: string }
  | { type: "json"; title?: string; value: unknown }
  | { type: "table"; title?: string; columns?: string[]; rows: Record<string, unknown>[] }
  | { type: "log-table"; title?: string; logs: LogEntry[] }
  | { type: "id-list"; title?: string; ids: string[] }
  | { type: "id-colors"; title?: string; items: Array<{ id: string | number; color: string }> };

export type SavedScriptKind = "query" | "selection";

export type SavedScript = {
  id: string;
  name: string;
  kind: SavedScriptKind;
  code: string;
  created_at: string;
  updated_at: string;
};
