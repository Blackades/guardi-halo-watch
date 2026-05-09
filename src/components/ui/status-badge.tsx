import * as React from "react"
import { cva, type VariantProps } from "class-variance-authority"
import { cn } from "@/lib/utils"

const statusBadgeVariants = cva(
  "inline-flex items-center rounded-full border px-2.5 py-0.5 text-xs font-semibold transition-colors focus:outline-none focus:ring-2 focus:ring-ring focus:ring-offset-2",
  {
    variants: {
      variant: {
        default: "border-transparent bg-primary text-primary-foreground hover:bg-primary/80",
        secondary: "border-transparent bg-secondary text-secondary-foreground hover:bg-secondary/80",
        destructive: "border-transparent bg-destructive text-destructive-foreground hover:bg-destructive/80",
        outline: "text-foreground",
        normal: "border-transparent bg-success text-success-foreground hover:bg-success/80",
        occupied: "border-transparent bg-info text-info-foreground hover:bg-info/80",
        warning: "border-transparent bg-warning text-warning-foreground hover:bg-warning/80",
        anomaly: "border-transparent bg-destructive text-destructive-foreground hover:bg-destructive/80",
      },
      size: {
        default: "px-2.5 py-0.5 text-xs",
        sm: "px-2 py-0.5 text-xs",
        lg: "px-3 py-1 text-sm",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  }
)

export interface StatusBadgeProps
  extends React.HTMLAttributes<HTMLDivElement>,
    VariantProps<typeof statusBadgeVariants> {}

function StatusBadge({ className, variant, size, ...props }: StatusBadgeProps) {
  return (
    <div className={cn(statusBadgeVariants({ variant, size }), className)} {...props} />
  )
}

export { StatusBadge, statusBadgeVariants }