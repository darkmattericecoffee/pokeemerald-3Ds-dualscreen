#!/usr/bin/env python3
"""The hand marks over the terraces (voxel_terraces.py, "7. hand marks"): the
piece a level alone gives, the runs a face or a band drawn wider than a tile
falls down, ledges never marked, and a real map taking its marks."""

import pathlib
import sys
from unittest.mock import patch

PORT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT / "scripts"))

import voxel_terraces as vt  # noqa: E402

L = vt.LEVEL


class FakeCells:
    """What apply_marks reads of a Cells: the kept cells, the bands, the faces."""

    def __init__(self, faces=(), band=None, keep=()):
        self.faces = set(faces)
        self.band = dict(band or {})
        self.keep = set(keep)


def grid_levels(rows):
    """{cell: px} from rows of levels, north row first."""
    return {(x, y): L * v for y, row in enumerate(rows) for x, v in enumerate(row)}


def test_auto_pieces():
    # a plateau at 1 over ground at 0, its face the row y=2
    level = grid_levels([[0, 0, 0, 0, 0],
                         [0, 1, 1, 1, 0],
                         [0, 1, 1, 1, 0],
                         [0, 0, 0, 0, 0]])
    got = {cell: vt.auto_piece(level, cell)[0] for cell in level if level[cell]}
    assert got == {(1, 1): "W", (2, 1): "F", (3, 1): "E",
                   (1, 2): "cSW", (2, 2): "S", (3, 2): "cSE"}, got
    # a concave corner: only the diagonal falls
    level = grid_levels([[1, 1], [0, 1]])
    assert vt.auto_piece(level, (1, 0)) == ("vSW", L)
    # two levels at once: a band of two, a face of two in its tile
    level = grid_levels([[0, 2, 2]])
    assert vt.auto_piece(level, (1, 0)) == ("W2", 2 * L)
    level = grid_levels([[2], [0]])
    assert vt.auto_piece(level, (0, 0)) == ("S:32", 2 * L)


def test_marks_and_runs():
    # a plateau at 2 whose face is drawn two rows tall (y=1, y=2), all
    # marked at the top's level: the face falls a level a row
    pieces = {cell: ("F", 0) for cell in grid_levels([[0] * 3] * 5)}
    marks = {(x, y): {"level": 2 * L, "piece": None} for x in range(3) for y in range(3)}
    c = FakeCells(faces={(x, y) for x in range(3) for y in (1, 2)})
    flat = {}
    with patch.object(vt, "is_face", lambda cc, cell: cell in cc.faces):
        marked = vt.apply_marks(c, pieces, flat, {}, marks)
    assert marked == set(marks)
    for x in range(3):
        assert pieces[(x, 0)] == ("F", 2 * L), pieces[(x, 0)]
        assert pieces[(x, 1)] == ("S", 2 * L), pieces[(x, 1)]
        assert pieces[(x, 2)] == ("S", L), pieces[(x, 2)]
        assert pieces[(x, 3)] == ("F", 0)
    assert flat[(1, 0)] == 2 * L

    # a band drawn two columns wide over a one-level fall: the fall shared out
    pieces = {(x, 0): ("F", 0) for x in range(4)}
    marks = {(x, 0): {"level": L, "piece": None} for x in (1, 2, 3)}
    c = FakeCells(band={(1, 0): "W", (2, 0): "W"})
    with patch.object(vt, "is_face", lambda cc, cell: False):
        vt.apply_marks(c, pieces, {}, {}, marks)
    assert pieces[(2, 0)] == ("W:8", L), pieces[(2, 0)]
    assert pieces[(1, 0)] == ("W:8", L - 8), pieces[(1, 0)]
    assert pieces[(3, 0)] == ("F", L)

    # a piece given is taken as given; a ledge is never marked
    pieces = {(0, 0): ("F", 0), (1, 0): ("F", 0)}
    marks = {(0, 0): {"level": L, "piece": "S"}, (1, 0): {"level": L, "piece": None}}
    with patch.object(vt, "is_face", lambda cc, cell: False):
        marked = vt.apply_marks(FakeCells(keep={(1, 0)}), pieces, {}, {}, marks)
    assert marked == {(0, 0)}
    assert pieces == {(0, 0): ("S", L), (1, 0): ("F", 0)}, pieces

    # a neighbour left apart from the marks follows the levels; one that
    # still meets them is left as it was
    pieces = {(x, y): ("F", 0) for x in range(3) for y in range(2)}
    pieces[(2, 1)] = ("E", 0)          # meets nothing marked: kept
    marks = {(0, 0): {"level": L, "piece": None}}
    with patch.object(vt, "is_face", lambda cc, cell: False):
        vt.apply_marks(FakeCells(), pieces, {}, {}, marks)
    assert pieces[(0, 0)] == ("cSE", L), pieces[(0, 0)]
    assert pieces[(2, 1)] == ("E", 0)


def test_unknown_piece_refused():
    class Members:
        members = [("LAYOUT_LAVARIDGE_TOWN", 0, 0)]
    fixes = {"layouts": {"LAYOUT_LAVARIDGE_TOWN": {"terraces": {"1,2": {"piece": "Z"}}}}}
    try:
        vt.load_marks(Members(), fixes)
    except ValueError:
        return
    raise AssertionError("an unknown piece was taken")


def test_real_map():
    # Lavaridge: a cell of its south lawn raised a level by hand stands so,
    # and the solve without marks is the drawing's own
    lid = "LAYOUT_LAVARIDGE_TOWN"
    auto = vt.solve_auto(lid)
    cell = (10, 17)
    top = auto["pieces"][cell][1]
    sol = vt.finish(auto, {cell: {"level": top + L, "piece": None}})
    assert sol["pieces"][cell] == ("S", top + L), sol["pieces"][cell]
    assert sol["marked"] == {cell}
    assert vt.finish(auto, {})["pieces"] == auto["pieces"]
    # a mark's level counts from the group's main ground, wherever the world
    # placed it
    zero = vt.zero_of(auto)
    fixes = {"layouts": {lid: {"terraces": {"10,17": {"level": (top - zero) // L + 1}}}}}
    assert vt.load_marks(auto["cells"], fixes, zero=zero) == {cell: {"level": top + L, "piece": None}}
    shifted = vt.load_marks(auto["cells"], fixes, zero=zero - L)
    assert shifted == {cell: {"level": top, "piece": None}}, shifted


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
