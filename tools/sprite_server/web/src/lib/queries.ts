import { useInfiniteQuery, useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { toast } from "sonner";
import { api, ApiError, setCsrf } from "./api";
import { parseEdit, parsePicture } from "./editfile";
import type { Account, Feed, FeedFilter, FocusState, Grid, Mon, PairingCode, PictureData, ReviewStatus, Session, Users } from "./types";

export const keys = {
  session: ["session"] as const,
  grid: ["grid"] as const,
  mon: (key: string) => ["mon", key] as const,
  edit: (vid: number) => ["edit", vid] as const,
  picture: (key: string) => ["picture", key] as const,
  account: ["account"] as const,
  users: ["users"] as const,
  focus: ["focus"] as const,
  feed: (filter: FeedFilter) => ["feed", filter] as const,
};

export function useSession() {
  return useQuery({
    queryKey: keys.session,
    queryFn: async () => {
      const s = await api.get<Session>("/api/web/session");
      setCsrf(s.csrf);
      return s;
    },
    staleTime: 5 * 60_000,
  });
}

export function useGrid() {
  return useQuery({ queryKey: keys.grid, queryFn: () => api.get<Grid>("/api/web/grid") });
}

export function useMon(key: string) {
  return useQuery({ queryKey: keys.mon(key), queryFn: () => api.get<Mon>(`/api/web/mons/${key}`) });
}

/** A version's edit file, parsed. Versions never change, so it is fetched once. */
export function useEdit(vid: number | null | undefined) {
  return useQuery({
    queryKey: keys.edit(vid ?? 0),
    queryFn: async () => parseEdit(await api.bytes(`/api/versions/${vid}`)),
    enabled: !!vid,
    staleTime: Infinity,
    gcTime: 30 * 60_000,
  });
}

/** The game picture as data; null when the server has none (trainers, or no sprites.json). */
export function usePicture(key: string, has: boolean) {
  return useQuery({
    queryKey: keys.picture(key),
    queryFn: async () => parsePicture(await api.get<PictureData>(`/api/web/pictures/${key}?v=1`)),
    enabled: has,
    staleTime: Infinity,
    gcTime: 30 * 60_000,
  });
}

/** The activity feed, newest first, a page at a time (the next page is the lines before the last one). */
export function useFeed(filter: FeedFilter) {
  return useInfiniteQuery({
    queryKey: keys.feed(filter),
    queryFn: ({ pageParam }) =>
      api.get<Feed>(`/api/web/feed?kind=${filter}${pageParam ? `&before=${pageParam}` : ""}`),
    initialPageParam: 0,
    getNextPageParam: (last) => (last.more && last.items.length ? last.items[last.items.length - 1].id : undefined),
  });
}

export function useAccount() {
  return useQuery({ queryKey: keys.account, queryFn: () => api.get<Account>("/api/web/account") });
}

export function useUsers() {
  return useQuery({ queryKey: keys.users, queryFn: () => api.get<Users>("/api/web/users") });
}

function errorToast(err: unknown) {
  toast.error(err instanceof ApiError ? err.message : "Something went wrong.");
}

/** Vote on or off; the page shows it at once and the server's answer settles it. */
export function useVote(key: string) {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: ({ versionId, on }: { versionId: number; on: boolean }) =>
      api.post<Mon>(`/api/web/mons/${key}/vote`, { version_id: versionId, on }),
    onMutate: async ({ versionId, on }) => {
      await qc.cancelQueries({ queryKey: keys.mon(key) });
      const before = qc.getQueryData<Mon>(keys.mon(key));
      if (before) {
        const me = qc.getQueryData<Session>(keys.session)?.user?.display_name ?? "you";
        qc.setQueryData<Mon>(keys.mon(key), {
          ...before,
          favourites: on
            ? [...before.favourites.filter((v) => v !== versionId), versionId]
            : before.favourites.filter((v) => v !== versionId),
          versions: before.versions.map((v) =>
            v.version_id !== versionId || before.favourites.includes(versionId) === on
              ? v
              : {
                  ...v,
                  votes: v.votes + (on ? 1 : -1),
                  voters: on ? [...v.voters, me] : v.voters.filter((n, i) => n !== me || v.voters.indexOf(me) !== i),
                },
          ),
        });
      }
      return { before };
    },
    onError: (err, _vars, ctx) => {
      if (ctx?.before) qc.setQueryData(keys.mon(key), ctx.before);
      errorToast(err);
    },
    onSuccess: (mon) => {
      qc.setQueryData(keys.mon(key), mon);
      qc.invalidateQueries({ queryKey: keys.grid });
    },
  });
}

/** The picture's review before the beta ("" clears it); shown at once, settled by the server. */
export function useReview(key: string) {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (status: ReviewStatus | "") => api.post<Mon>(`/api/web/mons/${key}/review`, { status }),
    onMutate: async (status) => {
      await qc.cancelQueries({ queryKey: keys.mon(key) });
      const before = qc.getQueryData<Mon>(keys.mon(key));
      if (before) {
        const me = qc.getQueryData<Session>(keys.session)?.user?.display_name ?? "you";
        qc.setQueryData<Mon>(keys.mon(key), {
          ...before,
          review: status ? { status, by: me, at: Math.floor(Date.now() / 1000) } : null,
        });
      }
      return { before };
    },
    onError: (err, _vars, ctx) => {
      if (ctx?.before) qc.setQueryData(keys.mon(key), ctx.before);
      errorToast(err);
    },
    onSuccess: (mon) => {
      qc.setQueryData(keys.mon(key), mon);
      qc.invalidateQueries({ queryKey: keys.grid });
    },
  });
}

