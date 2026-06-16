import { ChangeEvent, useCallback, useEffect, useMemo, useState } from "react";
import {
  Box,
  Download,
  FileJson,
  Loader2,
  Play,
  Plus,
  Save,
  Search,
  Trash2,
  Upload,
} from "lucide-react";
import { extractColorMaps } from "./colorMaps";
import { parseJsonlFile, LogStore } from "./logStore";
import { extractIdColors, runUserScript } from "./scriptRunner";
import {
  DEFAULT_QUERY_SCRIPT,
  DEFAULT_SELECTION_SCRIPT,
  exportScripts,
  importScripts,
  loadSavedScripts,
  makeSavedScript,
  persistSavedScripts,
} from "./scriptStore";
import { ThreeViewer } from "./ThreeViewer";
import { ColorMapRecord, LogEntry, RenderBlock, SavedScript, SavedScriptKind } from "./types";

export function App() {
  const [logFile, setLogFile] = useState<File | null>(null);
  const [modelFile, setModelFile] = useState<File | null>(null);
  const [materialFile, setMaterialFile] = useState<File | null>(null);
  const [store, setStore] = useState<LogStore | null>(null);
  const [colorMaps, setColorMaps] = useState<ColorMapRecord[]>([]);
  const [workspace, setWorkspace] = useState<"logs" | "three">("logs");
  const [status, setStatus] = useState("等待导入日志");
  const [error, setError] = useState("");
  const [isParsing, setIsParsing] = useState(false);
  const [scripts, setScripts] = useState<SavedScript[]>(() => loadSavedScripts());
  const [activeQueryScriptId, setActiveQueryScriptId] = useState("default-query");
  const [activeSelectionScriptId, setActiveSelectionScriptId] = useState("default-selection");
  const [queryBlocks, setQueryBlocks] = useState<RenderBlock[]>([]);
  const [selectionBlocks, setSelectionBlocks] = useState<RenderBlock[]>([]);
  const [selectedIds, setSelectedIds] = useState<string[]>([]);
  const [selectionMode, setSelectionMode] = useState<"single" | "multi">("single");
  const [profileId, setProfileId] = useState("step1-face");
  const [queryColorOverrides, setQueryColorOverrides] = useState<Record<string, string>>({});
  const [selectionColorOverrides, setSelectionColorOverrides] = useState<Record<string, string>>({});
  const [isRunningQuery, setIsRunningQuery] = useState(false);
  const [isRunningSelection, setIsRunningSelection] = useState(false);
  const [autoSelectionQuery, setAutoSelectionQuery] = useState(false);

  const queryScripts = scripts.filter((script) => script.kind === "query");
  const selectionScripts = scripts.filter((script) => script.kind === "selection");
  const activeQueryScript = queryScripts.find((script) => script.id === activeQueryScriptId) ?? queryScripts[0];
  const activeSelectionScript =
    selectionScripts.find((script) => script.id === activeSelectionScriptId) ?? selectionScripts[0];

  useEffect(() => {
    persistSavedScripts(scripts);
  }, [scripts]);

  useEffect(() => {
    if (!autoSelectionQuery || !store || !activeSelectionScript) {
      return;
    }
    if (!selectedIds.length) {
      setSelectionBlocks([]);
      setSelectionColorOverrides({});
      return;
    }
    void runSelectionScript();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedIds]);

  async function parseLog(file: File) {
    setIsParsing(true);
    setError("");
    setStatus("正在解析 JSONL");
    try {
      const nextStore = await parseJsonlFile(file);
      const nextColorMaps = extractColorMaps(nextStore.logs);
      setStore(nextStore);
      setColorMaps(nextColorMaps);
      setQueryBlocks([]);
      setSelectionBlocks([]);
      setSelectedIds([]);
      setQueryColorOverrides({});
      setSelectionColorOverrides({});
      setStatus(`已导入 ${nextStore.logs.length.toLocaleString()} 条日志，color-map ${nextColorMaps.length.toLocaleString()} 条`);
    } catch (err) {
      setError(toMessage(err));
      setStatus("");
    } finally {
      setIsParsing(false);
    }
  }

  function handleLogFileChange(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0] ?? null;
    setLogFile(file);
    if (file) {
      void parseLog(file);
    }
  }

  function handleBundleImport(event: ChangeEvent<HTMLInputElement>) {
    const files = Array.from(event.target.files ?? []);
    const nextLog = files.find((file) => file.name.toLowerCase().endsWith(".jsonl"));
    const nextModel = files.find((file) => /\.(obj|glb|gltf)$/i.test(file.name));
    const nextMaterial = files.find((file) => file.name.toLowerCase().endsWith(".mtl"));
    if (nextLog) {
      setLogFile(nextLog);
      void parseLog(nextLog);
    }
    if (nextModel) {
      setModelFile(nextModel);
    }
    if (nextMaterial) {
      setMaterialFile(nextMaterial);
    }
    event.target.value = "";
  }

  async function runQueryScript() {
    if (!store || !activeQueryScript) {
      setError("先导入日志并选择查询脚本");
      return;
    }
    setIsRunningQuery(true);
    setError("");
    setStatus("正在执行查询脚本");
    try {
      const blocks = await runUserScript({
        code: activeQueryScript.code,
        mode: "query",
        snapshot: {
          logs: store.logs,
          selectedIds,
          colorMaps,
        },
      });
      setQueryBlocks(blocks);
      applyColorBlocks(blocks, "query");
      setStatus(`脚本返回 ${blocks.length} 个展示块`);
    } catch (err) {
      setError(`查询脚本失败：${toMessage(err)}`);
      setStatus("");
    } finally {
      setIsRunningQuery(false);
    }
  }

  async function runSelectionScript() {
    if (!store || !activeSelectionScript) {
      setError("先导入日志并选择选中查询脚本");
      return;
    }
    setIsRunningSelection(true);
    setError("");
    setStatus("正在执行选中查询");
    try {
      const blocks = await runUserScript({
        code: activeSelectionScript.code,
        mode: "selection",
        ids: selectedIds,
        snapshot: {
          logs: store.logs,
          selectedIds,
          colorMaps,
        },
      });
      setSelectionBlocks(blocks);
      applyColorBlocks(blocks, "selection");
      setStatus(`选中查询返回 ${blocks.length} 个展示块`);
    } catch (err) {
      setError(`选中查询失败：${toMessage(err)}`);
      setStatus("");
    } finally {
      setIsRunningSelection(false);
    }
  }

  function applyColorBlocks(blocks: RenderBlock[], target: "query" | "selection") {
    const nextColors = extractIdColors(blocks);
    if (target === "selection") {
      setSelectionColorOverrides(nextColors);
      return;
    }
    if (Object.keys(nextColors).length) {
      setQueryColorOverrides((current) => ({ ...current, ...nextColors }));
    }
  }

  const handleSelectedIdsChange = useCallback((ids: string[]) => {
    setSelectedIds(ids);
    if (!ids.length) {
      setSelectionBlocks([]);
      setSelectionColorOverrides({});
    }
  }, []);

  function updateActiveScript(kind: SavedScriptKind, patch: Partial<SavedScript>) {
    const active = kind === "query" ? activeQueryScript : activeSelectionScript;
    if (!active) {
      return;
    }
    setScripts((current) =>
      current.map((script) =>
        script.id === active.id ? { ...script, ...patch, updated_at: new Date().toISOString() } : script,
      ),
    );
  }

  function createScript(kind: SavedScriptKind) {
    const script = makeSavedScript(
      kind,
      kind === "query" ? "New query script" : "New selection script",
      kind === "query" ? DEFAULT_QUERY_SCRIPT : DEFAULT_SELECTION_SCRIPT,
    );
    setScripts((current) => [script, ...current]);
    if (kind === "query") {
      setActiveQueryScriptId(script.id);
    } else {
      setActiveSelectionScriptId(script.id);
    }
  }

  function deleteActiveScript(kind: SavedScriptKind) {
    const active = kind === "query" ? activeQueryScript : activeSelectionScript;
    if (!active || active.id.startsWith("default-")) {
      return;
    }
    setScripts((current) => current.filter((script) => script.id !== active.id));
    const fallback = scripts.find((script) => script.kind === kind && script.id !== active.id);
    if (kind === "query") {
      setActiveQueryScriptId(fallback?.id ?? "default-query");
    } else {
      setActiveSelectionScriptId(fallback?.id ?? "default-selection");
    }
  }

  async function importScriptFile(file: File | null) {
    if (!file) {
      return;
    }
    try {
      const imported = await importScripts(file);
      setScripts((current) => mergeScripts(current, imported));
      setStatus(`已导入 ${imported.length} 个脚本`);
    } catch (err) {
      setError(`导入脚本失败：${toMessage(err)}`);
    }
  }

  const topMetrics = useMemo(() => {
    if (!store) {
      return null;
    }
    return [
      ["Logs", store.logs.length.toLocaleString()],
      ["Tags", store.parseResult.tag_keys.toLocaleString()],
      ["Props", store.parseResult.property_keys.toLocaleString()],
      ["Color maps", colorMaps.length.toLocaleString()],
    ];
  }, [colorMaps.length, store]);
  const idColorOverrides = useMemo(
    () => ({ ...queryColorOverrides, ...selectionColorOverrides }),
    [queryColorOverrides, selectionColorOverrides],
  );

  return (
    <main className="appShell">
      <header className="workspaceHeader">
        <div>
          <p className="eyebrow">Log Reader</p>
          <h1>纯浏览器日志与模型调试台</h1>
        </div>
        <div className={`statusStrip ${error ? "error" : ""}`}>
          <span className={error ? "dot errorDot" : "dot"} />
          <span>{error || status}</span>
        </div>
      </header>

      <section className="assetImportBar">
        <label className="topFilePicker">
          <input type="file" accept=".jsonl,application/x-ndjson,application/jsonl" onChange={handleLogFileChange} />
          {isParsing ? <Loader2 className="spin" size={18} /> : <FileJson size={18} />}
          <span>{logFile ? logFile.name : "导入日志 .jsonl"}</span>
        </label>
        <label className="topFilePicker">
          <input
            type="file"
            accept=".obj,.glb,.gltf,model/gltf-binary,model/gltf+json,text/plain"
            onChange={(event) => setModelFile(event.target.files?.[0] ?? null)}
          />
          <Box size={18} />
          <span>{modelFile ? modelFile.name : "导入 OBJ / GLB"}</span>
        </label>
        <label className="topFilePicker">
          <input type="file" accept=".mtl,text/plain" onChange={(event) => setMaterialFile(event.target.files?.[0] ?? null)} />
          <Upload size={18} />
          <span>{materialFile ? materialFile.name : "导入 MTL"}</span>
        </label>
        <label className="primaryButton fileButton">
          <Upload size={17} />
          <input
            multiple
            type="file"
            accept=".jsonl,.obj,.mtl,.glb,.gltf,application/x-ndjson,application/jsonl,model/gltf-binary,model/gltf+json,text/plain"
            onChange={handleBundleImport}
          />
          一键导入
        </label>
        {topMetrics && (
          <div className="topMetrics">
            {topMetrics.map(([name, value]) => (
              <span key={name}>
                {name}: <b>{value}</b>
              </span>
            ))}
          </div>
        )}
      </section>

      <nav className="workspaceModes" aria-label="工作区切换">
        <button className={workspace === "logs" ? "active" : ""} type="button" onClick={() => setWorkspace("logs")}>
          <Search size={16} />
          日志查询
        </button>
        <button className={workspace === "three" ? "active" : ""} type="button" onClick={() => setWorkspace("three")}>
          <Box size={16} />
          Three.js
        </button>
      </nav>

      {workspace === "logs" ? (
        <section className="logWorkspace">
          <ScriptPanel
            activeScript={activeQueryScript}
            disabled={!store || isRunningQuery}
            isRunning={isRunningQuery}
            kind="query"
            scripts={queryScripts}
            onCreate={() => createScript("query")}
            onDelete={() => deleteActiveScript("query")}
            onExport={() => exportScripts(scripts)}
            onImport={(file) => void importScriptFile(file)}
            onRun={() => void runQueryScript()}
            onScriptChange={setActiveQueryScriptId}
            onUpdate={(patch) => updateActiveScript("query", patch)}
          />
          <section className="resultsPane">
            <RenderBlocks
              blocks={queryBlocks}
              emptyText={store ? "执行脚本后显示 RenderBlock 结果" : "先导入 JSONL 日志"}
              onApplyColors={(blocks) => applyColorBlocks(blocks, "query")}
            />
          </section>
        </section>
      ) : (
        <section className="modelWorkspace">
          <ThreeViewer
            colorMaps={colorMaps}
            idColorOverrides={idColorOverrides}
            materialFile={materialFile}
            modelFile={modelFile}
            profileId={profileId}
            selectionMode={selectionMode}
            selectedIds={selectedIds}
            onClearIdColors={() => {
              setQueryColorOverrides({});
              setSelectionColorOverrides({});
            }}
            onProfileChange={(nextProfile) => {
              setProfileId(nextProfile);
              handleSelectedIdsChange([]);
            }}
            onSelectedIdsChange={handleSelectedIdsChange}
            onSelectionModeChange={setSelectionMode}
          />
          <aside className="selectionScriptPane">
            <div className="panelTitle">
              <Search size={18} />
              <h2>选中查询</h2>
            </div>
            <label className="toggleRow">
              <input checked={autoSelectionQuery} type="checkbox" onChange={(event) => setAutoSelectionQuery(event.target.checked)} />
              选中变化后自动执行
            </label>
            <ScriptPanel
              activeScript={activeSelectionScript}
              disabled={!store || isRunningSelection}
              isRunning={isRunningSelection}
              kind="selection"
              scripts={selectionScripts}
              compact
              onCreate={() => createScript("selection")}
              onDelete={() => deleteActiveScript("selection")}
              onExport={() => exportScripts(scripts)}
              onImport={(file) => void importScriptFile(file)}
              onRun={() => void runSelectionScript()}
              onScriptChange={setActiveSelectionScriptId}
              onUpdate={(patch) => updateActiveScript("selection", patch)}
            />
            <RenderBlocks
              blocks={selectionBlocks}
              emptyText="点击模型选择 id 后执行脚本"
              onApplyColors={(blocks) => applyColorBlocks(blocks, "selection")}
              compact
            />
          </aside>
        </section>
      )}
    </main>
  );
}

