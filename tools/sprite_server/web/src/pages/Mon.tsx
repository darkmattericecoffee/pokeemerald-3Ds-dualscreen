import {
  ArrowLeft,
  ArrowLeftRight,
  ChevronLeft,
  ChevronRight,
  Columns3,
  Download,
  Eye,
  Gamepad2,
  MoreHorizontal,
  Pin,
  PinOff,
  Star,
  Trash2,
  X,
} from "lucide-react";
import * as React from "react";
import { Link, useNavigate, useParams } from "react-router";
import { EditPixels, PicturePixels } from "@/components/Pixels";
import { ViewControls } from "@/components/ViewControls";
import { VoteButton } from "@/components/VoteButton";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Confirm } from "@/components/ui/dialog";
import { DropdownContent, DropdownItem, DropdownMenu, DropdownSeparator, DropdownTrigger } from "@/components/ui/dropdown";
import { Skeleton } from "@/components/ui/skeleton";
import { Tooltip } from "@/components/ui/tooltip";
import { frameCount, type EditFile, type Picture } from "@/lib/editfile";
import { useLive } from "@/lib/live";
import { postFocus, useMon, useMonAction, useVote } from "@/lib/queries";
import type { Mon, Version } from "@/lib/types";
import { useVersionFiles } from "@/lib/useVersions";
import { ago, cn, plural, shownNumber, when } from "@/lib/utils";
import { Viewer3D } from "@/three/LazyViewer";
import { defaultSettings, useFrameIndex, type ViewSettings } from "@/three/settings";

export const MAX_COMPARE = 4;

/** View settings kept for the visit (so going to the next Pokémon keeps them). */
export function useViewSettings(): [ViewSettings, (s: ViewSettings) => void] {
  const [s, set] = React.useState<ViewSettings>(() => {
    try {
      return { ...defaultSettings, ...JSON.parse(sessionStorage.getItem("view") ?? "{}") };
    } catch {
      return defaultSettings;
    }
  });
  return [
    s,
    (next) => {
      set(next);
      try {
        sessionStorage.setItem("view", JSON.stringify(next));
      } catch {
        /* fine */
      }
    },
  ];
}

export function MonHeader({ mon, compare }: { mon: Mon; compare?: boolean }) {
  const base = compare ? "/compare/" : "/mon/";
  return (
    <div className="flex flex-wrap items-center gap-x-4 gap-y-3">
      <div className="flex min-w-0 items-center gap-3">
        <Button asChild variant="outline" size="icon-sm" aria-label={compare ? "Back to the Pokémon" : "Back to the progress"}>
          <Link to={compare ? `/mon/${mon.key}` : "/"}>
            <ArrowLeft />
          </Link>
        </Button>
        <h1 className="flex min-w-0 items-baseline gap-2.5">
          <span className="font-pixel text-xs text-primary sm:text-sm">{shownNumber(mon.key)}</span>
          <span className="truncate text-2xl font-black sm:text-3xl">{mon.name}</span>
        </h1>
        <Badge variant="dark" className="capitalize">
          {mon.kind}
        </Badge>
      </div>
      <div className="ml-auto flex items-center gap-1.5">
        {mon.prev && (
          <Button asChild variant="outline" size="sm" title={`Previous: ${mon.prev.name} (←)`}>
            <Link to={base + mon.prev.key}>
              <ChevronLeft />
              <span className="hidden max-w-24 truncate md:inline">{mon.prev.name}</span>
            </Link>
          </Button>
        )}
        {mon.other && (
          <Button asChild variant="secondary" size="sm" title="The other side">
            <Link to={base + mon.other.key}>
              <ArrowLeftRight />
              {mon.other.key.startsWith("b") ? "Back" : "Front"}
            </Link>
          </Button>
        )}
        {mon.next && (
          <Button asChild variant="outline" size="sm" title={`Next: ${mon.next.name} (→)`}>
            <Link to={base + mon.next.key}>
              <span className="hidden max-w-24 truncate md:inline">{mon.next.name}</span>
              <ChevronRight />
            </Link>
          </Button>
        )}
      </div>
    </div>
  );
}

