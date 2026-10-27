import { Check, Gamepad2, Link2, Plus } from "lucide-react";
import * as React from "react";
import { useParams, useSearchParams } from "react-router";
import { FollowBadge } from "@/components/FollowBadge";
import { EditPixels } from "@/components/Pixels";
import { ViewControls } from "@/components/ViewControls";
import { VoteButton } from "@/components/VoteButton";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Skeleton } from "@/components/ui/skeleton";
import { frameCount } from "@/lib/editfile";
import { postFocus, useMon, useShowOn3ds, useVote } from "@/lib/queries";
import { useVersionFiles } from "@/lib/useVersions";
import { cn } from "@/lib/utils";
import { createOrbit, OrbitContext } from "@/three/orbit";
import { useFrameIndex } from "@/three/settings";
import { Viewer3D } from "@/three/LazyViewer";
import { MAX_COMPARE, MonHeader, useArrowKeys, useViewSettings, VersionMeta } from "./Mon";

/** 2-4 versions side by side; turning one turns them all, so the depth can be judged together. */
export function ComparePage() {
  const { key = "" } = useParams();
  const [params, setParams] = useSearchParams();
  const { data: mon, isLoading, error } = useMon(key);
  const [settings, setSettings] = useViewSettings();
  const orbit = React.useMemo(createOrbit, []);
  const vote = useVote(key);
  const show3ds = useShowOn3ds();
  useArrowKeys(mon, "/compare/");

  const asked = (params.get("v") ?? "")
    .split(",")
    .map(Number)
    .filter((n) => Number.isInteger(n) && n > 0);
  const all = mon?.versions.map((v) => v.version_id) ?? [];
  // Unknown ids (deleted, or another Pokémon's after ← →) fall back to the top versions.
  let ids = asked.filter((id) => all.includes(id)).slice(0, MAX_COMPARE);
  if (mon && ids.length === 0) ids = all.slice(0, Math.min(MAX_COMPARE, all.length));
  const files = useVersionFiles(mon, all);
  const frames = Math.max(1, ...ids.map((id) => {
    const e = files.edits.get(id);
    return e ? frameCount(e, files.picture) : 1;
  }));
  const frame = useFrameIndex(settings.frame, frames);
  const idsKey = ids.join(",");
  React.useEffect(() => postFocus(key, idsKey ? idsKey.split(",").map(Number) : []), [key, idsKey]);

  const toggle = (vid: number) => {
    const next = ids.includes(vid) ? ids.filter((x) => x !== vid) : ids.length >= MAX_COMPARE ? [...ids.slice(1), vid] : [...ids, vid];
    const p = new URLSearchParams(params);
    p.set("v", next.join(","));
    setParams(p, { replace: true });
  };

  if (error) return <p className="panel p-5 font-bold">{(error as Error).message}</p>;
  if (isLoading || !mon) return <Skeleton className="h-96 rounded-2xl" />;

  const cols =
    ids.length <= 1 ? "grid-cols-1 max-w-xl" : ids.length === 2 ? "grid-cols-2" : ids.length === 3 ? "grid-cols-2 lg:grid-cols-3" : "grid-cols-2 xl:grid-cols-4";

  return (
    <div className="grid gap-5">
      <MonHeader mon={mon} compare />

      <Card className="grid gap-3 p-3 sm:p-4">
        <div className="flex flex-wrap items-center gap-2">
          <span className="mr-1 font-pixel text-[10px] uppercase text-muted-foreground">Side by side</span>
          {mon.versions.map((v) => {
            const on = ids.includes(v.version_id);
            const e = files.edits.get(v.version_id);
            return (
              <button
                key={v.version_id}
                onClick={() => toggle(v.version_id)}
                aria-pressed={on}
                className={cn(
                  "flex items-center gap-1.5 rounded-lg border-2 py-1 pl-1 pr-2 text-xs font-black transition-colors",
                  on ? "border-border bg-success text-success-foreground shadow-chunky-sm" : "border-hairline bg-card hover:border-border",
                )}
              >
                {e ? (
                  <EditPixels edit={e} picture={files.picture} frame={0} look="colour" className="size-6" />
                ) : (
                  <span className="size-6" />
                )}
                {v.builtin ? "" : `#${v.version_id} `}{v.author}
                {on ? <Check className="size-3.5" /> : <Plus className="size-3.5" />}
              </button>
            );
          })}
        </div>
        <ViewControls settings={settings} onChange={setSettings} frames={frames} />
        <div className="flex flex-wrap items-center gap-2">
          <p className="mr-auto flex items-center gap-1.5 text-xs font-semibold text-muted-foreground">
            <Link2 className="size-3.5" /> The views move together: drag any of them.
          </p>
          <FollowBadge />
          <Button
            variant="outline"
            size="sm"
            onClick={() => show3ds.mutate({ key, versionIds: ids, what: `these ${ids.length} versions` })}
          >
            <Gamepad2 /> Show on 3DS
          </Button>
        </div>
      </Card>

      <OrbitContext.Provider value={orbit}>
        <div className={cn("mx-auto grid w-full gap-3 sm:gap-4", cols)}>
          {ids.map((vid) => {
            const v = mon.versions.find((x) => x.version_id === vid)!;
            const voted = mon.favourites.includes(vid);
            return (
              <Card key={vid} className="flex min-w-0 flex-col gap-2 p-2 sm:p-3">
                <Viewer3D
                  edit={files.edits.get(vid)}
                  picture={files.picture}
                  frame={frame}
                  settings={settings}
                  loading={files.loading}
                  label={`Version ${vid} by ${v.author} in 3D`}
                />
                <div className="min-w-0 px-1 text-sm">
                  <VersionMeta v={v} mon={mon} />
                </div>
                <VoteButton
                  className="mt-auto w-full"
                  voted={voted}
                  votes={v.votes}
                  onToggle={() => vote.mutate({ versionId: vid, on: !voted })}
                />
              </Card>
            );
          })}
        </div>
      </OrbitContext.Provider>
    </div>
  );
}
