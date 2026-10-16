import { Loader2, LogIn } from "lucide-react";
import * as React from "react";
import { useLocation, useNavigate } from "react-router";
import { Lens, Wordmark } from "@/components/Brand";
import { Button } from "@/components/ui/button";
import { Input, Label } from "@/components/ui/input";
import { ApiError } from "@/lib/api";
import { useLogin } from "@/lib/queries";

export function LoginPage() {
  const login = useLogin();
  const navigate = useNavigate();
  const location = useLocation();
  const [name, setName] = React.useState("");
  const [password, setPassword] = React.useState("");
  const from = (location.state as { from?: string } | null)?.from ?? "/";
  const error = login.error instanceof ApiError ? login.error.message : login.error ? "Could not sign in." : "";

  return (
    <div className="grid min-h-dvh place-items-center p-4">
      <div className="w-full max-w-sm overflow-hidden rounded-[1.75rem] border-[3px] border-border bg-primary shadow-[0_6px_0_var(--border)]">
        <div className="flex items-center gap-3 border-b-[3px] border-border px-5 py-4">
          <Lens live="live" />
          <Wordmark />
        </div>
        <div className="p-4">
          <form
            className="screen grid gap-4 !bg-card p-5"
            style={{ background: "var(--card)" }}
            onSubmit={(e) => {
              e.preventDefault();
              login.mutate({ name, password }, { onSuccess: () => navigate(from, { replace: true }) });
            }}
          >
            <h1 className="font-pixel text-xs uppercase">Sign in</h1>
            <Label>
              Name
              <Input
                name="name"
                autoComplete="username"
                autoCapitalize="none"
                required
                autoFocus
                value={name}
                onChange={(e) => setName(e.target.value)}
              />
            </Label>
            <Label>
              Password
              <Input
                name="password"
                type="password"
                autoComplete="current-password"
                required
                value={password}
                onChange={(e) => setPassword(e.target.value)}
              />
            </Label>
            {error && (
              <p role="alert" className="rounded-lg border-2 border-primary bg-primary/10 px-3 py-2 text-sm font-bold text-primary">
                {error}
              </p>
            )}
            <Button type="submit" size="lg" disabled={login.isPending}>
              {login.isPending ? <Loader2 className="animate-spin" /> : <LogIn />} Sign in
            </Button>
            <p className="text-xs font-semibold text-muted-foreground">Accounts are handed out by the project's admin.</p>
          </form>
        </div>
        <div className="flex items-center justify-between px-6 pb-4">
          <div className="flex gap-2">
            <span className="h-2 w-8 rounded-full border-2 border-border bg-red-800" />
            <span className="h-2 w-8 rounded-full border-2 border-border bg-sky-400" />
          </div>
          <div className="grid grid-cols-3 grid-rows-3 gap-0" aria-hidden>
            <span />
            <span className="size-3 bg-stone-800" />
            <span />
            <span className="size-3 bg-stone-800" />
            <span className="size-3 bg-stone-800" />
            <span className="size-3 bg-stone-800" />
            <span />
            <span className="size-3 bg-stone-800" />
            <span />
          </div>
        </div>
      </div>
    </div>
  );
}
