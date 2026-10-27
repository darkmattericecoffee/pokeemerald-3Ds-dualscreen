import { CircleCheck, Search, Star, Users2, Wrench, X } from "lucide-react";
import * as React from "react";
import { Link, useSearchParams } from "react-router";
import { Input } from "@/components/ui/input";
import { Skeleton } from "@/components/ui/skeleton";
import { Segmented } from "@/components/ui/toggle-group";
import { useLive } from "@/lib/live";
import { postFocus, useGrid } from "@/lib/queries";
import type { Grid, GridItem, ReviewStatus } from "@/lib/types";
import { cn, shownNumber } from "@/lib/utils";

type Kind = "front" | "back" | "other";
type Show = "all" | "open" | "done" | "mine" | "voted";
type ReviewShow = "any" | ReviewStatus | "unjudged";

function keysFor(grid: Grid, kind: Kind): string[] {
  const fronts = Array.from({ length: grid.dex_count }, (_, i) => String(i + 1).padStart(3, "0"));
  if (kind === "front") return fronts;
  if (kind === "back") return fronts.map((k) => "b" + k);
  return grid.others;
}

const Tile = React.memo(function Tile({
  k,
  name,
  item,
  picture,
  fresh,
  review,
}: {
  k: string;
  name: string;
  item: GridItem | undefined;
  picture: boolean;
  fresh: boolean;
  review: ReviewStatus | undefined;
}) {
  return (
    <Link
      to={`/mon/${k}`}
      className={cn(
        "group relative flex flex-col items-center rounded-xl border-2 bg-card px-1.5 pb-2 pt-1.5 text-center transition-transform hover:-translate-y-0.5 focus-visible:-translate-y-0.5",
        item ? "border-border shadow-chunky-sm" : "border-dashed border-hairline",
        review === "fix" && "border-solid border-primary",
        review === "ok" && "border-solid border-success",
        item?.final && "bg-[linear-gradient(180deg,color-mix(in_srgb,var(--accent)_30%,var(--card)),var(--card)_55%)]",
        fresh && "animate-fresh",
      )}
    >
      <span className="self-start font-pixel text-[9px] text-muted-foreground">{shownNumber(k)}</span>
      <div className="relative -mt-1 grid size-[72px] place-items-center">
        {picture ? (
          <img
            src={`/img/sprite/${k}.png`}
            alt=""
            width={64}
            height={64}
            loading="lazy"
            decoding="async"
            className={cn(
              "pixelated size-16 transition-[filter,opacity]",
              !item && "opacity-35 brightness-0 group-hover:opacity-60 dark:invert dark:opacity-25",
            )}
          />
        ) : (
          <span className="font-pixel text-lg text-muted-foreground/50">?</span>
        )}
      </div>
      <span className="line-clamp-1 w-full text-xs font-extrabold">{name}</span>
      {item ? (
        <span className="mt-0.5 line-clamp-1 w-full text-[11px] font-semibold text-muted-foreground">{item.author}</span>
      ) : (
        <span className="mt-0.5 text-[11px] font-semibold text-muted-foreground/70">not done</span>
      )}
      {item?.final && (
        <Star className="absolute right-1.5 top-1.5 size-3.5 fill-accent text-border" aria-label="final" />
      )}
      {review && (
        <span
          className={cn(
            "absolute -left-1.5 -top-2 grid size-5 place-items-center rounded-full border-2 border-border text-white",
            review === "fix" ? "bg-primary" : "bg-success",
          )}
          title={review === "fix" ? "Needs work before the beta" : "Good enough for the beta"}
        >
          {review === "fix" ? <Wrench className="size-3" /> : <CircleCheck className="size-3" />}
        </span>
      )}
      {item && item.versions > 1 && (
        <span
          className="absolute -bottom-2 -right-1.5 grid h-5 min-w-5 place-items-center rounded-full border-2 border-border bg-primary px-1 text-[10px] font-black text-white"
          title={`${item.versions} versions`}
        >
          {item.versions}
        </span>
      )}
    </Link>
  );
});

