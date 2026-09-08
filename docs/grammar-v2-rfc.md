# ICAD language v2 RFC: industrial, agent-readable CAD

Status: **proposed, not accepted by the production compiler**.

The production syntax is [grammar/icad.ebnf](../grammar/icad.ebnf). The
complete proposed surface is
[grammar/icad-v2-proposal.ebnf](../grammar/icad-v2-proposal.ebnf). This RFC
defines the semantic contract that must exist behind that syntax. Agents must
query `icad.language` and emit only capabilities advertised by the running
compiler. The corresponding lexer, compiler-pass, native-kernel, concurrency,
and incremental execution design is in the
[v2 compiler and engine architecture](compiler-v2-architecture.md).

## 1. Design objective

ICAD must describe how an industrial CAD designer constructs and verifies a
product, not merely how a renderer draws a collection of meshes. The language
therefore records five connected forms of intent:

1. **Design intent:** parameters, datums, dimensions, and constraints.
2. **Construction intent:** ordered sketches and solid features.
3. **Product intent:** parts, occurrences, interfaces, mates, and joints.
4. **Engineering intent:** materials, processes, tolerances, and validation.
5. **Communication intent:** scenes, drawings, BOMs, exports, and agent views.

The compiled model is a dependency graph. Every visible face must be traceable
to the sketch entity and feature that generated it. Every motion must identify
the occurrence it moves, its parent, its anchor, its axis, and its limits.

```text
requirements/imports
        |
parameters + datums + materials + appearances
        |
sketch workspaces -> solved regions -> ordered body features
        |                                  |
        +------------ provenance ----------+
                                           |
parts + interfaces -> occurrences -> mates/joints
                                           |
validation + simulation + scenes + drawings
                                           |
             visual.json + exchange outputs
```

Meshes are derived output. They are never the source of design truth.

## 2. Language properties

The v2 language keeps the existing line-oriented block style because it is
compact, diffable, and easy for an agent to repair. It adds the following
properties:

- typed expressions rather than literal-only dimensions;
- qualified names such as `bracket.base.top`;
- explicit named shape paths inside a sketch;
- ordered, immutable feature history;
- semantic topology references with provenance;
- separate body, part, and assembly scopes;
- separate physical material and visible appearance;
- compiled validation and drawing declarations;
- capability negotiation so an agent never guesses parser support.

The language is declarative about desired design state, but ordered wherever
order affects CAD history. A `PAD` can only consume an earlier solved sketch;
a face sketch can only reference topology produced earlier in its body; an
assembly can only instantiate a previously declared or imported part.

## 3. Capability negotiation

A v2 document states its minimum language and optional feature requirements:

```icad
REQUIRES ICAD 2.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH
REQUIRES CAPABILITY PERSISTENT_TOPOLOGY
REQUIRES CAPABILITY FEATURE_SHELL
REQUIRES CAPABILITY ASSEMBLY_JOINTS
```

The compiler must reject a missing capability before parsing dependent syntax
and report one actionable diagnostic. `icad.language` returns:

- implemented language versions;
- implemented capability names;
- syntax summaries and unit families;
- `visual.json` schema version;
- supported export formats and validation levels.

No plugin, LSP, MCP tool, or agent prompt may treat this RFC as implemented
until those capability flags exist.

Implementation status: the version/capability header mechanism is now current
production syntax at language version `1.0`. The registry advertises only
implemented v1 contracts. Proposed names in the table below still fail with
`ICAD-C0003` until their complete vertical compiler and engine gates land.
The bounded production subset is advertised as `MULTI_SHAPE_SKETCH_V1`; the
broader `MULTI_SHAPE_SKETCH` capability in this RFC remains proposed.

Initial capability families should be:

| Capability | Contract |
|---|---|
| `EXPRESSIONS` | Typed arithmetic and parameter references |
| `MULTI_SHAPE_SKETCH` | Named open and closed paths in one workspace |
| `ADVANCED_SKETCH_ENTITIES` | Ellipse, spline, slot, polygon, projection |
| `ADVANCED_SKETCH_CONSTRAINTS` | Tangency, equality, symmetry, dimensional families |
| `PERSISTENT_TOPOLOGY` | Stable semantic face/edge references and provenance |
| `ADVANCED_FEATURES` | Shell, rib, draft, hole, circular/table patterns |
| `PART_STRUCTURE` | Parts, interfaces, physical properties |
| `ASSEMBLY_JOINTS` | Occurrences, mates, joints, subassemblies |
| `ENGINEERING_MATERIALS` | Physical properties independent of appearance |
| `MANUFACTURING_VALIDATION` | Process checks and rule-bound diagnostics |
| `ASSOCIATIVE_DRAWINGS` | Views, dimensions, GD&T, and BOM |

