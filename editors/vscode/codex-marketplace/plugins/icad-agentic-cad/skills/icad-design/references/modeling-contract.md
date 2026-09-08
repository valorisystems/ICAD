# ICAD modeling contract

Use the dependency chain below for every manufactured or articulated design:

```text
parameters/datums -> sketch workspace -> named path entities -> feature history
-> body -> manufacturing interfaces -> connections/joints -> collision review
-> scene -> visual feedback
```

## Procurement and BOM contract

Model one body definition plus assembly occurrences for repeated manufactured
items. Use explicit connection `QUANTITY` values for bolts, anchors, and welds;
visual bolt cylinders are not a quantity source. Bind every material to a
source-traceable `PROFILE`, then validate `icad bom-json` for:

- grouped line-item quantity and occurrence list;
- volume, density or supplier areal-mass basis, calculated mass, and nulls where
  no defensible material property exists;
- fastener nominal diameter, declared length and property class, with missing
  finish/nut/washer fields retained as procurement blockers;
- weld deposited volume, deposition efficiency, filler mass, supplier package
  mass, and rounded-up package quantity;
- English/French descriptions and limitations.

Treat `procurementReady:false` as an actionable controlled-data result. Do not
replace missing architectural, structural, supplier, or test evidence with an
agent estimate.

## Current sketch grammar

The compiler supports legacy one-contour sketches and, when
`MULTI_SHAPE_SKETCH_V1` is advertised, multiple named paths in one workspace.
Points define coordinates; ordered entities define topology. Point declaration
order is not the path.

```icad
SKETCH plate ON PLANE XY
POINT a 0 mm 0 mm FIXED
POINT b 80 mm 0 mm FIXED
POINT c 80 mm 50 mm FIXED
POINT d 0 mm 50 mm FIXED
LINE bottom FROM a TO b
LINE right FROM b TO c
LINE top FROM c TO d
LINE left FROM d TO a
END
PAD plate_solid FROM plate DEPTH 8 mm NEW
```

An arc is `ARC name FROM start TO end CENTER center CW|CCW`. Its endpoint radii
must match and the full entity list must close end-to-start. A sketch may
instead contain one `CIRCLE center-x center-y radius`.

For multi-shape parts, declare `SHAPE name CLOSED ROLE STOCK|ADDITIVE|HOLE`
or an open/closed `CONSTRUCTION` path, then select the exact result as
`sketch.shape`. When `SKETCH_REGION_ARRANGEMENT_V1` is advertised, declare one
outer plus optional holes in a named `REGION` and consume `sketch.region`.
When `ADVANCED_SKETCH_CONSTRAINTS_V1` is advertised, use the qualified
dimensional, line, circular, midpoint, and symmetry families in the EBNF. When
`SKETCH_LINE_ARC_TANGENCY_V1` is advertised, use `TANGENT line arc AT
shared_endpoint`. Use `SOLVE FULL` for released geometry. Richer curves and
broader tangency remain proposal-only.

When `SEMANTIC_EDGE_LOOP_SELECTION_V1` is advertised, a FILLET or CHAMFER may
use `SELECT EDGE TOP|BOTTOM INNER|OUTER` on the current circular annular solid.
Verify `selection.classification` and `selection.applicableOperations` in
`visual.json`. Shell, offset, split, project, and arbitrary chains are not yet
production operations.

When `TOPOLOGY_QUERY_V1` is advertised, prefer a named query over the direct
selector: `SELECTION name`, `FROM feature`, `EDGES WHERE`, `LOOP`, `CIRCULAR`,
`CONCAVE|CONVEX`, `ADJACENT_TO FACE top|bottom`, then `SELECT EDGESET name` in
the immediately following FILLET/CHAMFER. Verify `matchedTopologyId`,
`matchReason`, `applicability.allowed`, and every rejected-operation reason.
The production subset is one circular loop on an annular REGION extrusion; it
does not promise general topology remapping.

When `PERSISTENT_FACE_REFERENCES_V1` is advertised, name an earlier planar cap
with `FACE alias FROM feature.face.top|bottom` and attach the next sketch using
`ON FACE alias`. Direct `ON FACE feature.face.top|bottom` is also supported.
Confirm `supportTopologyId` in `visual.json`; never substitute a mesh index or
proximity guess.

## Manufacturing interfaces and magnetic seating

An assembly is not valid merely because two meshes touch. Declare the physical
contract on both occurrences and the manufacturing method that realizes it:

```icad
POINT3 shaft_seat 40 mm 0 mm 24 mm
VECTOR shaft_axis 1 0 0
VECTOR bore_axis -1 0 0
INTERFACE motor_shaft BODY motor AT shaft_seat AXIS shaft_axis TYPE SHAFT SIZE 12 mm
INTERFACE arm_bore BODY arm AT shaft_seat AXIS bore_axis TYPE BORE SIZE 12 mm
CONNECT shoulder_fit motor_shaft arm_bore METHOD SLIP_FIT STANDARD ISO_286 FIT H7_g6 CLEARANCE 0.02 mm AUTO
```

The production pairing rules are:

- bolted/screwed: mount, flange, or hole interfaces, with `FASTENER`;
- pinned: pin/hole or shaft/bore, with `FASTENER`;
- press/slip fit: pin/hole or shaft/bore, with `FIT`;
- bearing: shaft/bearing-seat or shaft/bore, with `FIT`;
- welded/brazed: weld-seam, mount, or flange interfaces;
- bonded: bond-face, mount, or flange interfaces.

Every `CONNECT` requires `STANDARD`. `AUTO` is deterministic seating evidence,
not a hidden transform: use the returned interface gap and axis alignment to
repair source datums or poses. A released connection must report `SEATED`, its
manufacturing validation must pass, and `interference-json` must show only the
contact intended by the chosen method. Use `JOINT` separately for kinematics;
the connection states how parts are manufactured together, while the joint
states what relative motion remains.

`interference-json` annotates each colliding body pair with its declared
connection, method, and standard. `declaredEngagementPartPairs` counts overlap
that has such a contract; `unintendedPenetratingPartPairs` counts anonymous
collision and must be zero. Treat this as the required second feedback pass
after all interfaces report `SEATED`.

## Acceptance

Compilation and body counts are necessary but not sufficient. Inspect:

1. four-view silhouette and per-body bounds;
2. feature history and the source sketch for each solid result;
3. every parent-child joint anchor and axis;
4. contact/attachment gaps at rest;
5. first, middle, and last animation frames;
6. interference, joint-limit, manifold, manufacturing, STEP, and STL read-back.
7. every connection's standard, fit/fastener, gap, alignment, and snap state.

If a reference image and output disagree visibly, the output fails even when
all numeric count tests pass. Repair datums, dimensions, and topology from the
source model; never hide the failure with camera changes or export settings.