function ScriptPanel({
  activeScript,
  compact = false,
  disabled,
  isRunning,
  kind,
  scripts,
  onCreate,
  onDelete,
  onExport,
  onImport,
  onRun,
  onScriptChange,
  onUpdate,
}: {
  activeScript: SavedScript | undefined;
  compact?: boolean;
  disabled: boolean;
  isRunning: boolean;
  kind: SavedScriptKind;
  scripts: SavedScript[];
  onCreate: () => void;
  onDelete: () => void;
  onExport: () => void;
  onImport: (file: File | null) => void;
  onRun: () => void;
  onScriptChange: (id: string) => void;
  onUpdate: (patch: Partial<SavedScript>) => void;
}) {
  return (
    <aside className={`scriptPanel ${compact ? "compact" : ""}`}>
      <div className="panelTitle">
        <Search size={18} />
        <h2>{kind === "query" ? "JS 查询" : "Selection JS"}</h2>
      </div>
      <div className="scriptToolbar">
        <select value={activeScript?.id ?? ""} onChange={(event) => onScriptChange(event.target.value)}>
          {scripts.map((script) => (
            <option key={script.id} value={script.id}>
              {script.name}
            </option>
          ))}
        </select>
        <button className="iconButton" title="新建脚本" type="button" onClick={onCreate}>
          <Plus size={15} />
        </button>
        <button className="iconButton" disabled={!activeScript || activeScript.id.startsWith("default-")} title="删除脚本" type="button" onClick={onDelete}>
          <Trash2 size={15} />
        </button>
      </div>
      <input
        className="scriptNameInput"
        value={activeScript?.name ?? ""}
        placeholder="脚本名称"
        onChange={(event) => onUpdate({ name: event.target.value })}
      />
      <textarea
        className="scriptBox"
        spellCheck={false}
        value={activeScript?.code ?? ""}
        onChange={(event) => onUpdate({ code: event.target.value })}
      />
      <div className="scriptActions">
        <button className="primaryButton" disabled={disabled || !activeScript} type="button" onClick={onRun}>
          {isRunning ? <Loader2 className="spin" size={17} /> : <Play size={17} />}
          执行
        </button>
        <label className="ghostButton fileButton">
          <Upload size={15} />
          <input accept=".json,application/json" type="file" onChange={(event) => onImport(event.target.files?.[0] ?? null)} />
          导入
        </label>
        <button className="ghostButton" type="button" onClick={onExport}>
          <Download size={15} />
          导出
        </button>
        <span className="saveHint">
          <Save size={14} />
          localStorage
        </span>
      </div>
    </aside>
  );
}

