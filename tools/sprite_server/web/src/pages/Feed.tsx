import { Activity, ArrowDown, CircleCheck, Gamepad2, Pin, PinOff, Star, ThumbsDown, ThumbsUp, Trash2, Upload, UserRoundPen, Wrench } from "lucide-react";
import * as React from "react";
import { Link, useSearchParams } from "react-router";
import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { Segmented } from "@/components/ui/toggle-group";
import { Tooltip } from "@/components/ui/tooltip";
import { useLive } from "@/lib/live";
import { postFocus, useFeed } from "@/lib/queries";
import type { FeedFilter, FeedItem, FeedKind } from "@/lib/types";
import { ago, cn, shownNumber, when } from "@/lib/utils";

const ICONS: Record<FeedKind, { icon: React.ComponentType<{ className?: string }>; tone: string }> = {
  upload: { icon: Upload, tone: "bg-primary text-white" },
  vote: { icon: ThumbsUp, tone: "bg-success text-success-foreground" },
  unvote: { icon: ThumbsDown, tone: "bg-muted text-muted-foreground" },
  pin: { icon: Pin, tone: "bg-foreground text-background" },
  unpin: { icon: PinOff, tone: "bg-muted text-muted-foreground" },
  delete: { icon: Trash2, tone: "bg-muted text-muted-foreground" },
  name: { icon: UserRoundPen, tone: "bg-sky-500 text-white" },
  console: { icon: Gamepad2, tone: "bg-sky-500 text-white" },
  review: { icon: Wrench, tone: "bg-primary text-white" },
};

/** A review line's icon follows what it was set to. */
function iconFor(item: FeedItem) {
  if (item.kind !== "review") return ICONS[item.kind];
  if (item.status === "ok") return { icon: CircleCheck, tone: "bg-success text-success-foreground" };
  if (item.status === "fix") return ICONS.review;
  return { icon: Wrench, tone: "bg-muted text-muted-foreground" };
}

function Who({ item }: { item: FeedItem }) {
  return <span className="font-black">{item.me ? "You" : (item.who ?? "Someone")}</span>;
}

/** The picture's name with its side: "Rayquaza (back) #384", "May (back) P001". */
function Picture({ item }: { item: FeedItem }) {
  if (!item.key) return null;
  const side = item.key.startsWith("b") || item.key.startsWith("p") ? " (back)" : "";
  return (
    <Link to={`/mon/${item.key}`} className="font-black text-primary hover:underline">
      {item.name ?? item.key}
      {side && <span className="font-bold text-muted-foreground">{side}</span>}
      <span className="ml-1 font-pixel text-[9px] text-muted-foreground">{shownNumber(item.key)}</span>
    </Link>
  );
}

