import { useEffect, useMemo, useRef, useState } from "react";
import { Box, Loader2, MousePointer2, RotateCcw, X } from "lucide-react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { MTLLoader } from "three/examples/jsm/loaders/MTLLoader.js";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import { COLOR_MAP_PROFILES, findColorMapId, rgbKey, rgbToHex } from "./colorMaps";
import { ColorMapRecord } from "./types";

type TriangleRef = {
  object: THREE.Mesh;
  triangleIndex: number;
};

type TriangleColorRecord = {
  rgb: [number, number, number];
  hex: string;
};

type PickInfo = {
  id: string | null;
  hex: string;
  rgb: [number, number, number];
  objectName: string;
};

type ViewerModel = {
  root: THREE.Object3D;
  triangleColors: Map<string, TriangleColorRecord>;
  triangleToId: Map<string, string>;
  idToTriangles: Map<string, TriangleRef[]>;
};

type DisplayMode = "normal" | "dim-unselected" | "isolate-selected";
type SelectionMode = "single" | "multi";

export function ThreeViewer({
  modelFile,
  materialFile,
  colorMaps,
  selectedIds,
  selectionMode,
  profileId,
  idColorOverrides,
  onSelectedIdsChange,
  onSelectionModeChange,
  onProfileChange,
  onClearIdColors,
}: {
  modelFile: File | null;
  materialFile: File | null;
  colorMaps: ColorMapRecord[];
  selectedIds: string[];
  selectionMode: SelectionMode;
  profileId: string;
  idColorOverrides: Record<string, string>;
  onSelectedIdsChange: (ids: string[]) => void;
  onSelectionModeChange: (mode: SelectionMode) => void;
  onProfileChange: (profileId: string) => void;
  onClearIdColors: () => void;
}) {
  const hostRef = useRef<HTMLDivElement | null>(null);
  const selectedIdsRef = useRef<string[]>(selectedIds);
  const selectionModeRef = useRef<SelectionMode>(selectionMode);
  const displayModeRef = useRef<DisplayMode>("normal");
  const idColorOverridesRef = useRef<Record<string, string>>(idColorOverrides);
  const refreshRef = useRef<(() => void) | null>(null);
  const [displayMode, setDisplayMode] = useState<DisplayMode>("normal");
  const [pickInfo, setPickInfo] = useState<PickInfo | null>(null);
  const [status, setStatus] = useState("等待模型");
  const [error, setError] = useState("");
  const [mappedIdCount, setMappedIdCount] = useState(0);
  const activeProfile = COLOR_MAP_PROFILES.find((profile) => profile.id === profileId) ?? COLOR_MAP_PROFILES[0];

  useEffect(() => {
    selectedIdsRef.current = selectedIds;
    refreshRef.current?.();
  }, [selectedIds]);

  useEffect(() => {
    selectionModeRef.current = selectionMode;
  }, [selectionMode]);

  useEffect(() => {
    idColorOverridesRef.current = idColorOverrides;
    refreshRef.current?.();
  }, [idColorOverrides]);

  useEffect(() => {
    displayModeRef.current = displayMode;
    refreshRef.current?.();
  }, [displayMode]);

  useEffect(() => {
    let disposed = false;
    let animationId = 0;
    let renderer: THREE.WebGLRenderer | null = null;
    let controls: OrbitControls | null = null;
    let resizeObserver: ResizeObserver | null = null;
    let scene: THREE.Scene | null = null;
    let model: ViewerModel | null = null;
    const objectUrls: string[] = [];
    let overlayMeshes: THREE.Mesh[] = [];
    let pointerHandler: ((event: PointerEvent) => void) | null = null;

    function rememberUrl(url: string) {
      objectUrls.push(url);
      return url;
    }

    function clearOverlays() {
      for (const overlay of overlayMeshes) {
        scene?.remove(overlay);
        disposeObject3D(overlay);
      }
      overlayMeshes = [];
    }

    async function init() {
      const host = hostRef.current;
      if (!host) {
        return;
      }
      setError("");
      setStatus(modelFile ? "正在加载模型" : "已加载示例模型");

      try {
        scene = new THREE.Scene();
        scene.background = new THREE.Color(0xf5f7f1);
        const camera = new THREE.PerspectiveCamera(45, 1, 0.01, 1000);
        renderer = new THREE.WebGLRenderer({ antialias: true });
        renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        renderer.outputColorSpace = THREE.SRGBColorSpace;
        renderer.domElement.className = "threeCanvas";
        host.replaceChildren(renderer.domElement);

        controls = new OrbitControls(camera, renderer.domElement);
        controls.enableDamping = true;
        controls.dampingFactor = 0.08;

        scene.add(new THREE.HemisphereLight(0xffffff, 0x87948d, 2.4));
        const keyLight = new THREE.DirectionalLight(0xffffff, 2.0);
        keyLight.position.set(5, 6, 4);
        scene.add(keyLight);

        const root = modelFile
          ? await loadModelFile(modelFile, materialFile, rememberUrl)
          : createSampleModel();
        if (disposed) {
          disposeObject3D(root);
          return;
        }
        scene.add(root);

        model = buildViewerModel(root, colorMaps, profileId);
        setMappedIdCount(model.idToTriangles.size);
        applyBaseMaterials(model, idColorOverridesRef.current);
        fitCameraToObject(camera, controls, root);

        const refresh = () => {
          if (!scene || !model) {
            return;
          }
          clearOverlays();
          applyBaseMaterials(model, idColorOverridesRef.current);
          applyDisplayOpacity(model.root, selectedIdsRef.current.length ? displayModeRef.current : "normal");
          const selectedTriangles = selectedIdsRef.current.flatMap((id) => model?.idToTriangles.get(id) ?? []);
          if (selectedTriangles.length) {
            const overlay = createTrianglesHighlight(selectedTriangles, "#ffd84d", 0.78);
            scene.add(overlay);
            overlayMeshes.push(overlay);
          }
        };
        refreshRef.current = refresh;
        refresh();

        const resize = () => {
          if (!renderer || !host) {
            return;
          }
          const rect = host.getBoundingClientRect();
          const width = Math.max(1, Math.floor(rect.width));
          const height = Math.max(1, Math.floor(rect.height));
          renderer.setSize(width, height, false);
          camera.aspect = width / height;
          camera.updateProjectionMatrix();
        };
        resize();
        resizeObserver = new ResizeObserver(resize);
        resizeObserver.observe(host);

        const raycaster = new THREE.Raycaster();
        const pointer = new THREE.Vector2();
        pointerHandler = (event: PointerEvent) => {
          if (!renderer || !scene || !model) {
            return;
          }
          const rect = renderer.domElement.getBoundingClientRect();
          pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
          pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
          raycaster.setFromCamera(pointer, camera);
          const hit = raycaster
            .intersectObjects(scene.children, true)
            .find((item) => isModelMesh(item.object));
          if (!hit || !isMesh(hit.object)) {
            return;
          }
          const key = triangleKey(hit.object, hit.faceIndex ?? 0);
          const record = model.triangleColors.get(key);
          const id = model.triangleToId.get(key) ?? null;
          if (record) {
            setPickInfo({
              id,
              hex: record.hex,
              rgb: record.rgb,
              objectName: hit.object.name || hit.object.parent?.name || "(unnamed)",
            });
          }
          if (id) {
            const next =
              selectionModeRef.current === "single"
                ? toggleSingleString(selectedIdsRef.current, id)
                : toggleString(selectedIdsRef.current, id);
            selectedIdsRef.current = next;
            onSelectedIdsChange(next);
            refresh();
          }
        };
        renderer.domElement.addEventListener("pointerdown", pointerHandler);

        const animate = () => {
          if (disposed || !renderer || !scene) {
            return;
          }
          controls?.update();
          renderer.render(scene, camera);
          animationId = window.requestAnimationFrame(animate);
        };
        animate();
        setStatus(`${modelFile ? modelFile.name : "示例模型"}；${activeProfile.label} 映射 ${model.idToTriangles.size} 个 id`);
      } catch (err) {
        setError(toMessage(err));
        setStatus("模型加载失败");
      }
    }

    void init();

    return () => {
      disposed = true;
      window.cancelAnimationFrame(animationId);
      resizeObserver?.disconnect();
      if (renderer?.domElement && pointerHandler) {
        renderer.domElement.removeEventListener("pointerdown", pointerHandler);
      }
      refreshRef.current = null;
      controls?.dispose();
      clearOverlays();
      disposeObject3D(scene);
      renderer?.dispose();
      objectUrls.forEach((url) => URL.revokeObjectURL(url));
      hostRef.current?.replaceChildren();
    };
  }, [activeProfile.label, colorMaps, materialFile, modelFile, onSelectedIdsChange, profileId]);

  const profileCounts = useMemo(() => {
    const counts = new Map<string, number>();
    for (const record of colorMaps) {
      counts.set(record.profileId, (counts.get(record.profileId) ?? 0) + 1);
    }
    return counts;
  }, [colorMaps]);

  return (
    <section className="threeWorkspace">
      <section className="threeCanvasPanel">
        <div ref={hostRef} className="threeCanvasHost" />
        <div className={`threeStatus ${error ? "error" : ""}`}>{error || status}</div>
      </section>

      <aside className="threeInspector">
        <div className="inspectorTitle">
          <MousePointer2 size={18} />
          <h2>模型选择</h2>
        </div>

        <label className="fieldLabel">
          Color map
          <select value={profileId} onChange={(event) => onProfileChange(event.target.value)}>
            {COLOR_MAP_PROFILES.map((profile) => (
              <option key={profile.id} value={profile.id}>
                {profile.label} ({profileCounts.get(profile.id) ?? 0})
              </option>
            ))}
          </select>
        </label>

        <div className="segmentedControl" aria-label="显示模式">
          <button className={displayMode === "normal" ? "active" : ""} type="button" onClick={() => setDisplayMode("normal")}>
            正常
          </button>
          <button className={displayMode === "dim-unselected" ? "active" : ""} type="button" onClick={() => setDisplayMode("dim-unselected")}>
            弱化
          </button>
          <button className={displayMode === "isolate-selected" ? "active" : ""} type="button" onClick={() => setDisplayMode("isolate-selected")}>
            只看选中
          </button>
        </div>

        <div className="segmentedControl twoPartControl" aria-label="选择模式">
          <button className={selectionMode === "single" ? "active" : ""} type="button" onClick={() => onSelectionModeChange("single")}>
            单选
          </button>
          <button className={selectionMode === "multi" ? "active" : ""} type="button" onClick={() => onSelectionModeChange("multi")}>
            多选
          </button>
        </div>

        <section className="metricGrid">
          <span>当前映射</span>
          <b>{mappedIdCount}</b>
          <span>已选 id</span>
          <b>{selectedIds.length}</b>
          <span>脚本染色</span>
          <b>{Object.keys(idColorOverrides).length}</b>
        </section>

        {pickInfo ? (
          <section className="pickedPanel">
            <div className="pickedColorSwatch" style={{ background: pickInfo.hex }} />
            <div className="colorValueGrid">
              <span>ID</span>
              <b>{pickInfo.id ?? "-"}</b>
              <span>HEX</span>
              <b>{pickInfo.hex}</b>
              <span>RGB</span>
              <b>{pickInfo.rgb.join(", ")}</b>
              <span>对象</span>
              <b>{pickInfo.objectName}</b>
            </div>
          </section>
        ) : (
          <div className="emptyPick">
            <Box size={26} />
            <span>点击模型面查看 id</span>
          </div>
        )}

        <section className="chipPanel">
          <div className="panelSubhead">
            <b>Selected ids</b>
            <button className="iconButton" disabled={!selectedIds.length} title="清空选择" type="button" onClick={() => onSelectedIdsChange([])}>
              <X size={15} />
            </button>
          </div>
          <div className="chipList">
            {selectedIds.length ? selectedIds.map((id) => <span key={id}>{id}</span>) : <em>暂无选中</em>}
          </div>
        </section>

        <button className="ghostButton" disabled={!Object.keys(idColorOverrides).length} type="button" onClick={onClearIdColors}>
          <RotateCcw size={15} />
          重置脚本染色
        </button>
      </aside>
    </section>
  );
}

