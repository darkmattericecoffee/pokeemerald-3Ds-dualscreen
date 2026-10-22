// What the server's /api/web/* endpoints answer (server.py, "The web app's API").

export type Role = "admin" | "artist";

export interface SessionUser {
  id: number;
  name: string;
  display_name: string;
  role: Role;
}

export interface Session {
  user: SessionUser | null;
  csrf: string | null;
  api: number;
}

export interface GridItem {
  version_id: number;
  pinned: boolean;
  final: boolean;
  author: string;
  versions: number;
  uploaded_at: number;
}

export interface Grid {
  rev: number;
  dex_count: number;
  others: string[];
  names: Record<string, string>;
  pictures: string[];
  mine: string[];
  voted: string[];
  items: Record<string, GridItem>;
}

export interface Version {
  version_id: number;
  author: string;
  signed: string;
  console: string | null;
  revision: number;
  final: boolean;
  votes: number;
  voters: string[];
  planes: number;
  frames: number;
  sha256: string;
  size: number;
  saved_at: number;
  uploaded_at: number;
  mine: boolean;
}

export interface Near {
  key: string;
  name: string;
}

export interface Mon {
  key: string;
  name: string;
  kind: "front" | "back" | "trainer" | "player";
  picture: boolean;
  prev: Near | null;
  next: Near | null;
  other: Near | null;
  current: number | null;
  pinned: boolean;
  favourites: number[];
  max_favourites: number;
  admin: boolean;
  versions: Version[];
}

export interface PictureData {
  key: string;
  palette: string[];
  frames: string[];
}

export interface Device {
  id: number;
  console_name: string;
  created: number;
  last_seen: number | null;
}

export interface Account {
  name: string;
  display_name: string;
  role: Role;
  uploads: number;
  min_password: number;
  base: string;
  devices: Device[];
}

export interface PairingCode {
  code: string;
  expires: number;
  seconds: number;
  base: string;
}

export interface UserRow {
  id: number;
  name: string;
  display_name: string;
  role: Role;
  disabled: boolean;
  consoles: number;
  uploads: number;
  created: number;
  last_seen: number | null;
  me: boolean;
}

export interface Users {
  users: UserRow[];
  name?: string;
  password?: string;
}

/** One line of the activity feed (/api/web/feed). */
export type FeedKind = "upload" | "vote" | "unvote" | "pin" | "unpin" | "delete" | "name" | "console";

export interface FeedItem {
  id: number;
  at: number;
  kind: FeedKind;
  /** Who did it, as the site shows them now; null when the account is gone. */
  who: string | null;
  me: boolean;
  key: string | null;
  name: string | null;
  version_id: number | null;
  /** Lines about a version: what became of it since. */
  exists?: boolean;
  replaced?: boolean;
  current?: boolean;
  final?: boolean;
  /** The version's uploader (for pins and deletes by someone else). */
  owner?: string | null;
  revision?: number;
  /** An upload that brought an earlier file of one's own back. */
  again?: number;
  /** Whose version the edit was made over, when the file says. */
  base?: string;
  /** A delete: 1 when the uploader deleted their own version. */
  own?: number;
  /** A name change: the new name. */
  new_name?: string;
  /** A console link: the console's name. */
  console?: string;
}

export type FeedFilter = "all" | "uploads" | "votes" | "curation" | "people";

export interface Feed {
  items: FeedItem[];
  more: boolean;
  rev: number;
}

/** What the account's web app shows, for its consoles that follow the web (/api/web/focus). */
export interface FocusState {
  key: string | null;
  version_ids: number[];
  selected: number | null;
  seq: number;
  at: number;
  /** The console that asked in the last 15 s, if any: it follows this page. */
  follower: { console_name: string; seen: number } | null;
}

/** What the live channel (/api/web/events) sends. */
export type LiveEvent =
  | { type: "hello"; rev: number }
  | { type: "upload"; key: string; version_id: number; current: boolean; who: string; name: string; at: number }
  | { type: "vote"; key: string; version_id?: number; on?: boolean; who?: string; at: number }
  | { type: "pin"; key: string; version_id: number | null; at: number }
  | { type: "delete"; key: string; version_id: number; at: number }
  | { type: "name"; name?: string; at: number }
  | { type: "console"; action: "linked" | "unlinked"; device_id: number; console_name?: string; at: number }
  | { type: "users"; at: number };
