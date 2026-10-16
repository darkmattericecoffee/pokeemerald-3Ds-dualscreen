import { CheckCircle2, Copy, Gamepad2, KeyRound, Link2, Loader2, PartyPopper, Unlink } from "lucide-react";
import * as React from "react";
import { toast } from "sonner";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Confirm } from "@/components/ui/dialog";
import { Input, Label } from "@/components/ui/input";
import { Skeleton } from "@/components/ui/skeleton";
import { useLive } from "@/lib/live";
import { postFocus, useAccount, useAccountAction } from "@/lib/queries";
import type { PairingCode } from "@/lib/types";
import { ago, plural } from "@/lib/utils";

function copy(text: string) {
  navigator.clipboard?.writeText(text).then(
    () => toast.success("Copied."),
    () => toast.error("Could not copy; select it by hand."),
  );
}

function useCountdown(until: number | null) {
  const [now, setNow] = React.useState(Date.now() / 1000);
  React.useEffect(() => {
    if (!until) return;
    const t = setInterval(() => setNow(Date.now() / 1000), 1000);
    return () => clearInterval(t);
  }, [until]);
  return until ? Math.max(0, Math.round(until - now)) : 0;
}

/** The pairing code, counting down; it turns into a cheer the moment the console links. */
function PairingPanel({ code, linked, onDone }: { code: PairingCode; linked: string | null; onDone: () => void }) {
  const left = useCountdown(code.expires);
  if (linked) {
    return (
      <div className="grid place-items-center gap-2 rounded-xl border-2 border-border bg-success/15 p-6 text-center animate-in zoom-in-95">
        <PartyPopper className="size-10 text-success" />
        <p className="font-pixel text-xs uppercase">Linked!</p>
        <p className="font-bold">{linked} can upload and vote now.</p>
        <Button variant="outline" size="sm" onClick={onDone}>
          Done
        </Button>
      </div>
    );
  }
  const pct = (left / code.seconds) * 100;
  return (
    <div className="grid gap-4 rounded-xl border-2 border-border bg-muted/60 p-4 sm:p-5">
      <div className="flex flex-wrap items-center gap-3">
        <button
          onClick={() => copy(code.code)}
          className="screen px-4 py-3 font-pixel text-xl tracking-widest text-emerald-300 sm:text-2xl"
          title="Copy"
          aria-label={`Pairing code ${code.code}; copy it`}
        >
          {code.code}
        </button>
        <div className="grid gap-1 text-sm font-bold">
          {left > 0 ? (
            <>
              <span className="flex items-center gap-2">
                <Loader2 className="size-4 animate-spin" /> Waiting for your console…
              </span>
              <span className="text-xs text-muted-foreground">
                Works once, for {Math.floor(left / 60)}:{String(left % 60).padStart(2, "0")} more
              </span>
              <div className="h-2 w-40 overflow-hidden rounded-full border-2 border-border bg-card">
                <div className="h-full bg-accent transition-[width] duration-1000 ease-linear" style={{ width: `${pct}%` }} />
              </div>
            </>
          ) : (
            <span className="text-primary">Expired. Make a new code.</span>
          )}
        </div>
      </div>
      <ul className="grid gap-2 text-sm">
        <li>
          <strong>On the 3DS:</strong> OPTIONS › ONLINE › LINK CONSOLE, and type the code (the first time it also asks
          for this server's address: <code className="rounded bg-card px-1 font-bold">{code.base}</code>).
        </li>
        <li className="grid gap-1">
          <strong>On a computer:</strong>
          <span className="flex min-w-0 items-center gap-2">
            <code className="min-w-0 flex-1 overflow-x-auto whitespace-nowrap rounded-md border-2 border-hairline bg-card px-2 py-1.5 text-xs">
              python3 tools/sprite_server/client.py pair {code.base} {code.code}
            </code>
            <Button
              variant="outline"
              size="icon-sm"
              aria-label="Copy the command"
              onClick={() => copy(`python3 tools/sprite_server/client.py pair ${code.base} ${code.code}`)}
            >
              <Copy />
            </Button>
          </span>
        </li>
      </ul>
    </div>
  );
}

export function AccountPage() {
  const { data: account, isLoading } = useAccount();
  const { rename, pair, revoke, password } = useAccountAction();
  const { last } = useLive();
  const [name, setName] = React.useState<string | null>(null);
  const [code, setCode] = React.useState<PairingCode | null>(null);
  const [linked, setLinked] = React.useState<string | null>(null);
  const [pw, setPw] = React.useState({ old: "", new: "", again: "" });

  React.useEffect(() => postFocus(null), []);
  // The live channel says when a console traded the code shown here.
  React.useEffect(() => {
    if (code && last?.type === "console" && last.action === "linked" && last.at >= code.expires - code.seconds - 5) {
      setLinked(last.console_name ?? "Your console");
    }
  }, [last, code]);

  if (isLoading || !account) return <Skeleton className="h-96 rounded-2xl" />;

  const pwError =
    pw.new && pw.new.length < account.min_password
      ? `At least ${account.min_password} characters.`
      : pw.again && pw.again !== pw.new
        ? "The two new passwords differ."
        : "";

  return (
    <div className="mx-auto grid max-w-3xl gap-5">
      <div>
        <h1 className="text-3xl font-black">Your account</h1>
        <p className="font-semibold text-muted-foreground">
          You sign in as <strong className="text-foreground">{account.name}</strong>
          {account.role === "admin" && " (admin)"} · {plural(account.uploads, "upload")}
        </p>
      </div>

      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Gamepad2 className="size-4" /> Consoles and computers
          </CardTitle>
          <CardDescription>Linked devices upload and vote as you. No passwords are ever typed on a console.</CardDescription>
        </CardHeader>
        <CardContent className="grid gap-3">
          {account.devices.length === 0 ? (
            <p className="text-sm font-bold text-muted-foreground">No consoles linked yet.</p>
          ) : (
            <ul className="grid gap-2">
              {account.devices.map((d) => (
                <li key={d.id} className="flex items-center gap-3 rounded-xl border-2 border-hairline bg-card p-2.5 pl-3 animate-in fade-in-0">
                  <Gamepad2 className="size-5 shrink-0 text-primary" />
                  <div className="min-w-0 flex-1">
                    <div className="truncate font-black">{d.console_name}</div>
                    <div className="text-xs font-semibold text-muted-foreground">
                      linked {ago(d.created)} · {d.last_seen ? `last seen ${ago(d.last_seen)}` : "never used"}
                    </div>
                  </div>
                  <Confirm
                    trigger={
                      <Button variant="destructive" size="sm">
                        <Unlink /> Unlink
                      </Button>
                    }
                    title={`Unlink ${d.console_name}?`}
                    description="It will need a new code to upload or vote again."
                    confirm="Unlink"
                    onConfirm={() => revoke.mutate(d.id)}
                  />
                </li>
              ))}
            </ul>
          )}
          {code ? (
            <PairingPanel
              code={code}
              linked={linked}
              onDone={() => {
                setCode(null);
                setLinked(null);
              }}
            />
          ) : null}
          <div>
            <Button
              onClick={() =>
                pair.mutate(undefined, {
                  onSuccess: (c) => {
                    setLinked(null);
                    setCode(c);
                  },
                })
              }
              disabled={pair.isPending}
            >
              <Link2 /> {code ? "New code" : "Link a console"}
            </Button>
          </div>
        </CardContent>
      </Card>

      <div className="grid gap-5 md:grid-cols-2">
        <Card>
          <CardHeader>
            <CardTitle>Your name</CardTitle>
            <CardDescription>Shown with your versions. The editor's ONLINE tab (YOUR NAME) sets it too.</CardDescription>
          </CardHeader>
          <CardContent>
            <form
              className="grid gap-3"
              onSubmit={(e) => {
                e.preventDefault();
                rename.mutate((name ?? account.display_name).trim(), { onSuccess: () => setName(null) });
              }}
            >
              <Label>
                Name
                <Input
                  value={name ?? account.display_name}
                  onChange={(e) => setName(e.target.value)}
                  maxLength={16}
                  pattern="[A-Za-z0-9 ._\-]+"
                  required
                />
              </Label>
              <Button type="submit" variant="secondary" disabled={rename.isPending || name === null}>
                <CheckCircle2 /> Save name
              </Button>
            </form>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <KeyRound className="size-4" /> Password
            </CardTitle>
            <CardDescription>Changing it signs out your other browsers.</CardDescription>
          </CardHeader>
          <CardContent>
            <form
              className="grid gap-3"
              onSubmit={(e) => {
                e.preventDefault();
                if (pwError) return;
                password.mutate({ old: pw.old, new: pw.new }, { onSuccess: () => setPw({ old: "", new: "", again: "" }) });
              }}
            >
              <input type="text" autoComplete="username" value={account.name} readOnly hidden />
              <Label>
                Current password
                <Input type="password" autoComplete="current-password" required value={pw.old} onChange={(e) => setPw({ ...pw, old: e.target.value })} />
              </Label>
              <Label>
                New password
                <Input type="password" autoComplete="new-password" required value={pw.new} onChange={(e) => setPw({ ...pw, new: e.target.value })} />
              </Label>
              <Label>
                New password again
                <Input type="password" autoComplete="new-password" required value={pw.again} onChange={(e) => setPw({ ...pw, again: e.target.value })} />
              </Label>
              {pwError && <p className="text-sm font-bold text-primary">{pwError}</p>}
              <Button type="submit" variant="outline" disabled={password.isPending || !!pwError}>
                Change password
              </Button>
            </form>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