async function loadModelFile(file: File, materialFile: File | null, rememberUrl: (url: string) => string) {
  const url = rememberUrl(URL.createObjectURL(file));
  const ext = file.name.split(".").pop()?.toLowerCase();
  if (ext === "obj") {
    const loader = new OBJLoader();
    if (materialFile) {
      const materialUrl = rememberUrl(URL.createObjectURL(materialFile));
      const materials = await new MTLLoader().loadAsync(materialUrl);
      materials.preload();
      loader.setMaterials(materials);
    }
    return loader.loadAsync(url);
  }
  const gltf = await new GLTFLoader().loadAsync(url);
  return gltf.scene;
}

function createSampleModel() {
  const group = new THREE.Group();
  const colors = ["#d14b3f", "#2f8f62", "#356fba"];
  colors.forEach((color, index) => {
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(1, 1, 0.22),
      new THREE.MeshStandardMaterial({ color, roughness: 0.65, metalness: 0.03 }),
    );
    mesh.position.x = (index - 1) * 1.25;
    mesh.name = `sample_${index}`;
    group.add(mesh);
  });
  return group;
}

function buildViewerModel(root: THREE.Object3D, colorMaps: ColorMapRecord[], profileId: string): ViewerModel {
  const colorToId = new Map<string, string>();
  for (const record of colorMaps) {
    if (record.profileId === profileId) {
      colorToId.set(rgbKey(record.rgb), record.id);
    }
  }
  const triangleColors = captureTriangleColors(root);
  const triangleToId = new Map<string, string>();
  const idToTriangles = new Map<string, TriangleRef[]>();

  triangleColors.forEach((record, key) => {
    const id = findColorMapId(colorToId, record.rgb);
    const ref = triangleRefFromKey(key);
    if (!id || !ref) {
      return;
    }
    triangleToId.set(key, id);
    const refs = idToTriangles.get(id) ?? [];
    refs.push(ref);
    idToTriangles.set(id, refs);
  });
  return { root, triangleColors, triangleToId, idToTriangles };
}

