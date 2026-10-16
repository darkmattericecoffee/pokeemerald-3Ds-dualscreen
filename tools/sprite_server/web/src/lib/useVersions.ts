import { useQueries } from "@tanstack/react-query";
import { api } from "./api";
import { parseEdit, type EditFile } from "./editfile";
import { keys, usePicture } from "./queries";
import type { Mon } from "./types";

/** The game picture and the edit files of the given versions, fetched together. */
export function useVersionFiles(mon: Mon | undefined, ids: number[]) {
  const picture = usePicture(mon?.key ?? "", !!mon?.picture);
  const edits = useQueries({
    queries: ids.map((vid) => ({
      queryKey: keys.edit(vid),
      queryFn: async () => parseEdit(await api.bytes(`/api/versions/${vid}`)),
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
