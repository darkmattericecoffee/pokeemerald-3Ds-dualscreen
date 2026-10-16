import { Box, Film, Layers, Pause, Play, SlidersHorizontal } from "lucide-react";
import * as React from "react";
import { Slider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import { Segmented } from "@/components/ui/toggle-group";
import type { Look } from "@/lib/render";
import type { ViewSettings } from "@/three/settings";
import { cn } from "@/lib/utils";

/** The toolbar every 3D view on a page shares: frame, colours, solid or cards, sway, depth exaggeration. */
export function ViewControls({
  settings,
  onChange,
  frames,
  className,
}: {
  settings: ViewSettings;
  onChange: (s: ViewSettings) => void;
  frames: number;
  className?: string;
}) {
  const set = (patch: Partial<ViewSettings>) => onChange({ ...settings, ...patch });
  const frameValue = settings.frame === "anim" ? "anim" : String(settings.frame);
  const [more, setMore] = React.useState(false);
  return (
    <div className={cn("flex flex-wrap items-center gap-x-4 gap-y-3", className)}>
      <Segmented
        label="Frame"
        size="sm"
        value={frameValue}
        onChange={(v) => set({ frame: v === "anim" ? "anim" : Number(v) })}
        options={[
          ...Array.from({ length: Math.max(1, frames) }, (_, i) => ({ value: String(i), label: `Frame ${i + 1}` })),
          ...(frames > 1
            ? [{ value: "anim", label: settings.frame === "anim" ? <Pause /> : <Play />, title: "Play the frames in turn" }]
            : []),
        ]}
      />
      <button
        type="button"
        onClick={() => setMore(!more)}
        aria-expanded={more}
        className="ml-auto inline-flex h-8 items-center gap-1.5 rounded-lg border-2 border-border bg-card px-2.5 text-xs font-extrabold shadow-chunky-sm sm:hidden"
      >
        <SlidersHorizontal className="size-4" /> View options
      </button>
      <div className={cn("contents", !more && "max-sm:hidden")}>
      <Segmented<Look>
        label="Colours"
        size="sm"
        value={settings.look}
        onChange={(look) => set({ look })}
        options={[
          { value: "colour", label: "Colour", title: "The game's colours" },
          { value: "tint", label: "Tint", title: "The game's colours mixed with each plane's editor colour" },
          { value: "planes", label: "Planes", title: "Only the editor's plane colours" },
        ]}
      />
      <Segmented
        label="Shape"
        size="sm"
        value={settings.solid ? "solid" : "cards"}
        onChange={(v) => set({ solid: v === "solid" })}
        options={[
          { value: "solid", label: <><Box />Solid</>, title: "Each plane reaches back to the farthest: no gaps however you turn it" },
          { value: "cards", label: <><Layers />Cards</>, title: "Flat planes at their depths, as the console draws them" },
        ]}
      />
      <label className="flex items-center gap-2 text-xs font-extrabold">
        <Switch checked={settings.sway} onCheckedChange={(sway) => set({ sway })} aria-label="Sway" />
        <Film className="size-4" /> Sway
      </label>
      <label
        className="flex min-w-44 flex-1 items-center gap-3 text-xs font-extrabold sm:max-w-64"
        title="Spread the planes further apart than the console does, to see the layering better"
      >
        <span className="whitespace-nowrap">Depth ×{settings.exaggerate.toFixed(1)}</span>
        <Slider
          aria-label="Depth exaggeration"
          min={0.5}
          max={3}
          step={0.25}
          value={[settings.exaggerate]}
          onValueChange={([v]) => set({ exaggerate: v })}
        />
      </label>
      </div>
    </div>
  );
}