function captureTriangleColors(root: THREE.Object3D) {
  const colors = new Map<string, TriangleColorRecord>();
  root.traverse((child) => {
    if (!isMesh(child) || !child.geometry.attributes.position) {
      return;
    }
    const geometry = child.geometry;
    const position = geometry.attributes.position;
    const triangleCount = geometry.index ? geometry.index.count / 3 : position.count / 3;
    for (let triangleIndex = 0; triangleIndex < triangleCount; triangleIndex += 1) {
      const rgb = readTriangleRgb(child, triangleIndex);
      colors.set(triangleKey(child, triangleIndex), {
        rgb,
        hex: rgbToHex(rgb),
      });
    }
  });
  return colors;
}

function readTriangleRgb(mesh: THREE.Mesh, triangleIndex: number): [number, number, number] {
  const colorAttr = mesh.geometry.attributes.color;
  const face = triangleFaceFromGeometry(mesh.geometry, triangleIndex);
  if (colorAttr) {
    const color = new THREE.Color(0, 0, 0);
    for (const index of [face.a, face.b, face.c]) {
      color.r += colorAttr.getX(index);
      color.g += colorAttr.getY(index);
      color.b += colorAttr.getZ(index);
    }
    color.r /= 3;
    color.g /= 3;
    color.b /= 3;
    return colorToRgb(color);
  }
  const material = Array.isArray(mesh.material)
    ? mesh.material[face.materialIndex ?? 0]
    : mesh.material;
  const materialColor = hasColor(material) ? material.color.clone() : new THREE.Color(0xffffff);
  return colorToRgb(materialColor);
}

