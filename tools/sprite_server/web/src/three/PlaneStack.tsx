import * as React from "react";
import * as THREE from "three";
import { DIM, type EditFile, type Picture } from "@/lib/editfile";
import { planeZ, STANDARD_PX } from "@/lib/depth";
import { renderPixels, type Look } from "@/lib/render";

const SLICE = 0.5; // sprite pixels between the solid mode's slices

function texture(pixels: Uint8ClampedArray): THREE.DataTexture {
  // DataTexture rows go bottom to top.
  const data = new Uint8Array(pixels.length);
  const row = DIM * 4;
  for (let y = 0; y < DIM; y++) data.set(pixels.subarray(y * row, (y + 1) * row), (DIM - 1 - y) * row);
  const tex = new THREE.DataTexture(data, DIM, DIM, THREE.RGBAFormat);
  tex.magFilter = THREE.NearestFilter;
  tex.minFilter = THREE.NearestFilter;
  tex.generateMipmaps = false;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.needsUpdate = true;
  return tex;
}

const geometry = new THREE.PlaneGeometry(DIM, DIM);

function Walls({ tex, from, to }: { tex: THREE.Texture; from: number; to: number }) {
  // Copies of the plane from the farthest plane up to just behind it, darker further back: the
  // pixels become columns, so the picture stays whole however it is turned.
  const count = Math.max(1, Math.ceil((to - from) / SLICE));
  const ref = React.useRef<THREE.InstancedMesh>(null);
  const material = React.useMemo(
    () => new THREE.MeshBasicMaterial({ map: tex, alphaTest: 0.5, side: THREE.DoubleSide }),
    [tex],
  );
  React.useLayoutEffect(() => {
    const mesh = ref.current;
    if (!mesh) return;
    const m = new THREE.Matrix4();
    const c = new THREE.Color();
    for (let i = 0; i < count; i++) {
      const z = from + ((to - from) * i) / count;
      m.makeTranslation(0, 0, z);
      mesh.setMatrixAt(i, m);
      const shade = 0.42 + 0.36 * (i / count);
      mesh.setColorAt(i, c.setRGB(shade, shade, shade));
    }
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    material.needsUpdate = true;
  }, [count, from, to, material]);
  React.useEffect(() => () => material.dispose(), [material]);
  return <instancedMesh key={count} ref={ref} args={[geometry, material, count]} />;
}

export function PlaneStack({
  edit,
  picture,
  frame,
  look,
  exaggerate,
  solid,
}: {
  edit: EditFile;
  picture: Picture | null;
  frame: number;
  look: Look;
  exaggerate: number;
  solid: boolean;
}) {
  const layers = React.useMemo(
    () => edit.used.map((p) => ({ p, tex: texture(renderPixels(edit, picture, frame, look, p)) })),
    [edit, picture, frame, look],
  );
  React.useEffect(() => () => layers.forEach((l) => l.tex.dispose()), [layers]);
  const back = edit.used.length ? planeZ(edit.used[0], edit.planes, STANDARD_PX, exaggerate) : 0;
  return (
    <group>
      {layers.map(({ p, tex }) => {
        const z = planeZ(p, edit.planes, STANDARD_PX, exaggerate);
        return (
          <group key={p}>
            <mesh geometry={geometry} position={[0, 0, z]}>
              <meshBasicMaterial map={tex} alphaTest={0.5} side={THREE.DoubleSide} />
            </mesh>
            {solid && z - back > SLICE / 2 && <Walls tex={tex} from={back} to={z - SLICE / 2} />}
          </group>
        );
      })}
    </group>
  );
}