## 4. Names, scopes, and references

Every declaration has a stable name. Names are unique in their containing
scope and become qualified as the model is lowered:

```text
project
  part
    body
      sketch
        shape
          entity or point
      feature
        face, edge, or vertex
  assembly
    occurrence
      interface
```

Examples are `bracket.mounting.base_sketch.outer.bottom` and
`robot.shoulder.axis`. The compiler resolves an unqualified reference only
when exactly one visible declaration matches. Ambiguity is an error with a list
of valid qualified alternatives.

Names are human-facing handles, not persistent topology IDs. The compiler also
assigns a deterministic entity ID based on source identity and generation
provenance. Renaming a source symbol can change its human path but must not
randomly reorder unrelated entity IDs.

Imports are project-relative, sandboxed, cycle-checked, content-hashed, and
restricted to `.icad`. `IMPORT` creates a module namespace; `INJECT` expands a
trusted project fragment into the current scope. Neither form executes code,
shell commands, native libraries, or plugins.

## 5. Values, units, and expressions

**Current v1 boundary:** the production compiler now advertises
`PARAMETER_EXPRESSIONS_V1` for `PARAMETER`, `ANGLE`, and feature-property
expressions, and `QUALIFIED_VALUE_REFERENCES_V1` for project-qualified scalar
references. It supports precedence, unary signs, parentheses, forward
parameter dependencies, canonical unit evaluation, and stable diagnostics.
Multiplication must currently have one dimensionless operand; division accepts
a dimensionless divisor or equal dimensions. Derived area/volume dimensions,
functions, ranges, and qualified topology references remain proposed.

All physical values are typed. Addition and comparison require compatible
dimensions; multiplication and division derive dimensions; trigonometric
functions, when introduced, accept angles explicitly.

A numeric literal used as a physical value needs a unit. A parameter expression
such as `pivot_diameter / 2` carries its inferred length type and therefore does
not repeat `mm`. Bare numeric literals are allowed only in dimensionless
contexts such as color channels, ratios, counts, and normalized vectors.

```icad
PARAMETER plate_width 120 mm RANGE 80 mm 180 mm
PARAMETER wall 8 mm RANGE 4 mm 16 mm
PARAMETER bore_pitch plate_width - 2 * wall
ANGLE draft_angle 2 deg RANGE 0 deg 7 deg
```

The semantic evaluator must reject:

- addition of length and angle;
- use of a negative radius or wall thickness;
- division by zero;
- a parameter dependency cycle;
- a value outside its declared range;
- use of a scene-time unit as a model dimension.

Evaluation is deterministic and side-effect free. Expression nodes retain
source spans so an agent sees whether a bad result came from a parameter or a
feature.

## 6. Sketch workspaces and shapes

A sketch is a planar constraint system. It owns a support, local coordinate
frame, named shapes, entities, points, projected references, and constraints.
It is analogous to an SVG document only in the sense that it contains multiple
named paths. Unlike SVG, it is dimensional, constrained, tolerance-aware, and
must produce manufacturable regions.

**Current v1 boundary:** `MULTI_SHAPE_SKETCH_V1` implements named
`OPEN|CLOSED` shapes with `STOCK`, `ADDITIVE`, `HOLE`, or `CONSTRUCTION` roles;
named points, lines, circular arcs, and circles; qualified `shape.point`
constraints from the existing constraint family; `SOLVE FULL|ALLOW_UNDER`;
and explicit `sketch.shape` PAD/POCKET inputs. Open paths are construction-only.
The compiler rejects invalid chains, self-intersection, boundary contact, and
holes without exactly one stock/additive container.
`SKETCH_REGION_ARRANGEMENT_V1` is also current for one explicit `OUTER` plus
optional `HOLES`, consumed as `sketch.region` by a single PAD/POCKET.
`ADVANCED_SKETCH_CONSTRAINTS_V1` is current for `H_DISTANCE`, `V_DISTANCE`,
`PARALLEL`, `PERPENDICULAR`, `EQUAL_LENGTH`, `CONCENTRIC`, `EQUAL_RADIUS`,
`MIDPOINT`, and `SYMMETRIC ... ABOUT ...`.
`SKETCH_LINE_ARC_TANGENCY_V1` is current for `TANGENT line arc AT
shared_point`, where the contact is an endpoint common to a line and a
non-full-circle arc. Role selectors, ellipse/spline/slot entities, full-circle
tangency, arc-arc tangency, and projected-edge tangency remain proposal syntax.
`PERSISTENT_FACE_REFERENCES_V1` is current for planar cap references using
`feature.face.top|bottom`, optional body-local `FACE` aliases, strict source
order, and canonical topology IDs. Side faces, edges, plural queries, and
geometric fallback selection remain proposal syntax.

