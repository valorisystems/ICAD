---
name: icad-design
description: Author, inspect, validate, and compile agentic 3D designs in the native ICAD language, including programmable geometry, materials, animation, constraints, assemblies, and manufacturing outputs.
---

# ICAD Design

Keep declarative `.icad` source as the design authority. Use the `icad` compiler
installed by the ICAD VS Code extension or available on `PATH`. Do not introduce
OpenCASCADE and do not disguise text or mesh data with another extension.

## Runtime bootstrap

Resolve both `icad` and `icad-viewer` before beginning a design workflow. If
either program is unavailable and network downloads are permitted, run the
plugin's checksum-verifying dependency installer from the plugin root:

```sh
# macOS or Linux
scripts/install.sh --dependencies-only

# Windows PowerShell
scripts/install.ps1 -DependenciesOnly
```

The installer selects the matching GitHub Release, verifies its published
SHA-256 file, and installs the compiler and native Qt viewer under the user's
application-data directory. Never download an unverified executable or place a
toolchain inside the design workspace. If automatic download is disabled or the
platform has no published package, stop and give the manual dependency steps
from the project README instead of pretending compilation succeeded.

## Dependency order

Declare parameters and typed angles, datums, sketch workspaces, named path
entities, ordered operations, materials and appearances, bodies/components,
named manufacturing interfaces, connections, poses, joints and constraints,
scenes, then exports. Prefer stable names and editable parameters that another
agent can inspect and revise.

## Raw prompt protocol

1. Call MCP `icad.agent.conceptualize` exactly once for a new request.
2. Expand its `engineeringPreparation` contract into a design preparation
   record before choosing geometry. Separate given facts, derived dimensions,
   reversible assumptions, and open decisions. Do not author source until the
   record covers function, envelope, BOM, per-part construction, interfaces,
   kinematics, tolerances, visual intent, deliverables, and measurable
   acceptance. Ask only when an open decision changes safety, external fit, or
   architecture; otherwise expose the assumption as a named parameter.
3. Create a requirement-to-entity traceability map. Every important requirement
   must lead to a planned parameter/body/feature/interface/joint and a visual or
   numeric check.
4. Emit only grammar advertised by the running `icad language` tool—no Markdown,
   prose, pseudocode, future syntax, or foreign CAD syntax. Design unfamiliar
   mechanism topology directly instead of forcing a maintained template.
5. Compile, then call `icad.visualize`. Use only its direct `icad.visual.snapshot.v1` object as visual feedback. Inspect its three 512x512 lossless PNG inputs in `imageOrder` (`front`, `right`, `top`) with model vision, then correlate them with the legend/body bounds, depth rasters, and joints.
6. Revise named ICAD entities from the visual evidence without a second concept pass. Use `icad.compare` only after two independently acceptable candidates exist.

Read [references/design-preparation.md](references/design-preparation.md) for
every new prompt-driven model. It defines the mandatory engineering brief,
traceability gate, and anti-placeholder quality rules.
Read [references/blueprint-concept-pass.md](references/blueprint-concept-pass.md) for image, drawing, and underspecified mechanical requests.
Read [references/modeling-contract.md](references/modeling-contract.md) before
authoring a manufactured part or articulated assembly. It defines the mandatory
interface/contact workflow that prevents floating or merely overlapping parts.
Read [references/reference-index.md](references/reference-index.md) when exact
grammar syntax or page-level blueprint interpretation is needed. The packaged
EBNF defines source structure; the packaged PDF is supporting design-reading
context and must not override `icad.language` from the running compiler.

## Validation workflow

Run these gates before claiming a design is complete:

```sh
icad check model.icad
icad diagnostics-json model.icad
icad validate model.icad
icad manufacturing model.icad
icad inspect-json model.icad
icad visual-json model.icad
icad compare-json first.icad second.icad
icad topology-json model.icad
icad bom-json model.icad
icad materials-json
icad build model.icad --output-dir build/icad/model
```

Read back the generated STEP and STL with `icad inspect-step` and
`icad inspect-stl`. A complete build includes STEP assembly, STL, OBJ, glTF,
GLB, 3MF, HTML viewer, scene data, BOM, manufacturing report, SVG, DXF, and
topology JSON.

For procurement-oriented models, inspect the `icad.bom.v2` output rather than
counting visible solids. Require English and French descriptions, grouped
definition quantities, occurrence identities, calculated mass and its method,
explicit connection quantities, parsed fastener diameter/length/property class,
and weld filler mass/package quantities. `procurementReady:false` is a valid and
necessary result whenever supplier profiles, bolt stack data, coatings, nuts,
washers, sheet fixings, laps, flashings, openings, or other ordered details are
not substantiated. Never invent a missing procurement value merely to clear the
gate. Query `icad material-json PROFILE_ID` and retain every supplier qualifier.

