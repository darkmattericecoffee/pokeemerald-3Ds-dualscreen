import * as React from "react";
import { cn } from "@/lib/utils";

// three.js is most of the app's weight: it loads only when a page shows a 3D view.
const Viewer = React.lazy(() => import("./Viewer3D").then((m) => ({ default: m.Viewer3D })));

export function Viewer3D(props: React.ComponentProps<typeof Viewer>) {
  return (
    <React.Suspense fallback={<div className={cn("screen aspect-square w-full animate-pulse", props.className)} />}>
      <Viewer {...props} />
    </React.Suspense>
  );
}