function Stat({ label, done, total, tone }: { label: string; done: number; total: number; tone: string }) {
  const pct = total ? Math.round((done / total) * 100) : 0;
  return (
    <div className="panel flex min-w-0 flex-col gap-2 p-3 sm:p-4">
      <div className="flex items-baseline justify-between gap-2">
        <span className="font-pixel text-[10px] uppercase text-muted-foreground">{label}</span>
        <span className="text-xs font-extrabold text-muted-foreground">{pct}%</span>
      </div>
      <div className="text-2xl font-black tabular-nums sm:text-3xl">
        {done}
        <span className="text-base font-extrabold text-muted-foreground"> / {total}</span>
      </div>
      <div className="h-3 overflow-hidden rounded-full border-2 border-border bg-muted">
        <div className={cn("h-full rounded-full transition-[width] duration-700", tone)} style={{ width: `${pct}%` }} />
      </div>
    </div>
  );
}

export function ProgressPage() {
  const { data: grid, isLoading, error } = useGrid();
  const [params, setParams] = useSearchParams();
  const kind = (params.get("kind") as Kind) || "front";
  const show = (params.get("show") as Show) || "all";
  const reviewShow = (params.get("review") as ReviewShow) || "any";
  const [query, setQuery] = React.useState(params.get("q") ?? "");
  const { fresh } = useLive();
  const set = (k: string, v: string, dflt: string) => {
    const next = new URLSearchParams(params);
    if (v === dflt) next.delete(k);
    else next.set(k, v);
    setParams(next, { replace: true });
  };

  React.useEffect(() => postFocus(null), []);

  const stats = React.useMemo(() => {
    if (!grid) return null;
    const count = (ks: string[]) => ks.filter((k) => grid.items[k]).length;
    const fronts = keysFor(grid, "front");
    const backs = keysFor(grid, "back");
    const all = Object.values(grid.items);
    return {
      fronts: count(fronts),
      backs: count(backs),
      total: grid.dex_count,
      final: all.filter((i) => i.final).length,
      done: all.length,
      versions: all.reduce((n, i) => n + i.versions, 0),
      contested: all.filter((i) => i.versions > 1).length,
      fix: Object.values(grid.reviews).filter((r) => r.status === "fix").length,
      ok: Object.values(grid.reviews).filter((r) => r.status === "ok").length,
      // Every picture that can be judged: the fronts, the backs and the trainers' and players' pictures.
      judgeable: grid.dex_count * 2 + grid.others.length,
    };
  }, [grid]);

  const tiles = React.useMemo(() => {
    if (!grid) return [];
    const mine = new Set(grid.mine);
    const voted = new Set(grid.voted);
    const q = query.trim().toLowerCase().replace(/^#/, "");
    return keysFor(grid, kind).filter((k) => {
      const it = grid.items[k];
      if (show === "open" && it) return false;
      if (show === "done" && !it) return false;
      if (show === "mine" && !mine.has(k)) return false;
      if (show === "voted" && !voted.has(k)) return false;
      const r = grid.reviews[k]?.status;
      if (reviewShow === "unjudged" ? r : reviewShow !== "any" && r !== reviewShow) return false;
      if (q) {
        const num = k.replace(/^[btp]/, "");
        if (!(grid.names[k] ?? "").toLowerCase().includes(q) && !num.includes(q.padStart(Math.min(3, q.length), "0")) && String(Number(num)) !== q)
          return false;
      }
      return true;
    });
  }, [grid, kind, show, reviewShow, query]);

  const pictures = React.useMemo(() => new Set(grid?.pictures ?? []), [grid]);

  if (error) return <p className="panel p-5 font-bold">Could not load the progress: {String((error as Error).message)}</p>;

  return (
    <div className="grid gap-5 sm:gap-6">
      <section className="grid grid-cols-2 gap-3 lg:grid-cols-5">
        {stats ? (
          <>
            <Stat label="Fronts" done={stats.fronts} total={stats.total} tone="bg-primary" />
            <Stat label="Backs" done={stats.backs} total={stats.total} tone="bg-sky-500" />
            <div className="panel flex flex-col gap-2 p-3 sm:p-4">
              <span className="font-pixel text-[10px] uppercase text-muted-foreground">Final</span>
              <div className="flex items-center gap-2 text-2xl font-black tabular-nums sm:text-3xl">
                <Star className="size-6 fill-accent text-border" /> {stats.final}
                <span className="text-base font-extrabold text-muted-foreground">/ {stats.done}</span>
              </div>
              <span className="text-xs font-semibold text-muted-foreground">current versions marked final</span>
            </div>
            <div className="panel flex flex-col gap-2 p-3 sm:p-4">
              <span className="font-pixel text-[10px] uppercase text-muted-foreground">To vote on</span>
              <div className="text-2xl font-black tabular-nums sm:text-3xl">
                {stats.contested}
                <span className="text-base font-extrabold text-muted-foreground"> pictures</span>
              </div>
              <span className="text-xs font-semibold text-muted-foreground">
                with two or more versions · {stats.versions} versions in all
              </span>
            </div>
            <Link
              to={`/?${new URLSearchParams({ ...(kind !== "front" && { kind }), review: "fix" })}`}
              className="panel col-span-2 flex flex-col gap-2 p-3 transition-transform hover:-translate-y-0.5 sm:p-4 lg:col-span-1"
              title="Show the pictures that need work"
            >
              <span className="font-pixel text-[10px] uppercase text-muted-foreground">Before the beta</span>
              <div className="flex items-center gap-2 text-2xl font-black tabular-nums sm:text-3xl">
                <Wrench className="size-6 text-primary" /> {stats.fix}
                <span className="text-base font-extrabold text-muted-foreground"> to redo</span>
              </div>
              <span className="text-xs font-semibold text-muted-foreground">
                {stats.ok} good enough · {stats.judgeable - stats.fix - stats.ok} not judged
              </span>
            </Link>
          </>
        ) : (
          [0, 1, 2, 3, 4].map((i) => <Skeleton key={i} className="h-28 rounded-xl" />)
        )}
      </section>

      <section className="flex flex-wrap items-center gap-2 sm:gap-3">
        <Segmented<Kind>
          label="Which pictures"
          value={kind}
          onChange={(v) => set("kind", v, "front")}
          options={[
            { value: "front", label: "Fronts" },
            { value: "back", label: "Backs" },
            { value: "other", label: <>Trainers<span className="hidden sm:inline">&nbsp;&amp; more</span></> },
          ]}
        />
        <Segmented<Show>
          label="Filter"
          size="sm"
          value={show}
          onChange={(v) => set("show", v, "all")}
          options={[
            { value: "all", label: "All" },
            { value: "open", label: "Not done" },
            { value: "done", label: "Done" },
            { value: "mine", label: "Mine" },
            { value: "voted", label: <><Users2 className="!size-3.5" />Voted</>, title: "Pictures you voted on" },
          ]}
        />
        <Segmented<ReviewShow>
          label="Beta review"
          size="sm"
          value={reviewShow}
          onChange={(v) => set("review", v, "any")}
          options={[
            { value: "any", label: "Any mark", title: "Judged or not" },
            { value: "fix", label: <><Wrench className="!size-3.5" />Needs work</>, title: "Must be redone before the beta" },
            { value: "ok", label: <><CircleCheck className="!size-3.5" />Good enough</>, title: "Refined along the way" },
            { value: "unjudged", label: "Not judged" },
          ]}
        />
        <div className="relative ml-auto w-full sm:w-56">
          <Search className="pointer-events-none absolute left-3 top-1/2 size-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            value={query}
            onChange={(e) => {
              setQuery(e.target.value);
              set("q", e.target.value, "");
            }}
            placeholder="Name or number"
            aria-label="Search by name or number"
            className="pl-9 pr-9"
          />
          {query && (
            <button
              className="absolute right-2 top-1/2 grid size-6 -translate-y-1/2 place-items-center rounded-md hover:bg-muted"
              onClick={() => {
                setQuery("");
                set("q", "", "");
              }}
              aria-label="Clear the search"
            >
              <X className="size-4" />
            </button>
          )}
        </div>
      </section>

      {isLoading || !grid ? (
        <div className="grid grid-cols-[repeat(auto-fill,minmax(100px,1fr))] gap-3">
          {Array.from({ length: 24 }, (_, i) => (
            <Skeleton key={i} className="h-[136px] rounded-xl" />
          ))}
        </div>
      ) : tiles.length === 0 ? (
        <p className="panel p-6 text-center font-bold text-muted-foreground">Nothing here yet.</p>
      ) : (
        <div className="grid grid-cols-[repeat(auto-fill,minmax(100px,1fr))] gap-3 sm:gap-3.5">
          {tiles.map((k) => (
            <Tile
              key={k}
              k={k}
              name={grid.names[k] ?? k}
              item={grid.items[k]}
              picture={pictures.has(k)}
              fresh={fresh.has(k)}
              review={grid.reviews[k]?.status}
            />
          ))}
        </div>
      )}
    </div>
  );
}
