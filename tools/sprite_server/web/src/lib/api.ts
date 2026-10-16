// The server's web API: JSON, the session cookie, and the CSRF token on every POST.

export class ApiError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}

let csrfToken: string | null = null;
let onSignedOut: (() => void) | null = null;

export function setCsrf(token: string | null) {
  csrfToken = token;
}

/** Called when the server says the session is gone (signed out elsewhere, disabled, expired). */
export function setSignedOutHandler(fn: () => void) {
  onSignedOut = fn;
}

async function request<T>(method: "GET" | "POST", path: string, body?: unknown): Promise<T> {
  const headers: Record<string, string> = { Accept: "application/json" };
  if (method === "POST") {
    headers["Content-Type"] = "application/json";
    if (csrfToken) headers["X-CSRF-Token"] = csrfToken;
  }
  let res: Response;
  try {
    res = await fetch(path, {
      method,
      headers,
      credentials: "same-origin",
      body: method === "POST" ? JSON.stringify(body ?? {}) : undefined,
    });
  } catch {
    throw new ApiError(0, "The server cannot be reached. Check your connection.");
  }
  const text = await res.text();
  let data: unknown = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  if (!res.ok) {
    const said = data && typeof data === "object" && "error" in data ? String((data as { error: unknown }).error) : "";
    const message: string = said || `The server answered ${res.status}.`;
    if (res.status === 401 && !path.endsWith("/login")) onSignedOut?.();
    throw new ApiError(res.status, message.charAt(0).toUpperCase() + message.slice(1));
  }
  return data as T;
}

export const api = {
  get: <T>(path: string) => request<T>("GET", path),
  post: <T>(path: string, body?: unknown) => request<T>("POST", path, body),
  /** Raw bytes (an edit file). */
  async bytes(path: string): Promise<ArrayBuffer> {
    const res = await fetch(path, { credentials: "same-origin" });
    if (!res.ok) throw new ApiError(res.status, `Could not load ${path} (${res.status}).`);
    return res.arrayBuffer();
  },
};