/** ← and → go to the previous and next Pokémon. */
export function useArrowKeys(mon: Mon | undefined, base: string) {
  const navigate = useNavigate();
  React.useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (e.altKey || e.ctrlKey || e.metaKey || ["INPUT", "TEXTAREA", "SELECT"].includes(t.tagName)) return;
      if (e.key === "ArrowLeft" && mon?.prev) navigate(base + mon.prev.key);
      if (e.key === "ArrowRight" && mon?.next) navigate(base + mon.next.key);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [mon, base, navigate]);
}

function Thumb({ edit, picture, look, label }: { edit: EditFile | undefined; picture: Picture | null; look: "tint" | "colour" | "planes"; label: string }) {
  const [hover, setHover] = React.useState(false);
  const frames = edit ? frameCount(edit, picture) : 1;
  const frame = useFrameIndex(hover && frames > 1 ? "anim" : 0, frames);
  return (
    <div
      className="grid size-20 shrink-0 place-items-center rounded-lg border-2 border-border bg-[repeating-conic-gradient(var(--muted)_0_25%,var(--card)_0_50%)] bg-[length:12px_12px] sm:size-24"
      onPointerEnter={() => setHover(true)}
      onPointerLeave={() => setHover(false)}
    >
      {edit ? <EditPixels edit={edit} picture={picture} frame={frame} look={look} className="size-full" label={label} /> : <Skeleton className="size-3/4" />}
    </div>
  );
}

export function VersionMeta({ v, mon }: { v: Version; mon: Mon }) {
  const isCurrent = mon.current === v.version_id;
  return (
    <>
      <div className="flex flex-wrap items-center gap-1.5">
        <span className="font-pixel text-[10px] text-muted-foreground">#{v.version_id}</span>
        <span className="font-black">{v.author}</span>
        {isCurrent && (
          <Badge variant="current">
            {mon.pinned ? <Pin /> : <Gamepad2 />} {mon.pinned ? "Current · pinned" : "Current"}
          </Badge>
        )}
        {v.final && (
          <Badge variant="final">
            <Star className="fill-current" /> Final
          </Badge>
        )}
        {v.mine && <Badge variant="outline">Yours</Badge>}
      </div>
      <div className="mt-1 flex flex-wrap gap-x-3 gap-y-0.5 text-xs font-semibold text-muted-foreground">
        {v.signed && v.signed.toLowerCase() !== v.author.toLowerCase() && <span>signed {v.signed}</span>}
        {v.console && <span>from {v.console}</span>}
        <span title={when(v.uploaded_at)}>{ago(v.uploaded_at)}</span>
        {v.revision > 0 && <span>rev {v.revision}</span>}
        <span>
          {plural(v.planes, "plane")}, {plural(v.frames, "frame")}
        </span>
      </div>
    </>
  );
}