function applyBaseMaterials(model: ViewerModel, overrides: Record<string, string>) {
  model.root.traverse((child) => {
    if (!isMesh(child)) {
      return;
    }
    const geometry = child.geometry;
    const position = geometry.attributes.position;
    const triangleCount = geometry.index ? geometry.index.count / 3 : position.count / 3;
    const colors: number[] = [];
    for (let triangleIndex = 0; triangleIndex < triangleCount; triangleIndex += 1) {
      const key = triangleKey(child, triangleIndex);
      const id = model.triangleToId.get(key);
      const override = id ? overrides[id] : null;
      const base = override ? new THREE.Color(override) : new THREE.Color(0xd8ddd7);
      for (let i = 0; i < 3; i += 1) {
        colors.push(base.r, base.g, base.b);
      }
    }
    const nextGeometry = geometry.index ? geometry.toNonIndexed() : geometry;
    if (nextGeometry !== geometry) {
      child.geometry = nextGeometry;
      geometry.dispose();
    }
    child.geometry.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
    child.geometry.attributes.color.needsUpdate = true;
    child.material = new THREE.MeshStandardMaterial({
      vertexColors: true,
      roughness: 0.68,
      metalness: 0.02,
      side: THREE.DoubleSide,
    });
  });
}

function applyDisplayOpacity(root: THREE.Object3D, mode: DisplayMode) {
  const opacity = mode === "isolate-selected" ? 0.08 : mode === "dim-unselected" ? 0.22 : 1;
  root.traverse((child) => {
    if (!isMesh(child)) {
      return;
    }
    const materials = Array.isArray(child.material) ? child.material : [child.material];
    materials.forEach((material) => {
      material.opacity = opacity;
      material.transparent = opacity < 1;
      material.depthWrite = opacity >= 1;
      material.needsUpdate = true;
    });
  });
}

function createTrianglesHighlight(triangles: TriangleRef[], color: string, opacity: number) {
  const vertices: number[] = [];
  for (const triangle of triangles) {
    appendTriangleVertices(vertices, triangle.object, triangle.triangleIndex);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(vertices, 3));
  geometry.computeVertexNormals();
  const mesh = new THREE.Mesh(
    geometry,
    new THREE.MeshBasicMaterial({
      color,
      depthTest: false,
      opacity,
      transparent: true,
      side: THREE.DoubleSide,
    }),
  );
  mesh.renderOrder = 10;
  mesh.userData.isSelectionOverlay = true;
  return mesh;
}

