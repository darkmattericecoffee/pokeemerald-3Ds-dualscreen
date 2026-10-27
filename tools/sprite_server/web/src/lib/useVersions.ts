import { useQueries } from "@tanstack/react-query";
import { api } from "./api";
import { parseEdit, type EditFile } from "./editfile";
import { keys, usePicture } from "./queries";
import { BUILTIN_ID, type Mon } from "./types";

/** The game picture and the edit files of the given versions, fetched together. */
export function useVersionFiles(mon: Mon | undefined, ids: number[]) {
  const picture = usePicture(mon?.key ?? "", !!mon?.picture);
  const edits = useQueries({
    // Id 0 is the game's own planes (BUILTIN_ID), one per picture.
    queries: ids.map((vid) => ({
      queryKey: vid === BUILTIN_ID ? ["builtin", mon?.key ?? ""] : keys.edit(vid),
      queryFn: async () =>
        parseEdit(await api.bytes(vid === BUILTIN_ID ? `/api/builtin/${mon?.key}.bin` : `/api/versions/${vid}`)),
      staleTime: Infinity,
      gcTime: 30 * 60_000,
    })),
  });
  const byId = new Map<number, EditFile>();
  ids.forEach((vid, i) => {
    const e = edits[i]?.data;
    if (e) byId.set(vid, e);
  });
  return {
    picture: picture.data ?? null,
    pictureLoading: !!mon?.picture && picture.isLoading,
    edits: byId,
    loading: edits.some((q) => q.isLoading),
  };
}
