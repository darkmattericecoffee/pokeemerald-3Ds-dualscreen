import { Star } from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";

/** Vote, or take the vote back: a big star button that stays pressed in when voted. */
export function VoteButton({
  voted,
  votes,
  onToggle,
  disabled,
  className,
  size = "default",
}: {
  voted: boolean;
  votes: number;
  onToggle: () => void;
  disabled?: boolean;
  className?: string;
  size?: "default" | "sm" | "lg";
}) {
  return (
    <Button
      variant={voted ? "accent" : "outline"}
      size={size}
      onClick={onToggle}
      disabled={disabled}
      aria-pressed={voted}
      title={voted ? "Take your vote back" : "Vote for this version"}
      className={cn(voted && "translate-y-0.5 shadow-none", className)}
    >
      <Star className={cn(voted && "fill-current animate-pop")} />
      {voted ? "Voted" : "Vote"}
      <span className="ml-0.5 rounded-full bg-black/10 px-1.5 text-xs tabular-nums">{votes}</span>
    </Button>
  );
}
