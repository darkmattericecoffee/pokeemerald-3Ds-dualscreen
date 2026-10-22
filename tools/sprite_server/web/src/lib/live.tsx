import { useQueryClient } from "@tanstack/react-query";
import * as React from "react";
import { useNavigate } from "react-router";
import { toast } from "sonner";
import { keys } from "./queries";
import type { LiveEvent, Session } from "./types";

type Status = "connecting" | "live" | "offline";

interface LiveState {
  status: Status;
  /** Pictures that changed lately, with when (ms): tiles and rows flash. */
  fresh: Map<string, number>;
  /** The last event, for pages that react to one (the account page's console linking). */
  last: LiveEvent | null;
}

const LiveContext = React.createContext<LiveState>({ status: "connecting", fresh: new Map(), last: null });

export const useLive = () => React.useContext(LiveContext);

/**
 * The live channel: the server pushes every upload, vote, pin, delete and console link, and the
 * pages that show them fetch again at once. EventSource reconnects by itself; every (re)connect
 * starts with "hello", on which everything is fetched again so nothing missed in between is lost.
 */
export function LiveProvider({ session, children }: { session: Session; children: React.ReactNode }) {
  const qc = useQueryClient();
  const navigate = useNavigate();
  const [state, setState] = React.useState<LiveState>({ status: "connecting", fresh: new Map(), last: null });
  const me = session.user;

  React.useEffect(() => {
    if (!me) return;
    let connectedOnce = false;
    let source: EventSource | null = null;
    let retry: ReturnType<typeof setTimeout> | undefined;
    let closed = false;

    const markFresh = (key: string) =>
      setState((s) => {
        const fresh = new Map(s.fresh);
        fresh.set(key, Date.now());
        return { ...s, fresh };
      });

    const onEvent = (ev: LiveEvent) => {
      setState((s) => ({ ...s, last: ev }));
      switch (ev.type) {
        case "hello":
          if (connectedOnce) qc.invalidateQueries({ predicate: (q) => !["edit", "picture"].includes(String(q.queryKey[0])) });
          connectedOnce = true;
          break;
        case "upload":
          qc.invalidateQueries({ queryKey: keys.grid });
          qc.invalidateQueries({ queryKey: keys.mon(ev.key) });
          qc.invalidateQueries({ queryKey: ["feed"] });
          markFresh(ev.key);
          if (ev.who !== me.display_name) {
            toast(`${ev.who} uploaded ${ev.name}`, {
              description: ev.current ? "It is the current version now." : "A new version to vote on.",
              action: { label: "Look", onClick: () => navigate(`/mon/${ev.key}`) },
            });
          }
          break;
        case "vote":
        case "pin":
        case "delete":
          qc.invalidateQueries({ queryKey: keys.grid });
          qc.invalidateQueries({ queryKey: keys.mon(ev.key) });
          qc.invalidateQueries({ queryKey: ["feed"] });
          markFresh(ev.key);
          break;
        case "name":
          qc.invalidateQueries({ queryKey: ["feed"] });
          qc.invalidateQueries({ queryKey: keys.grid });
          qc.invalidateQueries({ queryKey: ["mon"] });
          qc.invalidateQueries({ queryKey: keys.account });
          qc.invalidateQueries({ queryKey: keys.users });
          qc.invalidateQueries({ queryKey: keys.session });
          break;
        case "console":
          qc.invalidateQueries({ queryKey: ["feed"] });
          qc.invalidateQueries({ queryKey: keys.account });
          qc.invalidateQueries({ queryKey: keys.users });
          break;
        case "users":
          qc.invalidateQueries({ queryKey: keys.users });
          break;
      }
    };

    const open = () => {
      if (closed) return;
      source = new EventSource("/api/web/events");
      source.onopen = () => setState((s) => ({ ...s, status: "live" }));
      source.onmessage = (m) => {
        try {
          onEvent(JSON.parse(m.data) as LiveEvent);
        } catch {
          /* a malformed event is skipped */
        }
      };
      source.onerror = () => {
        setState((s) => ({ ...s, status: "offline" }));
        // A closed stream (the server said no, e.g. signed out) is not retried by the browser.
        if (source?.readyState === EventSource.CLOSED) {
          source.close();
          qc.invalidateQueries({ queryKey: keys.session });
          retry = setTimeout(open, 5000);
        }
      };
    };
    open();
    return () => {
      closed = true;
      clearTimeout(retry);
      source?.close();
    };
  }, [me?.id, me?.display_name, qc, navigate]); // eslint-disable-line react-hooks/exhaustive-deps

  // Old "fresh" marks fade out.
  React.useEffect(() => {
    if (state.fresh.size === 0) return;
    const t = setTimeout(() => {
      setState((s) => {
        const fresh = new Map([...s.fresh].filter(([, at]) => Date.now() - at < 4000));
        return fresh.size === s.fresh.size ? s : { ...s, fresh };
      });
    }, 4100);
    return () => clearTimeout(t);
  }, [state.fresh]);

  return <LiveContext.Provider value={state}>{children}</LiveContext.Provider>;
}
