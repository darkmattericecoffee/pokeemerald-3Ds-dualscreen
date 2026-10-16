import { Copy, KeyRound, ShieldCheck, UserPlus, UserRoundX, UserRoundCheck } from "lucide-react";
import * as React from "react";
import { toast } from "sonner";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Confirm, Dialog } from "@/components/ui/dialog";
import { Input, Label } from "@/components/ui/input";
import { Skeleton } from "@/components/ui/skeleton";
import { Segmented } from "@/components/ui/toggle-group";
import { postFocus, useUserAction, useUsers } from "@/lib/queries";
import { ago, cn, plural } from "@/lib/utils";

export function UsersPage() {
  const { data, isLoading } = useUsers();
  const { add, act } = useUserAction();
  const [name, setName] = React.useState("");
  const [role, setRole] = React.useState<"artist" | "admin">("artist");
  const [shown, setShown] = React.useState<{ name: string; password: string; title: string } | null>(null);
  React.useEffect(() => postFocus(null), []);

  return (
    <div className="mx-auto grid max-w-4xl gap-5">
      <h1 className="text-3xl font-black">Users</h1>
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <UserPlus className="size-4" /> Add a user
          </CardTitle>
          <CardDescription>The server makes the password; you see it once and pass it on.</CardDescription>
        </CardHeader>
        <CardContent>
          <form
            className="flex flex-wrap items-end gap-3"
            onSubmit={(e) => {
              e.preventDefault();
              add.mutate(
                { name: name.trim(), role },
                {
                  onSuccess: (u) => {
                    setShown({ name: u.name!, password: u.password!, title: "User added" });
                    setName("");
                  },
                },
              );
            }}
          >
            <Label className="min-w-48 flex-1">
              Name
              <Input value={name} onChange={(e) => setName(e.target.value)} required maxLength={32} pattern="[A-Za-z0-9_. \-]+" />
            </Label>
            <Segmented<"artist" | "admin">
              label="Role"
              value={role}
              onChange={setRole}
              options={[
                { value: "artist", label: "Artist" },
                { value: "admin", label: "Admin" },
              ]}
            />
            <Button type="submit" disabled={add.isPending}>
              <UserPlus /> Add user
            </Button>
          </form>
        </CardContent>
      </Card>

      {isLoading || !data ? (
        <Skeleton className="h-64 rounded-2xl" />
      ) : (
        <ul className="grid gap-2.5">
          {data.users.map((u) => (
            <li key={u.id} className={cn("panel flex flex-wrap items-center gap-3 p-3 sm:p-4", u.disabled && "opacity-60")}>
              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-black">{u.name}</span>
                  {u.display_name !== u.name && <span className="text-sm font-bold text-muted-foreground">({u.display_name})</span>}
                  {u.role === "admin" && (
                    <Badge variant="dark">
                      <ShieldCheck /> Admin
                    </Badge>
                  )}
                  {u.disabled && <Badge variant="muted">Disabled</Badge>}
                  {u.me && <Badge variant="outline">You</Badge>}
                </div>
                <div className="mt-0.5 text-xs font-semibold text-muted-foreground">
                  {plural(u.consoles, "console")} · {plural(u.uploads, "upload")} · since {ago(u.created)}
                  {u.last_seen ? ` · console seen ${ago(u.last_seen)}` : ""}
                </div>
              </div>
              <div className="flex gap-2">
                <Confirm
                  trigger={
                    <Button variant="outline" size="sm">
                      <KeyRound /> New password
                    </Button>
                  }
                  title={`New password for ${u.name}?`}
                  description="The old one stops working and they are signed out everywhere."
                  confirm="Make one"
                  onConfirm={() =>
                    act.mutate(
                      { id: u.id, act: "reset" },
                      { onSuccess: (r) => setShown({ name: r.name!, password: r.password!, title: "New password" }) },
                    )
                  }
                />
                {!u.me &&
                  (u.disabled ? (
                    <Button variant="success" size="sm" onClick={() => act.mutate({ id: u.id, act: "enable" })}>
                      <UserRoundCheck /> Enable
                    </Button>
                  ) : (
                    <Confirm
                      trigger={
                        <Button variant="destructive" size="sm">
                          <UserRoundX /> Disable
                        </Button>
                      }
                      title={`Disable ${u.name}?`}
                      description="They can no longer sign in, and their consoles stop working."
                      confirm="Disable"
                      onConfirm={() => act.mutate({ id: u.id, act: "disable" })}
                    />
                  ))}
              </div>
            </li>
          ))}
        </ul>
      )}

      <Dialog
        open={!!shown}
        onOpenChange={(o) => !o && setShown(null)}
        title={shown?.title ?? ""}
        description={
          <>
            Pass this on to <strong>{shown?.name}</strong>. It is shown only now; they can change it on their account
            page.
          </>
        }
      >
        <div className="flex items-center gap-2">
          <code className="screen flex-1 break-all px-3 py-2.5 font-pixel text-xs text-emerald-300">{shown?.password}</code>
          <Button
            variant="outline"
            size="icon"
            aria-label="Copy"
            onClick={() =>
              navigator.clipboard?.writeText(shown?.password ?? "").then(() => toast.success("Copied."), () => toast.error("Could not copy."))
            }
          >
            <Copy />
          </Button>
        </div>
      </Dialog>
    </div>
  );
}
