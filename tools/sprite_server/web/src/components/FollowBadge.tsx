import { Gamepad2 } from "lucide-react";
import { Tooltip } from "@/components/ui/tooltip";
import { useFollower } from "@/lib/queries";
import { cn } from "@/lib/utils";

/** Whether a 3DS follows this page: green with its name, or how to make one follow. */
export function FollowBadge({ className }: { className?: string }) {
  const { data: follower } = useFollower();
  return (
    <Tooltip
      content={
        follower
          ? "It shows the version you pick here, and Show on 3DS jumps it there again."
          : "On the 3DS, open a Pokémon's online view and turn on FOLLOW WEB: it then shows what you pick here."
      }
    >
      <span
        className={cn(
          "inline-flex items-center gap-1.5 rounded-full border-2 px-2.5 py-1 text-xs font-extrabold",
          follower ? "border-border bg-success text-success-foreground" : "border-hairline bg-card text-muted-foreground",
          className,
        )}
      >
        <Gamepad2 className="size-3.5" />
        {follower ? `${follower.console_name} follows` : "No 3DS following"}
      </span>
    </Tooltip>
  );
}
