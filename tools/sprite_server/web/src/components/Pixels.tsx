import * as React from "react";
import { DIM, type EditFile, type Picture } from "@/lib/editfile";
import { renderPicture, renderPixels, type Look } from "@/lib/render";
import { cn } from "@/lib/utils";

/** 64x64 RGBA pixels drawn crisp at any size. */
export function PixelCanvas({ pixels, className, label }: { pixels: Uint8ClampedArray | null; className?: string; label?: string }) {
  const ref = React.useRef<HTMLCanvasElement>(null);
  React.useEffect(() => {
    const ctx = ref.current?.getContext("2d");
    if (!ctx) return;
    ctx.clearRect(0, 0, DIM, DIM);
    if (pixels) ctx.putImageData(new ImageData(new Uint8ClampedArray(pixels), DIM, DIM), 0, 0);
  }, [pixels]);
  return (
    <canvas
      ref={ref}
      width={DIM}
      height={DIM}
      className={cn("pixelated aspect-square", className)}
      role={label ? "img" : undefined}
      aria-label={label}
    />
  );
}

export function EditPixels({
  edit,
  picture,
  frame,
  look,
  className,
  label,
}: {
  edit: EditFile;
  picture: Picture | null;
  frame: number;
  look: Look;
  className?: string;
  label?: string;
}) {
  const pixels = React.useMemo(() => renderPixels(edit, picture, frame, look), [edit, picture, frame, look]);
  return <PixelCanvas pixels={pixels} className={className} label={label} />;
}

export function PicturePixels({ picture, frame, className, label }: { picture: Picture; frame: number; className?: string; label?: string }) {
  const pixels = React.useMemo(() => renderPicture(picture, frame), [picture, frame]);
  return <PixelCanvas pixels={pixels} className={className} label={label} />;
}
