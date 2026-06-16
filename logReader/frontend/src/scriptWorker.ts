import * as lodash from "lodash-es";

self.onmessage = async (event: MessageEvent) => {
  const { code, mode, ids, snapshot } = event.data;
  const allLogs = Array.isArray(snapshot?.logs) ? snapshot.logs : [];
  const tagIndex = new Map<string, number[]>();
  const propertyIndex = new Map<string, number[]>();
  const limit = (value: unknown) => {
    const n = Number(value);
    if (!Number.isFinite(n) || n <= 0) return 100;
    return Math.min(1000, Math.max(1, Math.trunc(n)));
  };
  const keyValue = (value: unknown) => {
    if (value === null) return "null";
    if (typeof value === "string") return value;
    if (value === undefined) return "";
    if (typeof value === "object") return JSON.stringify(value);
    return String(value);
  };
  const propKey = (name: string, value: unknown) => `${name}\u0000${keyValue(value)}`;
  const appendIndex = (map: Map<string, number[]>, key: string, seq: number) => {
    const list = map.get(key);
    if (list) list.push(seq);
    else map.set(key, [seq]);
  };

  for (const log of allLogs) {
    const seq = Number.isInteger(log.seq) ? log.seq : allLogs.indexOf(log);
    const seenTags = new Set(Array.isArray(log.tags) ? log.tags.filter(Boolean) : []);
    for (const tag of seenTags) appendIndex(tagIndex, String(tag), seq);
    const props = log.properties && typeof log.properties === "object" ? log.properties : {};
    for (const [name, value] of Object.entries(props)) appendIndex(propertyIndex, propKey(name, value), seq);
  }

  const tagSeqs = (...tags: unknown[]) => {
    const lists = tags.flat().filter(Boolean).map((tag) => tagIndex.get(String(tag)) || []);
    if (!lists.length) return [];
    return lists.length === 1 ? [...lists[0]] : lodash.intersection(...lists);
  };
  const propSeqs = (name: unknown, value?: unknown) => {
    if (name && typeof name === "object") {
      const lists = Object.entries(name).map(([key, val]) => propertyIndex.get(propKey(key, val)) || []);
      return lists.length ? lodash.intersection(...lists) : [];
    }
    return [...(propertyIndex.get(propKey(String(name), value)) || [])];
  };
  const log = (seq: unknown) => Number.isInteger(seq) ? allLogs[seq as number] : undefined;
  const logs = (seqs: unknown[] = []) => lodash.uniq(seqs).map(log).filter(Boolean);
  const text = (title: string, value: unknown) => ({ type: "text", title, text: String(value) });
  const json = (title: string, value: unknown) => ({ type: "json", title, value });
  const table = (title: string, rows: unknown[], columns?: string[]) => ({ type: "table", title, rows, columns });
  const logTable = (title: string, value: unknown[]) => ({ type: "log-table", title, logs: value });
  const idList = (title: string, value: unknown[]) => ({ type: "id-list", title, ids: value });
  const idColors = (items: unknown[], title = "ID colors") => ({ type: "id-colors", title, items });
  const search = (query: Record<string, unknown> = {}) => {
    const tags = Array.isArray(query.tags) ? query.tags.filter(Boolean) : [];
    const properties = query.properties && typeof query.properties === "object" ? query.properties as Record<string, unknown> : {};
    const indexedSeqs = lodash.intersection(
      ...tags.map((tag) => tagSeqs(tag)),
      ...Object.entries(properties).map(([name, value]) => propSeqs(name, value)),
    );
    const matches = tags.length || Object.keys(properties).length ? logs(indexedSeqs) : allLogs;
    const offset = Math.max(0, Math.trunc(Number(query.offset) || 0));
    const pageLimit = limit(query.limit);
    const pageLogs = matches.slice(offset, offset + pageLimit);
    return {
      file_id: "browser",
      total: matches.length,
      offset,
      limit: pageLimit,
      indices: pageLogs.map((item: any) => item.seq),
      logs: pageLogs,
    };
  };
  const logsBySeq = (seqs: unknown[] = []) => logs(seqs);
  const selectedIds = Array.isArray(snapshot?.selectedIds) ? snapshot.selectedIds : [];
  const colorMaps = Array.isArray(snapshot?.colorMaps) ? snapshot.colorMaps : [];

  try {
    const runtime = {
      allLogs,
      selectedIds,
      colorMaps,
      search,
      logsBySeq,
      tagSeqs,
      propSeqs,
      log,
      logs,
      text,
      json,
      table,
      logTable,
      idList,
      idColors,
      _: lodash,
      lodash,
    };
    const names = Object.keys(runtime);
    const values = Object.values(runtime);
    const factory = new Function(...names, `${code}\n; return typeof run === 'function' ? run : null;`);
    const run = factory(...values);
    if (!run) {
      throw new Error("script must define async function run() or async function run(ids)");
    }
    const blocks = mode === "selection" ? await run(ids || []) : await run();
    self.postMessage({ ok: true, blocks });
  } catch (err) {
    self.postMessage({ ok: false, error: err instanceof Error ? err.message : String(err) });
  }
};