`SEMANTIC_EDGE_LOOP_SELECTION_V1` is current for `SELECT EDGE TOP|BOTTOM
INNER|OUTER` on circular annular results. It feeds native `FILLET` and
`CHAMFER`, retains semantic selection intent, and exposes the valid operation
list in agent visual feedback. General edge queries, chains, shell, offset
face/edge, draft, split, and project remain proposed.

`TOPOLOGY_QUERY_V1` is current for a named selection with the exact predicate
set `EDGES WHERE`, `LOOP`, `CIRCULAR`, `CONCAVE|CONVEX`, and `ADJACENT_TO FACE
top|bottom`, consumed through `SELECT EDGESET name`. The resolver currently
accepts one circular loop on an annular REGION extrusion and requires the
modifier to immediately follow its source result. Its stable topology ID,
match evidence, applicability, and rejection reasons are serialized for the
agent. Boolean-history remapping, arbitrary chains, geometric fallback, and
plural cardinality remain proposed.

```icad
SKETCH base_layout ON PLANE XY
  SHAPE outer CLOSED ROLE STOCK
    POINT p0 -60 mm -40 mm FIXED
    POINT p1  60 mm -40 mm
    POINT p2  60 mm  40 mm
    POINT p3 -60 mm  40 mm
    LINE bottom FROM p0 TO p1
    LINE right  FROM p1 TO p2
    LINE top    FROM p2 TO p3
    LINE left   FROM p3 TO p0
  END

  SHAPE bore_left CLOSED ROLE HOLE
    POINT center -45 mm 0 mm
    CIRCLE rim CENTER center RADIUS 6 mm
  END

  SHAPE bore_right CLOSED ROLE HOLE
    POINT center 45 mm 0 mm
    CIRCLE rim CENTER center RADIUS 6 mm
  END

  REGION base_material
    OUTER outer
    HOLES bore_left bore_right
  END

  CONSTRAINT pair EQUAL_RADIUS bore_left.rim bore_right.rim
  CONSTRAINT centered SYMMETRIC bore_left.center bore_right.center ABOUT y_axis
  CONSTRAINT pitch H_DISTANCE bore_left.center bore_right.center 90 mm
  SOLVE FULL
END
```

### 6.1 Path and region rules

- A closed shape has one ordered boundary. Nested closed shapes form islands or
  holes by explicit role and containment, not accidental winding alone.
- An open shape may drive a sweep, rib, split, engraving, or construction.
- Every path endpoint is named or deterministically generated.
- Adjacent entities must meet within model tolerance.
- Self-intersection, zero-length entities, duplicate edges, and ambiguous
  nesting are errors.
- Analytic lines, arcs, circles, and ellipses remain analytic through solving
  and feature generation. Tessellation happens only for display or mesh export.
- A spline retains degree, knots, control data, and fit tolerance.

### 6.2 Constraint solver contract

Constraints are not parser decoration. The sketch solver returns:

- status: `fully_constrained`, `under_constrained`, `over_constrained`, or
  `inconsistent`;
- remaining translational and rotational degrees of freedom;
- maximum numerical residual in model units;
- conflicting constraint names and their source spans;
- movable entities and a small suggested repair set;
- solved entity geometry and region classification.

`SOLVE FULL` turns remaining degrees of freedom into a compiler error.
`ALLOW_UNDER` permits exploratory design but emits a warning. A feature may not
consume an inconsistent sketch.

Projected geometry records the source topology ID and whether association is
live. If regeneration invalidates an associative projection, the dependent
sketch becomes invalid rather than silently freezing stale geometry.

## 7. Ordered feature history

A body represents one continuous manufacturable solid. Its items execute in
source order and each feature produces an immutable history result. The active
body is the latest successful result.

```icad
BODY bracket_body
  MATERIAL aluminum_6061
  APPEARANCE anodized_blue

  SKETCH base_layout ON PLANE XY
    # named shapes and constraints
  END
  PAD base FROM base_layout.outer DEPTH 12 mm NEW
  POCKET mounting_bores FROM base_layout[ROLE HOLE] THROUGH ALL

  SKETCH ear_layout ON FACE base.TOP
    # ear profile and shaft hole
  END
  PAD ears FROM ear_layout.ear_profiles DEPTH 48 mm ADD
  POCKET shaft_bore FROM ear_layout.shaft_holes THROUGH ALL

  FILLET edge_relief ON shaft_bore
    SELECT EDGES GENERATED_BY ears ROLE SIDE
    RADIUS 3 mm
  END
END
```

