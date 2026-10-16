import { cn } from "@/lib/utils";

/** The dex's big lens and its three little lights. */
export function Lens({ className, live }: { className?: string; live?: "live" | "offline" | "connecting" }) {
  return (
    <div className={cn("flex items-start gap-1.5", className)}>
      <div className="relative grid size-10 place-items-center rounded-full border-[3px] border-border bg-white shadow-chunky-sm sm:size-12">
        <div className="size-7 rounded-full border-2 border-border bg-[radial-gradient(circle_at_35%_30%,#e0f2fe,var(--lens)_40%,#0369a1)] sm:size-8" />
        <div className="absolute left-[30%] top-[26%] size-2 rounded-full bg-white/90" />
      </div>
      <div className="mt-0.5 flex gap-1" aria-hidden>
        <span className="size-2.5 rounded-full border-2 border-border bg-red-400" />
        <span className="size-2.5 rounded-full border-2 border-border bg-yellow-300" />
        <span
          className={cn(
            "size-2.5 rounded-full border-2 border-border",
            live === "live" ? "bg-emerald-400" : live === "offline" ? "bg-stone-400" : "bg-emerald-200",
          )}
        />
      </div>
    </div>
  );
}

export function Wordmark({ className }: { className?: string }) {
  return (
    <div className={cn("leading-none", className)}>
      <div className="font-pixel text-[11px] text-white drop-shadow-[2px_2px_0_var(--primary-deep)] sm:text-sm">
        EMERALD <span className="text-accent">3D</span>
      </div>
      <div className="mt-1 text-[11px] font-extrabold uppercase tracking-[0.2em] text-white/85">Plane Lab</div>
    </div>
  );
}
