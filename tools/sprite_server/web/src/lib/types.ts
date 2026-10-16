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