Operation families mirror the way a designer works:

| Family | Operations |
|---|---|
| Form | pad, revolve, sweep, loft, rib, shell, thicken |
| Remove | pocket, typed hole, groove, split |
| Modify | fillet, chamfer, draft, offset face, move face |
| Repeat | linear, circular, mirror, table-driven pattern |
| Reference | datum point, axis, plane, coordinate system |

Feature termination is explicit: fixed depth, symmetric depth, through all, or
up to a stable topology reference. `NEW`, `ADD`, `CUT`, and `INTERSECT` state
the intended material operation. The compiler never guesses a boolean from
visual overlap.

Each feature produces:

- a parameter snapshot and input dependency list;
- success/failure state and diagnostic set;
- exact or analytic geometry owned by the native engine;
- mass and bounding-box deltas;
- generated, modified, consumed, and deleted topology maps;
- preview mesh revisions keyed by stable component and feature IDs.

## 8. Persistent topology and selection

Industrial history fails if a later fillet means “edge number 14” and an
earlier edit changes enumeration. V2 uses semantic selection plus provenance:

```icad
SELECT EDGES GENERATED_BY ears ROLE SIDE
SELECT FACES TANGENT_TO housing.outer_wall
SELECT FACE bracket_body.base.top
SELECT EDGE NEAREST service_point
```

Each topology item records:

- stable ID and human semantic path;
- generating feature and source sketch entities;
- geometric class: plane, cylinder, cone, NURBS, line, circle, and so on;
- role: top, bottom, side, start, end, inner, outer, profile edge;
- adjacency and orientation;
- creation, modification, split, merge, and deletion history.

Selection cardinality is part of semantics. A singular `FACE` query must
resolve exactly once. A plural query may resolve a stable ordered set. A
changed count can be a warning or error depending on the consuming operation.
Fallback geometric queries such as `NEAREST` must report their resolved stable
IDs in `visual.json`; they are not hidden guesses.

## 9. Bodies, parts, and assemblies

These constructs have separate meanings:

- **Body:** one continuous solid and its feature history.
- **Part:** a manufactured definition containing one or more bodies, material,
  metadata, interfaces, and process intent.
- **Occurrence:** one placed instance of a part inside an assembly.
- **Assembly:** a product graph of occurrences, subassemblies, mates, and
  joints.

```icad
PART shoulder_bracket
  PART_NUMBER "ICAD-ARM-110"
  DESCRIPTION "Machined shoulder clevis"
  BODY housing
    # feature history
  END
  INTERFACE base_mount FACE housing.base.bottom
  INTERFACE pivot_axis AXIS housing.shoulder_axis
  MANUFACTURING MILLING
    MIN_WALL 4 mm
    MIN_RADIUS 2 mm
    TOLERANCE_CLASS ISO_2768_m
  END
END

ASSEMBLY robot_arm
  OCCURRENCE base OF pedestal FIXED
  OCCURRENCE shoulder OF shoulder_bracket
  OCCURRENCE upper OF upper_link
  OCCURRENCE forearm OF forearm_link

  MATE seat COINCIDENT base.top_mount shoulder.base_mount
  JOINT shoulder_pitch REVOLUTE shoulder.pivot_axis upper.shoulder_axis
    VALUE 0 deg
    LIMIT -110 deg 135 deg
    HOME 0 deg
  END
  JOINT elbow_pitch REVOLUTE upper.elbow_axis forearm.elbow_axis
    VALUE 25 deg
    LIMIT -145 deg 145 deg
  END
END
```

A mate removes placement degrees of freedom. A joint preserves intentional
motion. The assembly solver computes the parent-child graph, rejects cycles in
the kinematic tree unless a closed-loop solver is explicitly requested, and
reports free, constrained, over-constrained, or disconnected occurrences.

Joint evaluation always transforms the declared child subtree around the
resolved anchor and axis. It must not rotate the entire model or detach the
child. For each sampled pose the engine reports attachment gap, angular error,
limits, clearance, interference, and transformed bounds.

## 10. Material, appearance, and embedded textures

Physical material and visual appearance are independent:

```icad
MATERIAL compressor_blade
  PROFILE ATI_TI_6AL_4V_GRADE5_BAR
  PRESET TITANIUM
END

APPEARANCE anodized_blue
  BASE_COLOR 0.04 0.18 0.55 1.0
  METALLIC 0.85
  ROUGHNESS 0.28
  TEXTURE anodized_micrograin SEED 3301 SCALE 0.35 mm
  UV_MODE BOX
END
```