function VersionCard({
  v,
  mon,
  edit,
  picture,
  showing,
  comparing,
  fresh,
  onShow,
  onCompare,
}: {
  v: Version;
  mon: Mon;
  edit: EditFile | undefined;
  picture: Picture | null;
  showing: boolean;
  comparing: boolean;
  fresh: boolean;
  onShow: () => void;
  onCompare: () => void;
}) {
  const vote = useVote(mon.key);
  const act = useMonAction(mon.key);
  const voted = mon.favourites.includes(v.version_id);
  const isCurrent = mon.current === v.version_id;
  const canDelete = mon.admin || v.mine;
  return (
    <Card
      className={cn(
        "cursor-pointer p-3 transition-[border-color,box-shadow] sm:p-4",
        // The version in the 3D view: a red stroke (the border, thickened inside) and a red drop.
        showing
          ? "border-primary shadow-[inset_0_0_0_1px_var(--primary),0_4px_0_0_var(--primary)]"
          : "hover:border-primary/60",
        fresh && "animate-fresh",
      )}
      onClick={(e) => {
        // A click anywhere on the card views it, except on its buttons and links (and on menus
        // and dialogs opened from it, which React bubbles here through their portals).
        const target = e.target as HTMLElement;
        if (!e.currentTarget.contains(target) || target.closest("button, a, [role=menuitem]")) return;
        onShow();
      }}
    >
      <div className="flex gap-3">
        <button onClick={onShow} className="rounded-lg" aria-label={`View version ${v.version_id} in 3D`}>
          <Thumb edit={edit} picture={picture} look="tint" label={`Version ${v.version_id}, tinted by plane`} />
        </button>
        <div className="min-w-0 flex-1">
          <VersionMeta v={v} mon={mon} />
          <div className="mt-1.5 text-xs font-bold">
            {v.voters.length ? (
              <span className="text-muted-foreground">
                <Star className="mr-1 inline size-3 fill-accent text-border" />
                {v.voters.join(", ")}
              </span>
            ) : (
              <span className="text-muted-foreground/70">No votes yet</span>
            )}
          </div>
        </div>
      </div>
      <div className="mt-3 flex flex-wrap items-center gap-2">
        <VoteButton
          voted={voted}
          votes={v.votes}
          onToggle={() => vote.mutate({ versionId: v.version_id, on: !voted })}
          className="basis-full sm:basis-auto"
        />
        <Button
          variant={showing ? "secondary" : "outline"}
          onClick={onShow}
          aria-pressed={showing}
          title="Show this version in the 3D view"
          className="flex-1 sm:flex-none"
        >
          <Eye /> View
        </Button>
        <Button
          variant={comparing ? "success" : "outline"}
          onClick={onCompare}
          aria-pressed={comparing}
          title="Pick it for side by side"
          className="flex-1 sm:flex-none"
        >
          <Columns3 /> {comparing ? "Picked" : "Compare"}
        </Button>
        <DropdownMenu>
          <DropdownTrigger asChild>
            <Button variant="ghost" size="icon" aria-label="More">
              <MoreHorizontal />
            </Button>
          </DropdownTrigger>
          <DropdownContent>
            <DropdownItem asChild>
              <a href={`/dl/v${v.version_id}/${mon.key}.bin`} download>
                <Download /> Download .bin
              </a>
            </DropdownItem>
            {mon.admin && !(isCurrent && mon.pinned) && (
              <DropdownItem onSelect={() => act.mutate({ kind: "pin", versionId: v.version_id })}>
                <Pin /> {isCurrent ? "Pin as current" : "Make current (pin)"}
              </DropdownItem>
            )}
            {mon.admin && isCurrent && mon.pinned && (
              <DropdownItem onSelect={() => act.mutate({ kind: "unpin" })}>
                <PinOff /> Unpin (votes decide)
              </DropdownItem>
            )}
            {canDelete && (
              <>
                <DropdownSeparator />
                <Confirm
                  trigger={
                    <DropdownItem onSelect={(e) => e.preventDefault()} className="text-primary">
                      <Trash2 /> Delete…
                    </DropdownItem>
                  }
                  title="Delete this version?"
                  description={`Version #${v.version_id} by ${v.author} goes for good, with its ${plural(v.votes, "vote")}.`}
                  confirm="Delete"
                  onConfirm={() => act.mutate({ kind: "delete", versionId: v.version_id })}
                />
              </>
            )}
          </DropdownContent>
        </DropdownMenu>
      </div>
    </Card>
  );
}

