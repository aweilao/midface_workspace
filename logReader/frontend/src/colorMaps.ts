import { ColorMapProfile, ColorMapRecord, LogEntry } from "./types";

export const COLOR_MAP_PROFILES: ColorMapProfile[] = [
  {
    id: "step1-face",
    label: "Step1 face_id",
    tags: ["step1", "face-analyze", "color-map"],
    idProperty: "face_id",
  },
  {
    id: "step2-group",
    label: "Step2 group_id",
    tags: ["step2", "grouping", "color-map"],
    idProperty: "group_id",
  },
  {
    id: "step3-pair",
    label: "Step3 pair_id",
    tags: ["step3", "pairing", "color-map"],
    idProperty: "pair_id",
  },
  {
    id: "step3-wall",
    label: "Step3 wall_id",
    tags: ["step3", "pairing", "wall", "color-map"],
    idProperty: "wall_id",
  },
  {
    id: "step6-selection",
    label: "Step6 selection_id",
    tags: ["step6", "trim-select", "selection", "color-map"],
    idProperty: "selection_id",
  },
];

export function extractColorMaps(logs: LogEntry[], profiles = COLOR_MAP_PROFILES): ColorMapRecord[] {
  const records: ColorMapRecord[] = [];
  for (const log of logs) {
    for (const profile of profiles) {
      if (!hasAllTags(log, profile.tags)) {
        continue;
      }
      const idValue = log.properties[profile.idProperty];
      const rgb = readRgb(log.properties.rgb ?? log.properties.color_rgb);
      if (idValue === undefined || !rgb) {
        continue;
      }
      records.push({
        profileId: profile.id,
        id: formatLookupValue(idValue),
        rgb,
        hex: rgbToHex(rgb),
        log,
      });
    }
  }
  return records;
}

export function hasAllTags(log: LogEntry, tags: string[]) {
  return tags.every((tag) => log.tags.includes(tag));
}

export function readRgb(value: unknown): [number, number, number] | null {
  if (Array.isArray(value) && value.length >= 3) {
    const rgb = value.slice(0, 3).map((item) => Number(item));
    if (rgb.every((item) => Number.isFinite(item))) {
      return [clampByte(rgb[0]), clampByte(rgb[1]), clampByte(rgb[2])];
    }
  }
  if (typeof value === "string") {
    const hex = value.trim().match(/^#?([0-9a-f]{6})$/i);
    if (hex) {
      const raw = hex[1];
      return [
        parseInt(raw.slice(0, 2), 16),
        parseInt(raw.slice(2, 4), 16),
        parseInt(raw.slice(4, 6), 16),
      ];
    }
    const nums = value.match(/\d+(?:\.\d+)?/g)?.map((item) => Number(item)) ?? [];
    if (nums.length >= 3 && nums.slice(0, 3).every((item) => Number.isFinite(item))) {
      return [clampByte(nums[0]), clampByte(nums[1]), clampByte(nums[2])];
    }
  }
  return null;
}

export function rgbKey(rgb: [number, number, number]) {
  return rgb.join(",");
}

export function rgbToHex(rgb: [number, number, number]) {
  return `#${rgb.map((value) => clampByte(value).toString(16).padStart(2, "0")).join("")}`;
}

export function formatLookupValue(value: unknown): string {
  if (value === undefined || value === null) {
    return "";
  }
  if (Array.isArray(value)) {
    return value.map(formatLookupValue).join(",");
  }
  if (typeof value === "object") {
    return JSON.stringify(value);
  }
  return String(value);
}

export function clampByte(value: number) {
  return Math.max(0, Math.min(255, Math.round(value)));
}

export function findColorMapId(
  colorToId: Map<string, string>,
  rgb: [number, number, number],
  tolerance = 2,
) {
  const direct = colorToId.get(rgbKey(rgb));
  if (direct) {
    return direct;
  }
  for (const [key, id] of colorToId) {
    const parts = key.split(",").map((item) => Number(item));
    if (parts.length !== 3) {
      continue;
    }
    const distance = Math.max(
      Math.abs(parts[0] - rgb[0]),
      Math.abs(parts[1] - rgb[1]),
      Math.abs(parts[2] - rgb[2]),
    );
    if (distance <= tolerance) {
      return id;
    }
  }
  return null;
}
