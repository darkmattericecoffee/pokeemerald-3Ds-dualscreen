import { Link } from "react-router";
import { Button } from "@/components/ui/button";

export function NotFoundPage() {
  return (
    <div className="panel mx-auto grid max-w-md place-items-center gap-4 p-8 text-center">
      <p className="font-pixel text-sm">404</p>
      <p className="font-bold text-muted-foreground">There is nothing here. Maybe it fled?</p>
      <Button asChild>
        <Link to="/">Back to the progress</Link>
      </Button>
    </div>
  );
}
