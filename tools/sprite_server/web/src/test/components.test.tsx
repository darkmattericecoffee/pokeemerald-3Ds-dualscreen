import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { DepthBadge } from "@/components/DepthDots";
import { VoteButton } from "@/components/VoteButton";
import { ago } from "@/lib/utils";

describe("VoteButton", () => {
  it("shows the count and whether it is pressed, and toggles on click", () => {
    const onToggle = vi.fn();
    const { rerender } = render(<VoteButton voted={false} votes={2} onToggle={onToggle} />);
    const button = screen.getByRole("button");
    expect(button).toHaveAttribute("aria-pressed", "false");
    expect(button).toHaveTextContent("Vote");
    expect(button).toHaveTextContent("2");
    fireEvent.click(button);
    expect(onToggle).toHaveBeenCalledOnce();
    rerender(<VoteButton voted votes={3} onToggle={onToggle} />);
    expect(screen.getByRole("button")).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByRole("button")).toHaveTextContent("Voted");
  });
});

describe("DepthBadge", () => {
  it("names the sweet spot, or says it is not set", () => {
    const { rerender } = render(<DepthBadge depth={3} />);
    expect(screen.getByText("Deep")).toBeInTheDocument();
    rerender(<DepthBadge depth={null} />);
    expect(screen.getByText("Std?")).toBeInTheDocument();
  });
});

describe("ago", () => {
  it("speaks in minutes, hours and days", () => {
    const now = 1_000_000;
    expect(ago(now - 10, now)).toBe("just now");
    expect(ago(now - 180, now)).toBe("3 min ago");
    expect(ago(now - 7200, now)).toBe("2 h ago");
    expect(ago(now - 86400 * 2, now)).toBe("2 d ago");
    expect(ago(null, now)).toBe("never");
  });
});