function appendTriangleVertices(vertices: number[], object: THREE.Mesh, triangleIndex: number) {
  const position = object.geometry.attributes.position;
  const face = triangleFaceFromGeometry(object.geometry, triangleIndex);
  object.updateWorldMatrix(true, false);
  for (const index of [face.a, face.b, face.c]) {
    const point = new THREE.Vector3(position.getX(index), position.getY(index), position.getZ(index));
    point.applyMatrix4(object.matrixWorld);
    vertices.push(point.x, point.y, point.z);
  }
}

function triangleFaceFromGeometry(geometry: THREE.BufferGeometry, triangleIndex: number) {
  if (geometry.index) {
    return {
      a: geometry.index.getX(triangleIndex * 3),
      b: geometry.index.getX(triangleIndex * 3 + 1),
      c: geometry.index.getX(triangleIndex * 3 + 2),
      materialIndex: materialIndexForTriangle(geometry, triangleIndex),
    };
  }
  return {
    a: triangleIndex * 3,
    b: triangleIndex * 3 + 1,
    c: triangleIndex * 3 + 2,
    materialIndex: materialIndexForTriangle(geometry, triangleIndex),
  };
}

function materialIndexForTriangle(geometry: THREE.BufferGeometry, triangleIndex: number) {
  const drawIndex = triangleIndex * 3;
  const group = geometry.groups.find((item) => drawIndex >= item.start && drawIndex < item.start + item.count);
  return group && Number.isInteger(group.materialIndex) ? group.materialIndex : 0;
}

const meshRegistry = new Map<string, THREE.Mesh>();

function triangleKey(mesh: THREE.Mesh, triangleIndex: number) {
  meshRegistry.set(mesh.uuid, mesh);
  return `${mesh.uuid}:${triangleIndex}`;
}

function triangleRefFromKey(key: string): TriangleRef | null {
  const separator = key.lastIndexOf(":");
  if (separator <= 0) {
    return null;
  }
  const object = meshRegistry.get(key.slice(0, separator));
  const triangleIndex = Number(key.slice(separator + 1));
  if (!object || !Number.isFinite(triangleIndex)) {
    return null;
  }
  return { object, triangleIndex };
}

function fitCameraToObject(camera: THREE.PerspectiveCamera, controls: OrbitControls, object: THREE.Object3D) {
  const box = new THREE.Box3().setFromObject(object);
  const size = box.getSize(new THREE.Vector3());
  const center = box.getCenter(new THREE.Vector3());
  const maxDim = Math.max(size.x, size.y, size.z, 1);
  const distance = Math.abs(maxDim / Math.sin((camera.fov * Math.PI) / 360)) * 0.72;
  const direction = new THREE.Vector3(1.2, 0.8, 1.15).normalize();
  camera.near = Math.max(distance / 100, 0.01);
  camera.far = distance * 100;
  camera.position.copy(center).add(direction.multiplyScalar(distance));
  camera.lookAt(center);
  camera.updateProjectionMatrix();
  controls.target.copy(center);
  controls.update();
}

function colorToRgb(color: THREE.Color): [number, number, number] {
  const displayColor = color.clone();
  displayColor.convertLinearToSRGB();
  return [
    Math.round(THREE.MathUtils.clamp(displayColor.r, 0, 1) * 255),
    Math.round(THREE.MathUtils.clamp(displayColor.g, 0, 1) * 255),
    Math.round(THREE.MathUtils.clamp(displayColor.b, 0, 1) * 255),
  ];
}

function disposeObject3D(object: THREE.Object3D | null) {
  object?.traverse((child) => {
    if (!isMesh(child)) {
      return;
    }
    child.geometry.dispose();
    const materials = Array.isArray(child.material) ? child.material : [child.material];
    materials.forEach((material) => material.dispose());
  });
}

function isMesh(value: unknown): value is THREE.Mesh {
  return value instanceof THREE.Mesh;
}

function isModelMesh(value: THREE.Object3D) {
  return isMesh(value) && !value.userData.isSelectionOverlay;
}

function hasColor(material: THREE.Material | undefined): material is THREE.Material & { color: THREE.Color } {
  return Boolean(material && "color" in material && material.color instanceof THREE.Color);
}

function toggleString(values: string[], value: string) {
  return values.includes(value) ? values.filter((item) => item !== value) : [...values, value];
}

function toggleSingleString(values: string[], value: string) {
  return values.length === 1 && values[0] === value ? [] : [value];
}

function toMessage(err: unknown) {
  return err instanceof Error ? err.message : String(err);
}