## Engineering evidence and controlled release states

When a project has an adjacent evidence manifest, inspect it as part of every
release-oriented validation pass:

```sh
icad evidence-json model.icad --manifest model.evidence.json
icad compliance-json model.icad --manifest model.evidence.json \
  --basis EASA-CS-E-AMENDMENT-8
icad compliance-report model.icad --manifest model.evidence.json \
  --format html --output build/compliance.html
```

The read-only MCP equivalents are `icad.evidence.inspect` and
`icad.compliance.inspect`. Treat a stale model revision, controlled-input
digest, material definition, load case, referenced-entity digest, or artifact
hash as a failed gate. Do not invent CFD, FEA, fatigue, thermal, test, or
certification results from display geometry.

Only `BENCHMARK`, `DEVELOPMENT`, `GROUND_TEST_RELEASED`, and
`DEMONSTRATOR_VALIDATED` are repository lifecycle states. Never select or claim
`TYPE_CERTIFIED`, airworthy, flight approved, or authority certified from ICAD
validation; those require external authority records and a separate program.

## Agentic modeling rules

- Query `icad.language` before authoring. A persistent source may begin with
  `REQUIRES ICAD 1.0` and one or more `REQUIRES CAPABILITY NAME` lines from the
  returned registry. Requirements must precede `PROJECT`; never use a proposed
  capability name that the running compiler does not advertise.
- When `PARAMETER_EXPRESSIONS_V1` is advertised, derive related dimensions with
  typed `+`, `-`, `*`, `/`, unary signs, and parentheses instead of duplicating
  constants. Use project-qualified scalar names only when
  `QUALIFIED_VALUE_REFERENCES_V1` is advertised. Read `visual.json.parameters`
  to confirm the canonical value, formula, and dependency list.
- When `MULTI_SHAPE_SKETCH_V1` is advertised, model one planar workspace with
  named `SHAPE name OPEN|CLOSED ROLE STOCK|ADDITIVE|HOLE|CONSTRUCTION` blocks.
  Use named `POINT`, `LINE`, `ARC`, and `CIRCLE` entities; qualify cross-shape
  constraints as `shape.point` or `shape.entity`; use `SOLVE FULL` for released
  geometry; and feed each closed shape explicitly to `PAD` or `POCKET` as
  `sketch.shape`.
  Read `visual.json.sketches[].shapes` to verify role, area, containment, and
  source entities. When `SKETCH_REGION_ARRANGEMENT_V1` is advertised, group one
  stock/additive `OUTER` and optional contained `HOLES` in a named `REGION`,
  consume it as `sketch.region`, and verify `visual.json.sketches[].regions`.
  When `ADVANCED_SKETCH_CONSTRAINTS_V1` is advertised, use the qualified
  dimensional, line, circle, midpoint, and symmetry constraint families. When
  `SKETCH_LINE_ARC_TANGENCY_V1` is advertised, use only
  `TANGENT line arc AT shared_endpoint`; full-circle and arc-arc tangency remain
  unavailable. Do not emit role selectors or spline/ellipse/slot entities.
- When `SEMANTIC_EDGE_LOOP_SELECTION_V1` is advertised, select a circular rim
  on the current annular result with `SELECT EDGE TOP|BOTTOM INNER|OUTER` and
  use it only with `FILLET` or `CHAMFER`. Confirm the classification and
  `applicableOperations` in `visual.json`; never substitute a mesh index.
- When `TOPOLOGY_QUERY_V1` is advertised, prefer a named `SELECTION` with
  `FROM feature`, `EDGES WHERE`, `LOOP`, `CIRCULAR`, `CONCAVE|CONVEX`, and
  `ADJACENT_TO FACE top|bottom`, then consume it with `SELECT EDGESET name`.
  Keep the FILLET/CHAMFER immediately after the annular REGION extrusion.
  Confirm `matchedTopologyId`, `matchReason`, allowed operations, and rejection
  reasons in `visual.json`; do not assume remapping through later history.
- Prefer CAD-style body history for new manufactured parts: `SKETCH name ON
  PLANE XY|XZ|YZ`, `PAD feature FROM sketch[.shape|.region] DEPTH value NEW`, then `SKETCH name
  ON FACE earlier_feature X_MIN|X_MAX|Y_MIN|Y_MAX|Z_MIN|Z_MAX` followed by
  `PAD ... ADD` or `POCKET ...`. Declare supports before use and inspect
  `visual.json.featureHistory` after compilation.