/** The sentence of one line: "darkmatter uploaded Pikachu". */
function Sentence({ item }: { item: FeedItem }) {
  const theirs = item.owner && item.owner !== item.who;
  // Whose version a vote, pin or delete was about: someone else's, or one's own.
  const whose = theirs ? `${item.owner}'s version of ` : item.owner || item.own ? "their own version of " : "";
  switch (item.kind) {
    case "upload":
      return (
        <>
          <Who item={item} /> {item.again ? "brought back an earlier version of" : item.base ? `uploaded a version of` : "uploaded"}{" "}
          <Picture item={item} />
          {item.base && <span className="text-muted-foreground"> made over {item.base}'s</span>}
        </>
      );
    case "vote":
      return (
        <>
          <Who item={item} /> voted for {whose}
          <Picture item={item} />
        </>
      );
    case "unvote":
      return (
        <>
          <Who item={item} /> took back a vote on <Picture item={item} />
        </>
      );
    case "pin":
      return (
        <>
          <Who item={item} /> pinned {whose}
          <Picture item={item} /> <span className="text-muted-foreground">as the current one</span>
        </>
      );
    case "unpin":
      return (
        <>
          <Who item={item} /> unpinned <Picture item={item} />{" "}
          <span className="text-muted-foreground">(the votes decide again)</span>
        </>
      );
    case "delete":
      return (
        <>
          <Who item={item} /> deleted {whose}
          <Picture item={item} />
        </>
      );
    case "name":
      return (
        <>
          <Who item={item} /> {item.me ? "are" : "is"} now called <span className="font-black">{item.new_name}</span>
        </>
      );
    case "review":
      return item.status ? (
        <>
          <Who item={item} /> marked <Picture item={item} />{" "}
          <span className={cn("font-black", item.status === "fix" ? "text-primary" : "text-success")}>
            {item.status === "fix" ? "needs work" : "good enough"}
          </span>{" "}
          <span className="text-muted-foreground">for the beta</span>
        </>
      ) : (
        <>
          <Who item={item} /> cleared the beta mark on <Picture item={item} />
        </>
      );
    case "console":
      return (
        <>
          <Who item={item} /> linked a console{item.console ? <span className="text-muted-foreground">, {item.console}</span> : null}
        </>
      );
  }
}

function Thumb({ item }: { item: FeedItem }) {
  if (!item.key) return <div className="grid size-14 shrink-0 place-items-center rounded-lg border-2 border-hairline bg-muted" />;
  const src = item.version_id && item.exists ? `/img/v${item.version_id}/tint.png` : `/img/sprite/${item.key}.png`;
  return (
    <Link
      to={`/mon/${item.key}`}
      className={cn(
        "relative grid size-14 shrink-0 place-items-center rounded-lg border-2 border-border bg-card shadow-chunky-sm transition-transform hover:-translate-y-0.5",
        item.exists === false && "border-dashed border-hairline shadow-none",
      )}
      aria-label={item.name ?? item.key}
    >
      <img
        src={src}
        alt=""
        width={48}
        height={48}
        loading="lazy"
        decoding="async"
        className={cn("pixelated size-12", item.exists === false && "opacity-40 grayscale")}
      />
      {item.final && item.exists && <Star className="absolute -right-1.5 -top-1.5 size-4 fill-accent text-border" aria-label="final" />}
    </Link>
  );
}

function Fate({ item }: { item: FeedItem }) {
  if (item.version_id == null || item.kind === "delete") return null;
  const text = !item.exists ? "deleted since" : item.replaced ? "replaced by a newer one since" : item.current ? "the current version" : null;
  if (!text) return null;
  return (
    <span
      className={cn(
        "rounded-full border-2 px-2 py-0.5 text-[10px] font-extrabold uppercase tracking-wide",
        item.current && item.exists && !item.replaced ? "border-border bg-success text-success-foreground" : "border-hairline bg-card text-muted-foreground",
      )}
    >
      {text}
    </span>
  );
}

const Row = React.memo(function Row({ item, fresh }: { item: FeedItem; fresh: boolean }) {
  const { icon: Icon, tone } = iconFor(item);
  return (
    <li className={cn("panel flex items-center gap-3 p-3 sm:gap-4 sm:p-4", fresh && "animate-fresh")}>
      <span className={cn("grid size-8 shrink-0 place-items-center rounded-full border-2 border-border", tone)} aria-hidden>
        <Icon className="size-4" />
      </span>
      <Thumb item={item} />
      <div className="min-w-0 flex-1">
        <p className="text-sm font-semibold leading-snug [overflow-wrap:anywhere] sm:text-[15px]">
          <Sentence item={item} />
        </p>
        <div className="mt-1 flex flex-wrap items-center gap-x-2 gap-y-1 text-xs font-semibold text-muted-foreground">
          <Tooltip content={when(item.at)}>
            <time dateTime={new Date(item.at * 1000).toISOString()}>{ago(item.at)}</time>
          </Tooltip>
          {item.kind === "upload" && item.revision ? <span>· revision {item.revision}</span> : null}
          <Fate item={item} />
        </div>
      </div>
    </li>
  );
});

/** "Today", "Yesterday", or the date. */
function dayLabel(unix: number, now = new Date()): string {
  const d = new Date(unix * 1000);
  const start = (x: Date) => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime();
  const days = Math.round((start(now) - start(d)) / 86400000);
  if (days === 0) return "Today";
  if (days === 1) return "Yesterday";
  return d.toLocaleDateString(undefined, { weekday: "long", day: "numeric", month: "long", year: days > 300 ? "numeric" : undefined });
}

export function FeedPage() {
  const [params, setParams] = useSearchParams();
  const filter = (params.get("show") as FeedFilter) || "all";
  const feed = useFeed(filter);
  const { fresh } = useLive();
  React.useEffect(() => postFocus(null), []);

  const items = React.useMemo(() => feed.data?.pages.flatMap((p) => p.items) ?? [], [feed.data]);
  const groups = React.useMemo(() => {
    const out: { label: string; items: FeedItem[] }[] = [];
    for (const it of items) {
      const label = dayLabel(it.at);
      const last = out[out.length - 1];
      if (last && last.label === label) last.items.push(it);
      else out.push({ label, items: [it] });
    }
    return out;
  }, [items]);

  return (
    <div className="mx-auto grid max-w-3xl gap-5">
      <div className="flex flex-wrap items-center gap-3">
        <h1 className="flex items-center gap-2 text-3xl font-black">
          <Activity className="size-7 text-primary" /> Feed
        </h1>
        <Segmented<FeedFilter>
          label="What to show"
          size="sm"
          className="ml-auto"
          value={filter}
          onChange={(v) => {
            const next = new URLSearchParams(params);
            if (v === "all") next.delete("show");
            else next.set("show", v);
            setParams(next, { replace: true });
          }}
          options={[
            { value: "all", label: "All" },
            { value: "uploads", label: "Uploads" },
            { value: "votes", label: "Votes" },
            { value: "curation", label: "Curation", title: "Pins, unpins, deleted versions and beta review marks" },
            { value: "people", label: "People", title: "Name changes and linked consoles" },
          ]}
        />
      </div>
      <p className="-mt-2 text-sm font-semibold text-muted-foreground">
        Who did what, newest first. It updates by itself while the page is open.
      </p>

      {feed.error ? (
        <p className="panel p-5 font-bold">Could not load the feed: {String((feed.error as Error).message)}</p>
      ) : feed.isLoading ? (
        <ul className="grid gap-2.5">
          {Array.from({ length: 8 }, (_, i) => (
            <Skeleton key={i} className="h-[84px] rounded-xl" />
          ))}
        </ul>
      ) : items.length === 0 ? (
        <p className="panel p-6 text-center font-bold text-muted-foreground">Nothing happened yet.</p>
      ) : (
        <>
          {groups.map((g) => (
            <section key={g.label} className="grid gap-2.5">
              <h2 className="sticky top-[76px] z-10 w-fit rounded-full border-2 border-border bg-card px-3 py-0.5 font-pixel text-[10px] uppercase shadow-chunky-sm">
                {g.label}
              </h2>
              <ul className="grid gap-2.5">
                {g.items.map((it) => (
                  <Row key={it.id} item={it} fresh={!!it.key && fresh.has(it.key) && Date.now() / 1000 - it.at < 30} />
                ))}
              </ul>
            </section>
          ))}
          {feed.hasNextPage && (
            <Button variant="outline" className="mx-auto" onClick={() => feed.fetchNextPage()} disabled={feed.isFetchingNextPage}>
              <ArrowDown /> {feed.isFetchingNextPage ? "Loading…" : "Older"}
            </Button>
          )}
        </>
      )}
    </div>
  );
}