export function useMonAction(key: string) {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (a: { kind: "pin"; versionId: number } | { kind: "unpin" } | { kind: "delete"; versionId: number }) =>
      a.kind === "delete"
        ? api.post<Mon>(`/api/web/versions/${a.versionId}/delete`)
        : a.kind === "pin"
          ? api.post<Mon>(`/api/web/mons/${key}/pin`, { version_id: a.versionId })
          : api.post<Mon>(`/api/web/mons/${key}/unpin`),
    onSuccess: (mon, a) => {
      qc.setQueryData(keys.mon(key), mon);
      qc.invalidateQueries({ queryKey: keys.grid });
      toast.success(
        a.kind === "delete"
          ? "Version deleted."
          : a.kind === "pin"
            ? "Pinned: this version stays current until unpinned."
            : "Unpinned: the most votes win again.",
      );
    },
    onError: errorToast,
  });
}

export function useLogin() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { name: string; password: string }) => api.post<Session>("/api/web/login", v),
    onSuccess: (s) => {
      setCsrf(s.csrf);
      qc.clear();
      qc.setQueryData(keys.session, s);
    },
  });
}

export function useLogout() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: () => api.post<Session>("/api/web/logout"),
    onSettled: () => {
      setCsrf(null);
      qc.clear();
      qc.setQueryData<Session>(keys.session, { user: null, csrf: null, api: 2 });
    },
  });
}

export function useAccountAction() {
  const qc = useQueryClient();
  return {
    rename: useMutation({
      mutationFn: (name: string) => api.post<Account>("/api/web/account/name", { name }),
      onSuccess: (a) => {
        qc.setQueryData(keys.account, a);
        qc.invalidateQueries({ queryKey: keys.session });
        toast.success("Name saved. Your versions show it from now on.");
      },
      onError: errorToast,
    }),
    pair: useMutation({
      mutationFn: () => api.post<PairingCode>("/api/web/account/pair"),
      onError: errorToast,
    }),
    revoke: useMutation({
      mutationFn: (id: number) => api.post<Account>(`/api/web/account/devices/${id}/revoke`),
      onSuccess: (a) => {
        qc.setQueryData(keys.account, a);
        toast.success("Unlinked. That console needs a new code to upload again.");
      },
      onError: errorToast,
    }),
    password: useMutation({
      mutationFn: (v: { old: string; new: string }) => api.post<Session>("/api/web/account/password", v),
      onSuccess: (s) => {
        setCsrf(s.csrf);
        qc.setQueryData(keys.session, s);
        toast.success("Password changed. Other browsers were signed out.");
      },
      onError: errorToast,
    }),
  };
}

export function useUserAction() {
  const qc = useQueryClient();
  const done = (u: Users) => qc.setQueryData(keys.users, { users: u.users });
  return {
    add: useMutation({
      mutationFn: (v: { name: string; role: string }) => api.post<Users>("/api/web/users", v),
      onSuccess: done,
      onError: errorToast,
    }),
    act: useMutation({
      mutationFn: (v: { id: number; act: "enable" | "disable" | "reset" }) =>
        api.post<Users>(`/api/web/users/${v.id}/${v.act}`),
      onSuccess: done,
      onError: errorToast,
    }),
  };
}

/** Tells the server what this page shows (the picture, the versions compared, the one selected), for
 *  the account's consoles that follow the web. */
export function postFocus(key: string | null, versionIds: number[] = [], selected: number | null = null) {
  // Id 0 is the game's own planes (BUILTIN_ID): a following console shows its pack's.
  api.post("/api/web/focus", { key, version_ids: versionIds, selected }).catch(() => {});
}

/** Whether a 3DS of this account follows the web right now (it asks every couple of seconds). */
export function useFollower() {
  return useQuery({
    queryKey: keys.focus,
    queryFn: () => api.get<FocusState>("/api/web/focus"),
    refetchInterval: 5000,
    select: (f) => f.follower,
  });
}

/** "Show on 3DS": sends this page's picture and version to the account's following console now. */
export function useShowOn3ds() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (v: { key: string; versionIds?: number[]; selected?: number | null; what?: string }) =>
      api.post<FocusState>("/api/web/focus", {
        key: v.key,
        version_ids: v.versionIds ?? [],
        selected: v.selected ?? null,
        push: true,
      }),
    onSuccess: (f, v) => {
      qc.setQueryData(keys.focus, f);
      if (f.follower) toast.success(`Showing ${v.what ?? "it"} on ${f.follower.console_name}.`);
      else
        toast("Sent, but no 3DS is following right now.", {
          description: "On the 3DS, open a Pokémon's online view and turn on FOLLOW WEB; it jumps here.",
        });
    },
    onError: errorToast,
  });
}
