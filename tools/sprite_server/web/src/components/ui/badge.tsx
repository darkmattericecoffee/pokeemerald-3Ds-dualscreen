import { cva, type VariantProps } from "class-variance-authority";
import * as React from "react";
import { cn } from "@/lib/utils";

const badgeVariants = cva(
  "inline-flex items-center gap-1 whitespace-nowrap rounded-full border-2 px-2 py-0.5 text-[11px] font-extrabold uppercase tracking-wide [&_svg]:size-3",
  {
    variants: {
      variant: {
        default: "border-border bg-primary text-primary-foreground",
        current: "border-border bg-success text-success-foreground",
        final: "border-border bg-accent text-accent-foreground",
        outline: "border-hairline bg-card text-muted-foreground",
        muted: "border-transparent bg-muted text-muted-foreground",
        dark: "border-border bg-foreground text-background",
      },
    },
    defaultVariants: { variant: "default" },
  },
);

export function Badge({
  className,
  variant,
  ...props
}: React.HTMLAttributes<HTMLSpanElement> & VariantProps<typeof badgeVariants>) {
  return <span className={cn(badgeVariants({ variant }), className)} {...props} />;
}