function RenderBlocks({
  blocks,
  compact = false,
  emptyText,
  onApplyColors,
}: {
  blocks: RenderBlock[];
  compact?: boolean;
  emptyText: string;
  onApplyColors: (blocks: RenderBlock[]) => void;
}) {
  if (!blocks.length) {
    return (
      <div className="emptyState">
        <FileJson size={32} />
        <span>{emptyText}</span>
      </div>
    );
  }
  return (
    <div className={`renderBlocks ${compact ? "compact" : ""}`}>
      {blocks.map((block, index) => (
        <RenderBlockView block={block} key={`${block.type}-${index}`} onApplyColors={onApplyColors} />
      ))}
    </div>
  );
}

function RenderBlockView({ block, onApplyColors }: { block: RenderBlock; onApplyColors: (blocks: RenderBlock[]) => void }) {
  const title = "title" in block ? block.title : undefined;
  if (block.type === "text") {
    return (
      <section className="renderBlock">
        {title && <h3>{title}</h3>}
        <p>{block.text}</p>
      </section>
    );
  }
  if (block.type === "json") {
    return (
      <section className="renderBlock">
        {title && <h3>{title}</h3>}
        <pre>{JSON.stringify(block.value, null, 2)}</pre>
      </section>
    );
  }
  if (block.type === "id-list") {
    return (
      <section className="renderBlock">
        {title && <h3>{title}</h3>}
        <div className="chipList">{block.ids.map((id) => <span key={id}>{id}</span>)}</div>
      </section>
    );
  }
  if (block.type === "id-colors") {
    return (
      <section className="renderBlock">
        <div className="blockHeader">
          <h3>{title || "ID colors"}</h3>
          <button className="ghostButton" type="button" onClick={() => onApplyColors([block])}>
            应用到模型
          </button>
        </div>
        <div className="colorCommandList">
          {block.items.map((item) => (
            <span key={`${item.id}-${item.color}`}>
              <i style={{ backgroundColor: item.color }} />
              {String(item.id)} {item.color}
            </span>
          ))}
        </div>
      </section>
    );
  }
  if (block.type === "table") {
    const columns = block.columns?.length ? block.columns : Array.from(new Set(block.rows.flatMap((row) => Object.keys(row))));
    return (
      <section className="renderBlock">
        {title && <h3>{title}</h3>}
        <DataTable columns={columns} rows={block.rows} />
      </section>
    );
  }
  return (
    <section className="renderBlock">
      {title && <h3>{title}</h3>}
      <LogTable logs={block.logs} />
    </section>
  );
}

