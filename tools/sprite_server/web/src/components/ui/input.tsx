import * as React from "react";
import { cn } from "@/lib/utils";

export const Input = React.forwardRef<HTMLInputElement, React.InputHTMLAttributes<HTMLInputElement>>(
  ({ className, ...props }, ref) => (
    <input
      ref={ref}
      className={cn(
        "h-10 w-full min-w-0 rounded-lg border-2 border-input bg-card px-3 text-base font-semibold shadow-[inset_0_2px_0_rgb(0_0_0/0.06)] outline-none placeholder:font-normal placeholder:text-muted-foreground focus-visible:border-ring sm:text-sm",
        className,
      )}
      {...props}
    />
  ),
);
Input.displayName = "Input";

export function Label({ className, ...props }: React.LabelHTMLAttributes<HTMLLabelElement>) {
  return <label className={cn("grid gap-1.5 text-sm font-bold", className)} {...props} />;
}
