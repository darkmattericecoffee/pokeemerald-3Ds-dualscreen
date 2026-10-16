import { clsx, type ClassValue } from "clsx";
import { twMerge } from "tailwind-merge";

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

/** "3 min ago", "2 h ago", "5 d ago". */
export function ago(unix: number | null | undefined, now = Date.now() / 1000): string {
  if (!unix) return "never";
  const d = Math.max(0, Math.floor(now - unix));
  if (d >= 86400 * 30) return new Date(unix * 1000).toLocaleDateString();
  if (d >= 86400) return `${Math.floor(d / 86400)} d ago`;
  if (d >= 3600) return `${Math.floor(d / 3600)} h ago`;
  if (d >= 60) return `${Math.floor(d / 60)} min ago`;
  return "just now";
}

export function when(unix: number | null | undefined): string {
  return unix ? new Date(unix * 1000).toLocaleString() : "";
}

export function plural(n: number, one: string, many = one + "s"): string {
  return `${n} ${n === 1 ? one : many}`;
}