function DataTable({ columns, rows }: { columns: string[]; rows: Record<string, unknown>[] }) {
  return (
    <div className="tableWrap">
      <table>
        <thead>
          <tr>{columns.map((column) => <th key={column}>{column}</th>)}</tr>
        </thead>
        <tbody>
          {rows.map((row, rowIndex) => (
            <tr key={rowIndex}>
              {columns.map((column) => (
                <td key={column}>{formatValue(row[column])}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function LogTable({ logs }: { logs: LogEntry[] }) {
  return (
    <div className="tableWrap">
      <table>
        <thead>
          <tr>
            <th>Seq</th>
            <th>Tags</th>
            <th>Properties</th>
          </tr>
        </thead>
        <tbody>
          {logs.map((log) => (
            <tr key={log.seq}>
              <td className="seqCell">{log.seq}</td>
              <td>
                <div className="tagList">{log.tags.map((tag) => <span key={`${log.seq}-${tag}`}>{tag}</span>)}</div>
              </td>
              <td>
                <div className="resultProperties">
                  {Object.entries(log.properties).map(([key, value]) => (
                    <span className="resultProperty" key={`${log.seq}-${key}`}>
                      <b>{key}</b>
                      <em>{formatValue(value)}</em>
                    </span>
                  ))}
                </div>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function mergeScripts(current: SavedScript[], imported: SavedScript[]) {
  const byId = new Map(current.map((script) => [script.id, script]));
  for (const script of imported) {
    byId.set(script.id, script);
  }
  return Array.from(byId.values()).sort((a, b) => b.updated_at.localeCompare(a.updated_at));
}

function formatValue(value: unknown) {
  if (value === null) {
    return "null";
  }
  if (typeof value === "string") {
    return value || "(empty)";
  }
  if (typeof value === "number" || typeof value === "boolean") {
    return String(value);
  }
  return JSON.stringify(value);
}

function toMessage(err: unknown) {
  return err instanceof Error ? err.message : String(err);
}