function CompareTray({ mon, picked, edits, picture, onRemove, onClear }: {
  mon: Mon;
  picked: number[];
  edits: Map<number, EditFile>;
  picture: Picture | null;
  onRemove: (vid: number) => void;
  onClear: () => void;
}) {
  if (picked.length === 0) return null;
  return (
    <div className="pointer-events-none fixed inset-x-0 bottom-0 z-30 px-3 pb-[max(0.75rem,env(safe-area-inset-bottom))]">
      <div className="panel pointer-events-auto mx-auto flex max-w-3xl items-center gap-2 p-2 pl-3 animate-in slide-in-from-bottom-4">
        <div className="flex min-w-0 flex-1 items-center gap-2 overflow-x-auto">
          {picked.map((vid) => {
            const e = edits.get(vid);
            return (
              <span key={vid} className="flex shrink-0 items-center gap-1 rounded-lg border-2 border-border bg-muted py-0.5 pl-0.5 pr-1">
                {e && <EditPixels edit={e} picture={picture} frame={0} look="colour" className="size-8" />}
                <span className="text-xs font-black">#{vid}</span>
                <button onClick={() => onRemove(vid)} aria-label={`Unpick version ${vid}`} className="rounded p-0.5 hover:bg-card">
                  <X className="size-3.5" />
                </button>
              </span>
            );
          })}
        </div>
        <Button variant="ghost" size="sm" onClick={onClear} className="hidden sm:inline-flex">
          Clear
        </Button>
        <Button asChild={picked.length >= 2} disabled={picked.length < 2} title={picked.length < 2 ? "Pick one more" : undefined}>
          {picked.length >= 2 ? (
            <Link to={`/compare/${mon.key}?v=${picked.join(",")}`}>
              <Columns3 /> Compare {picked.length}
            </Link>
          ) : (
            <span>
              <Columns3 /> Pick 2+
            </span>
          )}
        </Button>
      </div>
    </div>
  );
}

