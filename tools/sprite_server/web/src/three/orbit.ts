import * as React from "react";

/**
 * How the 3D views are turned: views given the same Orbit (an OrbitContext) move together, so
 * dragging one turns every version on the compare page at once.
 */
export interface Orbit {
  yaw: number;
  pitch: number;
  zoom: number;
  /** When someone last dragged (ms): the gentle sway waits a moment after that. */
  lastInput: number;
}

export const createOrbit = (): Orbit => ({ yaw: 0, pitch: 0, zoom: 1, lastInput: 0 });

export const OrbitContext = React.createContext<Orbit | null>(null);

export const YAW_LIMIT = 1.25;
export const PITCH_LIMIT = 0.75;

export function turn(o: Orbit, dx: number, dy: number) {
  o.yaw = Math.max(-YAW_LIMIT, Math.min(YAW_LIMIT, o.yaw + dx * 0.009));
  o.pitch = Math.max(-PITCH_LIMIT, Math.min(PITCH_LIMIT, o.pitch + dy * 0.009));
  o.lastInput = performance.now();
}

export function zoomBy(o: Orbit, factor: number) {
  o.zoom = Math.max(0.6, Math.min(3, o.zoom * factor));
  o.lastInput = performance.now();
}

export function reset(o: Orbit) {
  o.yaw = 0;
  o.pitch = 0;
  o.zoom = 1;
  o.lastInput = 0;
}

/** The sway's angle at time `t` (ms): the same in every view, since they share the clock. */
export function swayAt(t: number) {
  return { yaw: Math.sin(t / 1400) * 0.38, pitch: Math.sin(t / 2300) * 0.1 };
}
