import { Slot } from "@radix-ui/react-slot";
import { cva, type VariantProps } from "class-variance-authority";
import * as React from "react";
import { cn } from "@/lib/utils";

// Chunky game buttons: an outline, a hard shadow, and they press in when clicked.
const buttonVariants = cva(
  "inline-flex shrink-0 select-none items-center justify-center gap-2 whitespace-nowrap rounded-lg border-2 border-border font-extrabold transition-[transform,box-shadow,background-color] duration-100 disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:size-4 [&_svg]:shrink-0 shadow-chunky-sm hover:-translate-y-px active:translate-y-0.5 active:shadow-none",
  {
    variants: {
      variant: {
        default: "bg-primary text-primary-foreground hover:brightness-110",
        accent: "bg-accent text-accent-foreground hover:brightness-105",
        success: "bg-success text-success-foreground hover:brightness-110",
        outline: "bg-card text-card-foreground hover:bg-muted",
        secondary: "bg-secondary text-secondary-foreground hover:brightness-105",
        destructive: "bg-card text-destructive hover:bg-destructive hover:text-white",
        ghost: "border-transparent bg-transparent shadow-none hover:bg-muted active:translate-y-0 hover:translate-y-0",
        link: "border-transparent bg-transparent p-0 text-primary underline-offset-4 shadow-none hover:underline hover:translate-y-0 active:translate-y-0",
      },
      size: {
        default: "h-10 px-4 text-sm",
        sm: "h-8 rounded-md px-3 text-xs",
        lg: "h-12 px-6 text-base",
        icon: "size-10",
        "icon-sm": "size-8 rounded-md",
      },
    },
    defaultVariants: { variant: "default", size: "default" },
  },
);

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {
  asChild?: boolean;
}

export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, asChild = false, ...props }, ref) => {
    const Comp = asChild ? Slot : "button";
    return <Comp ref={ref} className={cn(buttonVariants({ variant, size }), className)} {...props} />;
  },
);
Button.displayName = "Button";

export { buttonVariants };