`PROFILE` is the implemented v1 physical-material contract. It resolves a
versioned supplier/product-form record from `icad.material.catalog.v1` with
composition limit semantics, conditioned properties, product forms, source
URLs, document revisions, and limitations. `PRESET` remains appearance-only
and is optional when the profile supplies a default appearance.

The standard library should embed deterministic procedural presets for common
industrial materials: concrete, structural steel, stainless steel, tool
steel, cast iron, aluminum, brass, copper, titanium, asphalt, glass,
polycarbonate, ABS, nylon, rubber, wood, ceramic, carbon fiber, paint, powder
coat, and anodized finishes. A preset is versioned and content-hashed.

External textures are project-relative, sandboxed, decoded with bounded memory,
and packaged into exports. Texture files cannot change density, strength, or
manufacturing rules. Missing appearance data never blocks engineering export;
missing required physical material data blocks analyses that depend on it.
The catalog is explicitly curated rather than exhaustive: proprietary or
unpublished composition is recorded as undisclosed and is never reconstructed
from generic handbook values.

## 11. Manufacturing and validation

Manufacturing intent is attached to a part, then checked against exact body
geometry. Rules include minimum wall/web, minimum internal radius, tool access,
draft, overhang, hole standards, tolerance class, and process-specific feature
limits.

Validation is source code, not a manual checklist:

```icad
VALIDATE release_gate
  CHECK FULLY_CONSTRAINED TARGET shoulder_bracket
  CHECK MANIFOLD TARGET shoulder_bracket
  CHECK MIN_WALL TARGET shoulder_bracket LIMIT 4 mm
  CHECK INTERFERENCE TARGET robot_arm
  CHECK CLEARANCE TARGET robot_arm LIMIT 1.5 mm
  CHECK JOINT_LIMITS TARGET robot_arm
  CHECK DISCONNECTED TARGET robot_arm
END
```

Each result contains status, measured value, allowed value, implicated stable
entities, source spans, and repair guidance. The CLI can promote warnings to
errors for CI. A release profile should require clean geometry, assembly,
export read-back, and drawing association checks.

Static or thermal simulation syntax may describe loads and boundary conditions,
but `SIMULATION` is valid only when an engine with that advertised capability
is installed. ICAD must not fabricate FEA results from a render mesh.

## 12. Associative drawings and documentation

Drawings reference exact model topology and assembly structure:

```icad
DRAWING bracket_sheet OF shoulder_bracket
  SHEET A3 SCALE 1:2
  VIEW front FRONT AT 70 90 STYLE VISIBLE
  VIEW iso ISOMETRIC AT 220 90 STYLE SHADED
  DIMENSION bore DIAMETER housing.shaft_bore.inner TOLERANCE 0.02 mm
  GDT pivot_position POSITION 0.05 mm DATUMS datum_a datum_b datum_c
  BOM arm_bom OF robot_arm
END
```

Dimensions attach to semantic topology IDs. Regeneration updates their values
and projected positions. A deleted reference becomes a drawing diagnostic; it
never silently points at a nearby edge. The drawing exporter consumes this
associative representation to produce SVG/DXF/PDF and a structured inspection
record.

## 13. Scenes and mechanism animation

Scenes describe presentation state and mechanism state without modifying part
history:

```icad
SCENE articulation
  DURATION 6 s
  FPS 60
  BACKGROUND studio_dim
  LIGHT key DIRECTIONAL COLOR 1.0 0.94 0.86 INTENSITY 4.0
  TRACK shoulder_motion JOINT robot_arm.shoulder_pitch
    EASING EASE_IN_OUT
    KEYFRAME 0 s VALUE -35 deg
    KEYFRAME 3 s VALUE 55 deg
    KEYFRAME 6 s VALUE -35 deg
  END
  TRACK elbow_motion JOINT robot_arm.elbow_pitch
    KEYFRAME 0 s VALUE 20 deg
    KEYFRAME 3 s VALUE 110 deg
    KEYFRAME 6 s VALUE 20 deg
  END
END
```

The evaluator samples tracks in dependency order, applies joint values to the
correct child subtrees, then runs requested clearance/interference checks.
Rendering may interpolate frames, but engineering checks use deterministic
sample times. Camera animation is independent of assembly motion.

## 14. `visual.json` v2: the agent's spatial feedback

`visual.json` is a compact, deterministic explanation of the compiled result.
It must let an agent detect a disconnected arm, reversed link, missing hole, or
incorrect proportion without reading millions of triangles.

Top-level sections should be:

```json
{
  "schema": "icad.visual/2",
  "source_revision": "content-hash",
  "units": "mm",
  "capabilities": [],
  "parameters": {},
  "sketches": [],
  "feature_history": [],
  "topology": {},
  "parts": [],
  "assembly": {},
  "materials": [],
  "appearances": [],
  "validation": [],
  "scenes": [],
  "views": {},
  "diagnostics": []
}
```

For each sketch it includes support, shapes, entities, solved coordinates,
constraint status, residuals, and degrees of freedom. For each feature it
includes dependencies, operation, result bounds, topology provenance, and mass
delta. For each occurrence it includes local/world transform, parent, children,
interfaces, connection state, and bounds.

The `views` section provides front, right, top, and isometric camera matrices,
silhouette/depth statistics, visible component IDs, projected bounds, and
optional raster artifact paths. A component screen-space ID buffer enables
click selection in the viewer and allows an agent to correlate image pixels
with semantic objects.

Large projects use content-addressed chunks. Unchanged sketch, feature,
topology, mesh, and view chunks are reused across revisions. A live compile
response reports parsed, solved, rebuilt, reused, and invalidated counts plus
wall time and peak memory.

## 15. Compiler architecture required by the grammar

The syntax is only useful when every stage understands it:

```text
source resolver
  -> Unicode lexer and token spans
  -> parser and scoped AST
  -> name/type/unit resolution
  -> sketch region builder and constraint solver
  -> ordered feature dependency graph
  -> native exact/analytic geometry engine
  -> persistent topology/provenance mapper
  -> part and assembly solver
  -> validation and scene evaluator
  -> visual.json, drawings, STEP, STL, OBJ, DWG
```

The native engine API must be re-entrant. An immutable compile context owns one
revision; shared caches are content-addressed and publish complete immutable
entries. Independent parts and unaffected feature subgraphs may compile in
parallel. A failed or cancelled compile cannot mutate the last valid revision.

Incremental invalidation follows dependencies:

- editing one sketch invalidates its solver result and downstream features in
  the same body;
- changed topology invalidates dependent sketches, selections, drawings, and
  assembly interfaces;
- changing only appearance invalidates render data, not exact geometry;
- changing a joint value invalidates assembly transforms and scene checks, not
  part geometry;
- changing a drawing layout does not rebuild the model.

## 16. Diagnostics

Diagnostics require stable codes, severity, source range, owning declaration,
related ranges, structured arguments, and one or more safe fixes. Suggested
families are:

| Prefix | Area |
|---|---|
| `ICAD-L2` | lexical syntax and invalid Unicode/token forms |
| `ICAD-P2` | grammar and block recovery |
| `ICAD-N2` | scope, import, and reference resolution |
| `ICAD-U2` | units, expressions, and parameter ranges |
| `ICAD-K2` | sketch entities, regions, and constraint solving |
| `ICAD-F2` | feature history and exact geometry |
| `ICAD-T2` | persistent topology and selection cardinality |
| `ICAD-A2` | part, mate, joint, and assembly solving |
| `ICAD-M2` | material and manufacturing rules |
| `ICAD-D2` | drawing association and documentation |
| `ICAD-V2` | validation and simulation |

Error recovery must preserve the last valid body or part preview and mark stale
results. The viewer must never label a stale mesh as the current successful
compile.

## 17. Implementation and acceptance order

The proposal must land in dependency order:

1. Lexical punctuation, strings, qualified names, expressions, version and
   capability headers.
2. Scoped AST/IR, typed values, deterministic imports, and new diagnostics.
3. Multi-shape sketch topology and region classification.
4. Constraint graph, solver state, residuals, and agent-readable fixes.
5. Ordered body history and feature extent semantics.
6. Persistent topology and provenance-based selections.
7. Advanced features: hole, rib, shell, draft, and patterns.
8. Part/interface structure, engineering materials, and appearances.
9. Assembly occurrences, mates, joints, motion, and interference.
10. Validation, associative drawings, `visual.json` v2, and agent view renders.
11. Formatter, LSP, MCP, viewer, VS Code extension, plugin grammar snapshot,
    examples, fuzzing, benchmarks, and release packaging.

Each stage is done only when lexer, parser, semantic, engine, serialization,
diagnostics, formatter/LSP, documentation, positive tests, negative tests,
incremental tests, sanitizer tests, and benchmark thresholds agree. Syntax is
not advertised merely because the parser accepts its keywords.

## 18. Compatibility and migration

Existing point-only sketches, `PROFILE`, primitive `FEATURE`, `BODY`, `MATE`,
`JOINT`, and scene syntax remain readable under the current language version.
The formatter may offer explicit migrations:

- legacy polygon sketch -> one `SHAPE ... CLOSED ROLE STOCK`;
- implicit feature property -> typed operation field;
- extremum selector -> semantic topology query;
- body occurrence -> `PART` plus `OCCURRENCE`;
- render material preset -> engineering material plus appearance.

Migration emits a semantic diff containing body count, volume, mass, bounds,
topology correspondence, assembly transforms, and drawing reference changes.
An automatic fix must not be applied if those invariants change unexpectedly.

## 19. Integrated proposed example

This shortened bearing bracket demonstrates the intended flow in one source.
It is proposal syntax and is deliberately not placed in `examples/` until the
corresponding capability gates compile it.

```icad
REQUIRES ICAD 2.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH
REQUIRES CAPABILITY PERSISTENT_TOPOLOGY
REQUIRES CAPABILITY ADVANCED_FEATURES
REQUIRES CAPABILITY PART_STRUCTURE
REQUIRES CAPABILITY ASSEMBLY_JOINTS

PROJECT bearing_bracket REVISION "A"
UNITS mm
TOLERANCE LINEAR 0.02 mm ANGULAR 0.1 deg

PARAMETER base_width 100 mm RANGE 80 mm 140 mm
PARAMETER base_depth 80 mm RANGE 60 mm 110 mm
PARAMETER base_thickness 12 mm RANGE 8 mm 18 mm
PARAMETER ear_gap 30 mm
PARAMETER ear_thickness 12 mm
PARAMETER pivot_diameter 20 mm

POINT3 world_origin 0 mm 0 mm 0 mm
POINT3 pivot_center 0 mm 0 mm 36 mm
VECTOR x_axis 1 0 0
VECTOR y_axis 0 1 0
VECTOR z_axis 0 0 1

MATERIAL aluminum_6061
  STANDARD ASTM_B209
  DENSITY 2.70 g_cm3
  ELASTIC_MODULUS 68.9 GPa
  POISSON_RATIO 0.33
  YIELD_STRENGTH 276 MPa
END

APPEARANCE anodized_blue
  BASE_COLOR 0.04 0.18 0.55 1.0
  METALLIC 0.85
  ROUGHNESS 0.28
  TEXTURE anodized_micrograin SEED 3301 SCALE 0.35 mm
  UV_MODE BOX
END

PART bracket
  PART_NUMBER "BRACKET-001"
  DESCRIPTION "Machined double-ear bearing bracket"

  BODY bracket_body
    MATERIAL aluminum_6061
    APPEARANCE anodized_blue

    SKETCH base_layout ON PLANE XY
      SHAPE outer CLOSED ROLE STOCK
        POINT p0 -50 mm -40 mm FIXED
        POINT p1  50 mm -40 mm
        POINT p2  50 mm  40 mm
        POINT p3 -50 mm  40 mm
        LINE bottom FROM p0 TO p1
        LINE right FROM p1 TO p2
        LINE top FROM p2 TO p3
        LINE left FROM p3 TO p0
      END
      SHAPE bolt_0 CLOSED ROLE HOLE
        POINT center -38 mm -28 mm
        CIRCLE rim CENTER center RADIUS 5 mm
      END
      SHAPE bolt_1 CLOSED ROLE HOLE
        POINT center 38 mm -28 mm
        CIRCLE rim CENTER center RADIUS 5 mm
      END
      SHAPE bolt_2 CLOSED ROLE HOLE
        POINT center 38 mm 28 mm
        CIRCLE rim CENTER center RADIUS 5 mm
      END
      SHAPE bolt_3 CLOSED ROLE HOLE
        POINT center -38 mm 28 mm
        CIRCLE rim CENTER center RADIUS 5 mm
      END
      CONSTRAINT equal_01 EQUAL_RADIUS bolt_0.rim bolt_1.rim
      CONSTRAINT equal_12 EQUAL_RADIUS bolt_1.rim bolt_2.rim
      CONSTRAINT equal_23 EQUAL_RADIUS bolt_2.rim bolt_3.rim
      SOLVE FULL
    END
    PAD base FROM base_layout.outer DEPTH base_thickness NEW
    POCKET bolts FROM base_layout[ROLE HOLE] THROUGH ALL

    SKETCH ears_layout ON FACE base.TOP
      SHAPE left_ear CLOSED ROLE ADDITIVE
        POINT center -21 mm 0 mm
        CIRCLE outer CENTER center RADIUS 24 mm
      END
      SHAPE right_ear CLOSED ROLE ADDITIVE
        POINT center 21 mm 0 mm
        CIRCLE outer CENTER center RADIUS 24 mm
      END
      SHAPE left_bore CLOSED ROLE HOLE
        POINT center -21 mm 0 mm
        CIRCLE rim CENTER center RADIUS pivot_diameter / 2
      END
      SHAPE right_bore CLOSED ROLE HOLE
        POINT center 21 mm 0 mm
        CIRCLE rim CENTER center RADIUS pivot_diameter / 2
      END
      CONSTRAINT outer_equal EQUAL_RADIUS left_ear.outer right_ear.outer
      CONSTRAINT bore_equal EQUAL_RADIUS left_bore.rim right_bore.rim
      CONSTRAINT left_concentric CONCENTRIC left_ear.outer left_bore.rim
      CONSTRAINT right_concentric CONCENTRIC right_ear.outer right_bore.rim
      CONSTRAINT gap H_DISTANCE left_ear.center right_ear.center ear_gap + ear_thickness
      SOLVE FULL
    END
    PAD ears FROM ears_layout[ROLE ADDITIVE] DEPTH 60 mm ADD
    POCKET pivot_bores FROM ears_layout[ROLE HOLE] THROUGH ALL

    SKETCH rib_path ON FACE base.TOP
      SHAPE centerline OPEN ROLE CONSTRUCTION
        POINT start 0 mm 0 mm FIXED
        POINT finish 0 mm 52 mm
        LINE spine FROM start TO finish
      END
      CONSTRAINT rib_vertical VERTICAL centerline.spine
      SOLVE FULL
    END
    RIB central_rib FROM rib_path.centerline THICKNESS 8 mm DIRECTION NORMAL ADD

    DATUM_AXIS pivot_axis AT pivot_center DIRECTION y_axis

    FILLET base_edges ON central_rib
      SELECT EDGES GENERATED_BY base ROLE SIDE
      RADIUS 3 mm
    END
  END

  INTERFACE mounting_face FACE bracket_body.base.bottom
  INTERFACE pivot_axis AXIS bracket_body.pivot_axis
  MANUFACTURING MILLING
    MIN_WALL 4 mm
    MIN_RADIUS 2 mm
    TOLERANCE_CLASS ISO_2768_m
  END
END

PART standard_shaft
  PART_NUMBER "SHAFT-020"
  BODY shaft_body
    MATERIAL aluminum_6061
    SKETCH shaft_profile ON PLANE XZ
      SHAPE section CLOSED ROLE STOCK
        POINT center 0 mm 0 mm FIXED
        CIRCLE rim CENTER center RADIUS pivot_diameter / 2
      END
      SOLVE FULL
    END
    PAD shaft FROM shaft_profile.section SYMMETRIC DEPTH 80 mm NEW
    DATUM_AXIS axis AT pivot_center DIRECTION y_axis
  END
  INTERFACE axis AXIS shaft_body.axis
END

ASSEMBLY bracket_demo
  OCCURRENCE support OF bracket FIXED
  OCCURRENCE shaft OF standard_shaft
  MATE shaft_center CONCENTRIC support.pivot_axis shaft.axis
  JOINT shaft_rotation REVOLUTE support.pivot_axis shaft.axis
    VALUE 0 deg
    LIMIT -180 deg 180 deg
    HOME 0 deg
  END
END

VALIDATE release_gate
  CHECK FULLY_CONSTRAINED TARGET bracket
  CHECK MANIFOLD TARGET bracket
  CHECK MIN_WALL TARGET bracket LIMIT 4 mm
  CHECK INTERFERENCE TARGET bracket_demo
  CHECK JOINT_LIMITS TARGET bracket_demo
  CHECK DISCONNECTED TARGET bracket_demo
END

DRAWING manufacturing_sheet OF bracket
  SHEET A3 SCALE 1:2
  VIEW front FRONT AT 70 90 STYLE VISIBLE
  VIEW iso ISOMETRIC AT 220 90 STYLE SHADED
  DIMENSION pivot_size DIAMETER bracket_body.pivot_bores.inner TOLERANCE 0.02 mm
  BOM parts OF bracket_demo
END

SCENE articulation
  DURATION 4 s
  FPS 60
  TRACK shaft_motion JOINT bracket_demo.shaft_rotation
    EASING EASE_IN_OUT
    KEYFRAME 0 s VALUE -90 deg
    KEYFRAME 2 s VALUE 90 deg
    KEYFRAME 4 s VALUE -90 deg
  END
END
```

This creates the needed agent loop: describe intent, compile, inspect structured
spatial evidence, render exact views, measure errors, revise only the failing
dependency, and repeat until the validation gate passes.
