import { Activity, Grid3x3, LogOut, Monitor, Moon, Sun, UserRound, Users } from "lucide-react";
import * as React from "react";
import { NavLink, Outlet, useNavigate } from "react-router";
import { Button } from "@/components/ui/button";
import {
  DropdownContent,
  DropdownItem,
  DropdownLabel,
  DropdownMenu,
  DropdownSeparator,
  DropdownTrigger,
} from "@/components/ui/dropdown";
import { Tooltip } from "@/components/ui/tooltip";
import { useLive } from "@/lib/live";
import { useLogout } from "@/lib/queries";
import { useTheme } from "@/lib/theme";
import type { SessionUser } from "@/lib/types";
import { cn } from "@/lib/utils";
import { Lens, Wordmark } from "./Brand";

function Tab({ to, icon: Icon, children }: { to: string; icon: React.ComponentType<{ className?: string }>; children: React.ReactNode }) {
  return (
    <NavLink
      to={to}
      end={to === "/"}
      className={({ isActive }) =>
        cn(
          "flex h-9 items-center gap-1.5 rounded-lg border-2 px-2.5 text-sm font-extrabold transition-colors sm:px-3",
          isActive
            ? "border-border bg-white text-primary shadow-chunky-sm"
            : "border-transparent text-white/90 hover:bg-white/15 hover:text-white",
        )
      }
    >
      <Icon className="size-4" />
      <span className="hidden sm:inline">{children}</span>
    </NavLink>
  );
}

export function Shell({ user }: { user: SessionUser }) {
  const { status } = useLive();
  const logout = useLogout();
  const navigate = useNavigate();
  const { theme, setTheme } = useTheme();
  const liveText =
    status === "live"
      ? "Live: changes show up by themselves"
      : status === "offline"
        ? "Reconnecting… (pages refresh when back)"
        : "Connecting…";
  return (
    <div className="flex min-h-dvh flex-col">
      <header className="sticky top-0 z-40 border-b-[3px] border-border bg-primary shadow-[0_3px_0_var(--primary-deep)]">
        <div className="mx-auto flex h-16 max-w-7xl items-center gap-3 px-3 sm:px-5">
          <NavLink to="/" className="flex items-center gap-3" aria-label="Emerald 3D Plane Lab, progress">
            <Tooltip content={liveText} side="bottom">
              <span>
                <Lens live={status} />
              </span>
            </Tooltip>
            <Wordmark className="hidden min-[400px]:block" />
          </NavLink>
          <nav className="ml-auto flex items-center gap-1">
            <Tab to="/" icon={Grid3x3}>
              Progress
            </Tab>
            <Tab to="/feed" icon={Activity}>
              Feed
            </Tab>
            <Tab to="/account" icon={UserRound}>
              Account
            </Tab>
            {user.role === "admin" && (
              <Tab to="/users" icon={Users}>
                Users
              </Tab>
            )}
            <DropdownMenu>
              <DropdownTrigger asChild>
                <Button variant="accent" size="sm" className="ml-1 max-w-36" aria-label="Menu">
                  <span className="truncate">{user.display_name}</span>
                </Button>
              </DropdownTrigger>
              <DropdownContent>
                <DropdownLabel>
                  Signed in as <span className="text-foreground">{user.name}</span>
                </DropdownLabel>
                <DropdownSeparator />
                <DropdownLabel>Theme</DropdownLabel>
                {(
                  [
                    ["light", Sun, "Light"],
                    ["dark", Moon, "Dark"],
                    ["system", Monitor, "Like the device"],
                  ] as const
                ).map(([value, Icon, label]) => (
                  <DropdownItem key={value} onSelect={() => setTheme(value)} className={theme === value ? "bg-muted" : ""}>
                    <Icon /> {label}
                  </DropdownItem>
                ))}
                <DropdownSeparator />
                <DropdownItem
                  onSelect={() =>
                    logout.mutate(undefined, {
                      onSettled: () => navigate("/login"),
                    })
                  }
                >
                  <LogOut /> Sign out
                </DropdownItem>
              </DropdownContent>
            </DropdownMenu>
          </nav>
        </div>
      </header>
      <main className="mx-auto w-full max-w-7xl flex-1 px-3 py-5 sm:px-5 sm:py-8">
        <Outlet />
      </main>
      <footer className="mx-auto w-full max-w-7xl px-4 pb-6 text-center text-xs font-semibold text-muted-foreground">
        Stereoscopic sprites and editor by darkmatter · a fan project for the Emerald 3DS port
      </footer>
    </div>
  );
}