export function MonPage() {
  const { key = "" } = useParams();
  const { data: mon, isLoading, error } = useMon(key);
  const [settings, setSettings] = useViewSettings();
  const [chosen, setChosen] = React.useState<number | null>(null);
  const [picked, setPicked] = React.useState<number[]>([]);
  const { fresh } = useLive();
  useArrowKeys(mon, "/mon/");

  // A new Pokémon: start from its current version, nothing picked.
  React.useEffect(() => {
    setChosen(null);
    setPicked([]);
  }, [key]);
  React.useEffect(() => postFocus(key, picked), [key, picked]);

  const ids = mon?.versions.map((v) => v.version_id) ?? [];
  // The version in 3D stays put when votes change which one is current; a click picks another.
  React.useEffect(() => {
    if (mon && mon.key === key && chosen === null && mon.versions.length) setChosen(mon.current ?? mon.versions[0].version_id);
  }, [mon, key, chosen]);
  const files = useVersionFiles(mon, ids);
  const shownId = chosen !== null && ids.includes(chosen) ? chosen : (mon?.current ?? ids[0] ?? null);
  const shown = mon?.versions.find((v) => v.version_id === shownId);
  const shownEdit = shownId !== null ? files.edits.get(shownId) : undefined;
  const frames = shownEdit ? frameCount(shownEdit, files.picture) : (files.picture?.frames.length ?? 2);
  const frame = useFrameIndex(settings.frame, frames);

  if (error) return <p className="panel p-5 font-bold">{(error as Error).message}</p>;
  if (isLoading || !mon) {
    return (
      <div className="grid gap-5 lg:grid-cols-[minmax(0,5fr)_minmax(0,7fr)]">
        <Skeleton className="aspect-square rounded-2xl" />
        <div className="grid content-start gap-3">
          <Skeleton className="h-10 w-2/3" />
          <Skeleton className="h-32" />
          <Skeleton className="h-32" />
        </div>
      </div>
    );
  }

  const togglePick = (vid: number) =>
    setPicked((p) => (p.includes(vid) ? p.filter((x) => x !== vid) : p.length >= MAX_COMPARE ? [...p.slice(1), vid] : [...p, vid]));

  return (
    <div className={cn("grid gap-5", picked.length > 0 && "pb-24")}>
      <MonHeader mon={mon} />

      {mon.versions.length === 0 ? (
        <Card className="grid place-items-center gap-4 p-8 text-center">
          {files.picture ? (
            <PicturePixels picture={files.picture} frame={0} className="size-40 opacity-40 grayscale" label={mon.name} />
          ) : null}
          <p className="font-pixel text-xs uppercase">No planes yet</p>
          <p className="max-w-md text-sm font-semibold text-muted-foreground">
            Nobody has uploaded planes for {mon.name} yet. Paint them in the plane editor on the 3DS and upload them from
            its ONLINE tab; they show up here at once.
          </p>
        </Card>
      ) : (
        <div className="grid items-start gap-5 lg:grid-cols-[minmax(0,5fr)_minmax(0,7fr)]">
          <div className="grid gap-3 lg:sticky lg:top-24">
            <Viewer3D
              edit={shownEdit}
              picture={files.picture}
              frame={frame}
              settings={settings}
              loading={files.loading}
              label={`${mon.name}, version ${shownId} in 3D`}
            >
              {shown && (
                <div className="pointer-events-none absolute left-3 top-3 z-20 flex flex-wrap gap-1.5">
                  <span className="rounded-md bg-black/45 px-2 py-1 text-xs font-black text-white backdrop-blur">
                    #{shown.version_id} · {shown.author}
                  </span>
                  {mon.current === shown.version_id && (
                    <Badge variant="current">
                      <Gamepad2 /> Current
                    </Badge>
                  )}
                </div>
              )}
              {files.picture && (
                <Tooltip content="The game's own picture" side="left">
                  <div className="absolute right-3 top-3 z-20 rounded-lg border-2 border-white/20 bg-white/10 p-1 backdrop-blur">
                    <PicturePixels picture={files.picture} frame={frame} className="size-12 sm:size-14" label="Game picture" />
                  </div>
                </Tooltip>
              )}
            </Viewer3D>
            <Card className="p-3 sm:p-4">
              <ViewControls settings={settings} onChange={setSettings} frames={frames} />
              <p className="mt-3 hidden text-xs font-semibold text-muted-foreground sm:block">
                Drag to turn it (pinch or Ctrl + scroll to zoom). It sways by itself after a moment. Every
                version is drawn at the console's standard depth.
              </p>
            </Card>
          </div>

          <section className="grid gap-3">
            <div className="flex flex-wrap items-center justify-between gap-2">
              <h2 className="font-pixel text-xs uppercase sm:text-sm">
                Versions <span className="text-muted-foreground">({mon.versions.length})</span>
              </h2>
              {mon.versions.length >= 2 && (
                <Button asChild variant="secondary" size="sm">
                  <Link to={`/compare/${mon.key}?v=${ids.slice(0, MAX_COMPARE).join(",")}`}>
                    <Columns3 /> Compare the top {Math.min(MAX_COMPARE, ids.length)}
                  </Link>
                </Button>
              )}
            </div>
            {mon.versions.map((v) => (
              <VersionCard
                key={v.version_id}
                v={v}
                mon={mon}
                edit={files.edits.get(v.version_id)}
                picture={files.picture}
                showing={v.version_id === shownId}
                comparing={picked.includes(v.version_id)}
                fresh={fresh.has(mon.key) && v.version_id === mon.versions[0]?.version_id}
                onShow={() => {
                  setChosen(v.version_id);
                  if (window.matchMedia("(max-width: 1023px)").matches) window.scrollTo({ top: 0, behavior: "smooth" });
                }}
                onCompare={() => togglePick(v.version_id)}
              />
            ))}
            <p className="px-1 text-xs font-semibold text-muted-foreground">
              Vote for as many versions as you like, here or on the console; the front and the back are voted on apart.
              The most votes come first and are current (among equals, the newest), unless an admin pins one. Consoles
              download the current one when they sync.
            </p>
          </section>
        </div>
      )}
      <CompareTray
        mon={mon}
        picked={picked}
        edits={files.edits}
        picture={files.picture}
        onRemove={(vid) => setPicked((p) => p.filter((x) => x !== vid))}
        onClear={() => setPicked([])}
      />
    </div>
  );
}
