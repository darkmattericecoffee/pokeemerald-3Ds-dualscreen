import { CircleCheck, Wrench } from "lucide-react";
import * as React from "react";
import { Button } from "@/components/ui/button";
import { useReview } from "@/lib/queries";
import type { Mon, ReviewStatus } from "@/lib/types";
import { ago, cn, when } from "@/lib/utils";

export const REVIEW_LABELS: Record<ReviewStatus, string> = { fix: "Needs work", ok: "Good enough" };

/**
 * The review before the beta, one per picture for everybody (the console's button beside SAVE sets
 * the same): "Needs work" must be redone before the beta, "Good enough" is refined along the way.
 * Pressing the lit one clears it. F and G do the same from the keyboard.
 */
export function ReviewBar({ mon }: { mon: Mon }) {
  const review = useReview(mon.key);
  const status = mon.review?.status ?? "";
  const toggle = React.useCallback(
    (s: ReviewStatus) => review.mutate(status === s ? "" : s),
    [review, status],
  );

  React.useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (e.altKey || e.ctrlKey || e.metaKey || ["INPUT", "TEXTAREA", "SELECT"].includes(t.tagName)) return;
      if (e.key === "f" || e.key === "F") toggle("fix");
      if (e.key === "g" || e.key === "G") toggle("ok");
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [toggle]);

  return (
    <div
      className={cn(
        "panel flex flex-wrap items-center gap-x-3 gap-y-2 p-2.5 sm:p-3",
        status === "fix" && "bg-[color-mix(in_srgb,var(--primary)_10%,var(--card))]",
        status === "ok" && "bg-[color-mix(in_srgb,var(--success)_12%,var(--card))]",
      )}
    >
      <span className="font-pixel text-[10px] uppercase text-muted-foreground">Before the beta</span>
      <div className="flex gap-2">
        <Button
          size="sm"
          variant={status === "fix" ? "default" : "outline"}
          aria-pressed={status === "fix"}
          onClick={() => toggle("fix")}
          title={status === "fix" ? "Clear the mark (F)" : "Must be redone before the beta (F)"}
          className={cn(status === "fix" && "translate-y-0.5 shadow-none")}
        >
          <Wrench /> {REVIEW_LABELS.fix}
        </Button>
        <Button
          size="sm"
          variant={status === "ok" ? "success" : "outline"}
          aria-pressed={status === "ok"}
          onClick={() => toggle("ok")}
          title={status === "ok" ? "Clear the mark (G)" : "Good enough for now, refined along the way (G)"}
          className={cn(status === "ok" && "translate-y-0.5 shadow-none")}
        >
          <CircleCheck /> {REVIEW_LABELS.ok}
        </Button>
      </div>
      <span className="text-xs font-semibold text-muted-foreground">
        {mon.review ? (
          <>
            marked by {mon.review.by ?? "someone"} <span title={when(mon.review.at)}>{ago(mon.review.at)}</span>
          </>
        ) : (
          "not judged yet"
        )}
        <span className="hidden sm:inline"> · keys F and G</span>
      </span>
    </div>
  );
}
