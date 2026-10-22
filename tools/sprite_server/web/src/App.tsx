import { useQueryClient } from "@tanstack/react-query";
import { Loader2 } from "lucide-react";
import * as React from "react";
import { Navigate, Route, Routes, useLocation } from "react-router";
import { Shell } from "@/components/Shell";
import { setSignedOutHandler } from "@/lib/api";
import { LiveProvider } from "@/lib/live";
import { keys, useSession } from "@/lib/queries";
import { AccountPage } from "@/pages/Account";
import { ComparePage } from "@/pages/Compare";
import { FeedPage } from "@/pages/Feed";
import { LoginPage } from "@/pages/Login";
import { MonPage } from "@/pages/Mon";
import { NotFoundPage } from "@/pages/NotFound";
import { ProgressPage } from "@/pages/Progress";
import { UsersPage } from "@/pages/Users";

export function App() {
  const { data: session, isLoading, error, refetch } = useSession();
  const qc = useQueryClient();
  const location = useLocation();

  // A 401 anywhere: the session is gone, so ask again (and land on the sign-in page).
  React.useEffect(() => setSignedOutHandler(() => qc.invalidateQueries({ queryKey: keys.session })), [qc]);

  if (isLoading) {
    return (
      <div className="grid min-h-dvh place-items-center">
        <Loader2 className="size-8 animate-spin text-primary" />
      </div>
    );
  }
  if (error || !session) {
    return (
      <div className="grid min-h-dvh place-items-center p-4">
        <div className="panel grid max-w-sm gap-3 p-6 text-center">
          <p className="font-pixel text-xs">NO SIGNAL</p>
          <p className="font-bold text-muted-foreground">The server cannot be reached right now.</p>
          <button className="font-black text-primary underline" onClick={() => refetch()}>
            Try again
          </button>
        </div>
      </div>
    );
  }
  if (!session.user) {
    return (
      <Routes>
        <Route path="/login" element={<LoginPage />} />
        <Route path="*" element={<Navigate to="/login" replace state={{ from: location.pathname + location.search }} />} />
      </Routes>
    );
  }
  const user = session.user;
  return (
    <LiveProvider session={session}>
      <Routes>
        <Route path="/login" element={<Navigate to="/" replace />} />
        <Route element={<Shell user={user} />}>
          <Route index element={<ProgressPage />} />
          <Route path="feed" element={<FeedPage />} />
          <Route path="mon/:key" element={<MonPage />} />
          <Route path="compare/:key" element={<ComparePage />} />
          <Route path="account" element={<AccountPage />} />
          <Route path="users" element={user.role === "admin" ? <UsersPage /> : <NotFoundPage />} />
          <Route path="*" element={<NotFoundPage />} />
        </Route>
      </Routes>
    </LiveProvider>
  );
}
