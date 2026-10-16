import * as React from "react";

export type Theme = "light" | "dark" | "system";

function read(): Theme {
  try {
    const t = localStorage.getItem("theme");
    return t === "light" || t === "dark" ? t : "system";
  } catch {
    return "system";
  }
}

function apply(theme: Theme) {
  const dark = theme === "dark" || (theme === "system" && matchMedia("(prefers-color-scheme: dark)").matches);
  document.documentElement.classList.toggle("dark", dark);
}

export function initTheme() {
  apply(read());
  matchMedia("(prefers-color-scheme: dark)").addEventListener?.("change", () => apply(read()));
}

export function useTheme() {
  const [theme, set] = React.useState<Theme>(read);
  const setTheme = (t: Theme) => {
    try {
      localStorage.setItem("theme", t);
    } catch {
      /* private mode: just for now */
    }
    apply(t);
    set(t);
  };
  return { theme, setTheme };
}