- When `PERSISTENT_FACE_REFERENCES_V1` is advertised, prefer
  `FACE alias FROM earlier_feature.face.top|bottom` followed by
  `SKETCH name ON FACE alias`, or use direct
  `ON FACE earlier_feature.face.top|bottom`. Confirm the canonical
  `supportTopologyId` in `visual.json`; side-face and edge selectors remain
  unavailable until separately advertised.
- Give every body-local sketch an explicit `ON` support and every
  non-construction region a later consuming operation. Correlate agent feedback
  by canonical `body::sketch[.shape|.region]` identifiers
  and preserved `PAD` or `POCKET` command; local sketch names may repeat across
  different bodies.
- Treat a sketch as a constrained 2D workspace and each shape as one ordered
  SVG-path-like contour. Declare every point before use, give every entity a
  stable name, close material-producing chains end-to-start, and inspect the
  emitted shapes/entities in `visual.json`. Keep legacy one-contour sketches
  valid, but prefer explicit shape roles for new multi-region parts.
- Use low-level `FEATURE` blocks only for operations without a history
  shorthand or while maintaining an existing model.
- For a newly generated industrial part, reject a feature-only result. At least
  one manufactured body must expose body-local `SKETCH` plus `PAD` or `POCKET`
  history, and `visual.json.sketches` must be non-empty. Treat a zero-sketch
  result as a redesign signal even when its mesh, assembly, and exports validate.
- Use named `POINT3`, `VECTOR`, `ANGLE`, `POSE`, and `JOINT` declarations for
  mechanisms instead of inferring structure from mesh vertices.
- When `MANUFACTURING_CONNECTIONS_V1` is advertised, give each mating side a
  named `INTERFACE ... BODY ... AT ... AXIS ... TYPE ... [SIZE ...]`, then use
  `CONNECT ... METHOD ... STANDARD ...`. Supply `FASTENER` for bolted, screwed,
  or pinned connections and `FIT` for fitted or bearing connections. Choose
  compatible pairs: shaft/bore, pin/hole, shaft/bearing-seat, flange/mount, or
  weld/bond faces as appropriate. Never use a connection name as a visual-only
  label.
- When `MAGNETIC_INTERFACE_SNAP_V1` is advertised, `AUTO` asks the engine to
  evaluate the candidate seat. Read `visual.json.connections[].gapMm`,
  `axisAlignment`, `snapState`, and `aligned`; `SEATED` is acceptance evidence,
  while `SNAP_REQUIRED` or `MISALIGNED` requires a source pose/datum repair.
  Then run `interference-json` and reject unintended penetration. AUTO never
  licenses a floating part or silently rewrites the authoritative source.
- In interference feedback, distinguish `declaredEngagementPartPairs` from
  `unintendedPenetratingPartPairs`. A bearing, pin, or fit can explain geometry
  engagement only when the body pair is tied to a compatible named connection
  with its method and standard. Delivery requires zero unintended penetrations;
  never excuse anonymous overlap as joint hardware.
- After every geometry or pose edit, call `icad.visualize` through MCP (or
  `icad visual-json`) and inspect all three ordered 512x512 lossless PNGs with
  model vision. Use the depth rasters and legend to resolve identity. Reject a
  poor silhouette, misplaced component, or unexpected occlusion before building.
- For mechanisms, reject count-only success. Verify every joint anchor lies on
  both the parent and child component envelope, verify the base is grounded,
  and sample the first/middle/last scene frames. A scene that moves the entire
  structure or separates connected interfaces is a failed design.
- Never substitute `icad.agent.comparison.*` for the direct `visual.json`
  feedback schema `icad.visual.snapshot.v1`.
- When exploring alternatives, generate only two candidates at a time and make
  their body graphs or mechanism architectures meaningfully different. Call
  MCP `icad.compare` (or `icad compare-json`) to compare membership, materials,
  complete mechanism edges, spatial envelopes, scenes, topology cost, and both
  four-view snapshots. Read the shared-bounds difference grids (`A` first-only,
  `B` second-only, `=` same body, `!` changed body identity) and the intent-aware
  optimization matrix; preserve the selected candidate before generating the
  next pair.
- Use `REVOLUTE` and `PRISMATIC` joint limits and explicit constraints for
  movable assemblies.
- Use predefined material presets and embedded procedural textures.
- Use `SCENE`, `TRACK`, and ordered `KEYFRAME` declarations for animation.
- Use only stable semantic topology identifiers returned by ICAD inspection.
- Preserve the last valid source and repair focused compiler diagnostics before
  generating manufacturing artifacts.
