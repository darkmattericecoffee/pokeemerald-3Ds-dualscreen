import { DEPTHS, depthLabel } from "@/lib/depth";
import type { Depth } from "@/lib/types";
import { cn } from "@/lib/utils";

/** One to three stacked bars: shallow, standard, deep (hollow when the version does not say). */
export function DepthDots({ depth, className }: { depth: Depth | null; className?: string }) {
  const n = depth ?? 2;
  return (
    <span className={cn("inline-flex items-end gap-0.5", className)} title={`Depth: ${depthLabel(depth)}`} aria-label={`Depth ${depthLabel(depth)}`}>
      {[1, 2, 3].map((i) => (
        <span
          key={i}
          className={cn(
            "w-1 rounded-sm border border-border",
            i === 1 ? "h-1.5" : i === 2 ? "h-2.5" : "h-3.5",
            i <= n ? (depth ? "bg-sky-500" : "bg-sky-200") : "bg-transparent opacity-40",
          )}
        />
      ))}
    </span>
  );
}

export function DepthBadge({ depth }: { depth: Depth | null }) {
  return (
    <span
      className="inline-flex items-center gap-1.5 rounded-full border-2 border-hairline bg-card px-2 py-0.5 text-[11px] font-extrabold uppercase tracking-wide text-muted-foreground"
      title={depth ? `Depth sweet spot: ${DEPTHS[depth].label}, ${DEPTHS[depth].px} px` : "No depth sweet spot saved: shown at Standard"}
    >
      <DepthDots depth={depth} />
      {depth ? DEPTHS[depth].label : "Std?"}
    </span>
  );
}
