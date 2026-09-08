<p align="center">
  <img src="assets/branding/icad-logo.png" alt="ICAD Agentic CAD" width="300">
</p>

# ICAD

ICAD is an independent C++23 compiler and geometry engine for agentic 3D
design. A person or AI agent writes a deterministic `.icad` program; ICAD
checks it, lowers it to a canonical model, resolves spatial points, vectors,
body poses and mechanism joints, builds geometry, applies predefined materials,
compiles animation scenes, and emits CAD, mesh, scene, drawing, and
manufacturing artifacts for its native viewer and downstream tools.

ICAD does not use an external CAD kernel. Its lexer, parser, type
system, diagnostics, semantic IR, primitive geometry, native boolean CSG,
transforms, analytic and faceted topology, triangulation, STEP writer, OBJ
writer, material system, and Qt/OpenGL viewer integration are owned by this
repository. This is a deliberate product rule: the
engine is designed around auditable AI-agent workflows instead of wrapping a
desktop CAD API.

ICAD is maintained by [Valori Systems](https://valorisystems.com/) and is
available under the [MIT License](LICENSE). Visit the
[ICAD project website](https://icad.valorisystems.com/) for the product
overview, installation paths, and release links.

Ordered releases are built automatically from `main` by GitHub Actions for
Linux, Windows, Intel macOS, Apple Silicon macOS, VS Code, and Codex. Release
tags begin at `v0.0.001-alpha`; see [the release guide](docs/releasing.md).

## Install ICAD for Codex

The `icad-agentic-cad` plugin gives Codex the ICAD design workflow and uses the
native `icad` compiler and ICAD Studio viewer for compilation, visual feedback,
validation, and export. Tagged GitHub Releases publish the plugin, installers,
compiler, and viewer together. The commands below resolve the latest release
and verify every downloaded plugin or native archive against its published
SHA-256 file.

You need the [Codex CLI](https://developers.openai.com/codex/cli) on `PATH` to
register the plugin. The native toolchain does not require Codex when installed
separately.

### Install only the plugin with Codex CLI

Codex can install the ICAD marketplace directly from GitHub using the
[official marketplace workflow](https://developers.openai.com/plugins/build/plugins#add-a-marketplace-from-the-cli):

```sh
codex plugin marketplace add valorisystems/ICAD --ref main
codex plugin add icad-agentic-cad@icad
```

Use `--ref vX.Y.Z` instead of `--ref main` to pin the plugin source to a tagged
release. To refresh an installation that tracks `main`, run:

```sh
codex plugin marketplace upgrade icad
codex plugin add icad-agentic-cad@icad
```

Use this Git marketplace path when installing the plugin separately. If you use
the all-in-one release installer below, rerun that installer to update its
release snapshot instead of adding a second marketplace with the same name.

When a design task begins, the plugin checks for both `icad` and
`icad-viewer`. If either is missing and Codex has permission to use the network
and run the installer, the plugin downloads the matching checksum-verified
GitHub Release automatically. If automatic downloads are disabled, use the
dependencies-only or fully manual instructions below.

### Install the plugin and native dependencies

On macOS or Linux:

```sh
curl -fsSL https://github.com/valorisystems/ICAD/releases/latest/download/install.sh | sh
```

On Windows PowerShell:

```powershell
$installer = Join-Path $env:TEMP "icad-install.ps1"
Invoke-WebRequest https://github.com/valorisystems/ICAD/releases/latest/download/install.ps1 -OutFile $installer
& $installer
```

The default install does all of the following:

1. Downloads and verifies `icad-codex-plugin.zip`.
2. Registers the release marketplace and installs `icad-agentic-cad@icad`.
3. Selects the native package for the current operating system and CPU.
4. Downloads and verifies the ICAD compiler and native Qt/OpenGL viewer.
5. Places the toolchain in the current user's application-data directory and
   links it from `~/.local/bin` on macOS/Linux or adds it to the user `PATH` on
   Windows. The installer prints the one required `PATH` change if
   `~/.local/bin` is not already present.

Start a new Codex conversation after installation so the new skill is loaded.

### Install the compiler and viewer separately

This keeps plugin registration unchanged and installs only the native runtime:

```sh
# macOS or Linux
curl -fsSL https://github.com/valorisystems/ICAD/releases/latest/download/install.sh | \
  sh -s -- --dependencies-only
```

```powershell
# Windows PowerShell
$installer = Join-Path $env:TEMP "icad-install.ps1"
Invoke-WebRequest https://github.com/valorisystems/ICAD/releases/latest/download/install.ps1 -OutFile $installer
& $installer -DependenciesOnly
```

To pin all downloads to a specific release, add `--version v0.0.001-alpha` on
macOS or Linux, or `-Version v0.0.001-alpha` on Windows.

### Fully manual native installation

Each GitHub Release contains one compiler-and-viewer archive per supported
platform:

| Platform | Release asset |
|---|---|
| Linux x86-64 | `icad-linux-x86_64.zip` |
| Windows x86-64 | `icad-windows-x86_64.zip` |
| macOS Intel | `icad-macos-x86_64.zip` |
| macOS Apple Silicon | `icad-macos-arm64.zip` |

Download the matching ZIP and its adjacent `.sha256` file from the
[GitHub Releases page](https://github.com/valorisystems/ICAD/releases), verify the
checksum, and extract the archive. For example:

```sh
# Linux
sha256sum -c icad-linux-x86_64.zip.sha256

# macOS Apple Silicon
shasum -a 256 -c icad-macos-arm64.zip.sha256
```

On Windows, compare `(Get-FileHash -Algorithm SHA256 <archive>).Hash` with the
first value in the `.sha256` file. Add the extracted `stage/bin` directory to
`PATH`. On macOS, `stage/bin/icad` is the compiler and
`stage/icad-viewer.app` is the viewer; optionally link
`stage/icad-viewer.app/Contents/MacOS/icad-viewer` into a directory on `PATH`.

Verify either installation mode before starting a design:

```sh
icad --version
icad-viewer --help
codex plugin list
```

## Quick start

Requirements are intentionally small:

- CMake 3.25+
- Clang or another C++23 compiler
- Make (optional convenience wrapper)
- Qt 6.5+ Core, Gui, Widgets, OpenGL, OpenGLWidgets, and Concurrent modules
  when building the optional native viewer

The desktop viewer is a native Qt 6/OpenGL application. It links directly to
the thread-safe ICAD engine API and has no browser, WebView, JavaScript, npm, or
HTTP runtime. Qt is not a compiler-core dependency.

On macOS, Apple Clang works. clangd is recommended for IntelliSense.

```sh
make
make test
make benchmark
make advanced
make viewer
make viewer-package
```

Create the maintained detailed robotic-arm design and its complete artifact
package directly from one short prompt:

```sh
build/bin/icad agent-create \
  "Create a detailed articulated industrial robotic arm with a gripper and animated joints" \
  --source-out build/agent/robotic_arm.icad \
  --output-dir build/agent/robotic_arm
```

This one command classifies the design intent, selects an embedded
compiler-validated parametric template, runs the composite engineering review,
writes editable `.icad` source, and exports every format. Its result also gives
the calling model explicit assumptions, named parameter/angle handles, overall
and per-body bounds, named spatial references, the parent-child joint graph,
contacts, and animation tracks—enough to form a compact spatial picture without
reverse-engineering mesh triangles. Recognized robotic-arm and bridge prompts
target one model iteration; unfamiliar prompts receive a valid generic
parametric starter for agent refinement.

Open the source directly in ICAD Studio:

```sh
open build/bin/icad-viewer.app --args --view isometric examples/advanced.icad # macOS

# Open several sources as independent tabs in one native workspace:
open build/bin/icad-viewer.app --args examples/advanced.icad examples/assembly_semantics.icad

# Open a workspace folder; main.icad is selected automatically and the full
# folder remains available in the file tree:
open build/bin/icad-viewer.app --args ../test-icad

# Headless automation using the exact native Qt/OpenGL viewport:
build/bin/icad-viewer.app/Contents/MacOS/icad-viewer \
  --view isometric --snapshot build/advanced.png examples/advanced.icad

# Capture the complete native IDE, including project tree, tabs, and panels:
build/bin/icad-viewer.app/Contents/MacOS/icad-viewer \
  --studio-snapshot build/icad-studio.png examples/advanced.icad examples/robotic_arm.icad
```

The native viewer live-compiles edits through the in-process engine, keeps the
last valid mesh while diagnostics are fixed, uploads indexed meshes to OpenGL,
and supports orbit, pan, zoom, orthographic standard views, component picking,
frame-all/frame-selected camera centering, scene-defined backgrounds and lights,
four viewport shading modes, screenshots, and manufacturing-package export.
`Solid with mesh edges` and `Triangle mesh` expose every render triangle like a
Blender mesh overlay, while `CAD wireframe` keeps only physical boundaries and
creases. The render mesh uses crease-aware, angle-weighted normals, an explicit
24-bit depth buffer, opaque depth writes, and depth-safe overlays, preventing
radial triangle bands on smooth revolved and filleted solids. Use
`--display solid|solid-mesh|cad-wire|mesh-wire` for reproducible automation.
`--snapshot output.png` waits for compilation, captures that same native
viewport, and exits with a nonzero status if compilation or image writing fails.
`--studio-snapshot output.png` captures the complete rendered workbench after
its source tabs finish compiling, making release screenshots reproducible.
Passing a workspace directory opens its `main.icad` entry point (or the first
`.icad` source when no `main.icad` exists) and roots the file tree at that
directory. Per-document source, diagnostics, model tree, scene, and render cache
follow stable tab identities even after tabs are reordered.

The advanced build produces:

```text
build/examples/advanced.step        analytic/faceted solid CAD exchange
build/examples/advanced.assembly.step hierarchical component exchange
build/examples/advanced.obj         named triangle mesh
build/examples/advanced.stl         portable triangle mesh
build/examples/advanced.gltf        embedded-buffer glTF 2.0 scene mesh
build/examples/advanced.glb         binary glTF 2.0 scene mesh
build/examples/advanced.3mf         OPC-packaged 3MF manufacturing mesh
build/examples/advanced.scene.json  native render mesh, materials, textures, animation
build/examples/advanced.bom.json    body/component bill of materials
build/examples/advanced.manufacturing.json manufacturing validation
build/examples/advanced.drawing.svg projected native-edge drawing sheet
build/examples/advanced.drawing.dxf real R2013 2D drawing exchange
build/examples/advanced.topology.json stable analytic B-Rep entity map
```

## Compiler commands

```sh
build/bin/icad check examples/advanced.icad
build/bin/icad tokens examples/minimal.icad
build/bin/icad ast examples/minimal.icad
build/bin/icad inspect examples/advanced.icad
build/bin/icad inspect-json examples/advanced.icad
build/bin/icad visual-json examples/robotic_arm.icad
build/bin/icad compare-json first.icad second.icad
build/bin/icad topology-json examples/advanced.icad
build/bin/icad diagnostics-json examples/advanced.icad
build/bin/icad measure examples/advanced.icad
build/bin/icad interference-json examples/assembly_semantics.icad
build/bin/icad validate examples/advanced.icad
build/bin/icad manufacturing examples/advanced.icad
build/bin/icad materials
build/bin/icad materials-json --class metal
build/bin/icad materials-json --class nickel_superalloy
build/bin/icad material-json ATI_TI_6AL_4V_GRADE5_BAR
build/bin/icad bom-json examples/bench/turbojet_engine.icad > turbojet_engine.bom.json
build/bin/icad build examples/advanced.icad --output-dir build/examples
build/bin/icad inspect-step build/examples/advanced.step
build/bin/icad inspect-stl build/examples/advanced.stl
build/bin/icad inspect-gltf build/examples/advanced.glb
build/bin/icad inspect-3mf build/examples/advanced.3mf
build/bin/icad inspect-dxf build/examples/advanced.drawing.dxf
build/bin/icad agent-bootstrap "Create an articulated robot arm"
build/bin/icad agent-review examples/robotic_arm.icad
build/bin/icad lsp
build/bin/icad mcp --workspace "$PWD"
```

Commands return non-zero on failure. Diagnostics have stable codes and exact
line/column positions, making them suitable for an agent repair loop.

## Connect any LLM through MCP

`icad mcp` is a native, dependency-free MCP stdio server. It supports the
current stateless `2026-07-28` discovery protocol and the legacy initialize
handshake used by older hosts. Configure any MCP-capable LLM host with:

```json
{
  "mcpServers": {
    "icad": {
      "command": "/absolute/path/to/bin/icad",
      "args": ["mcp", "--workspace", "/absolute/path/to/design-workspace"]
    }
  }
}
```

The deterministic tool catalog contains source-text tools:

- `icad.agent.create`: prompt-to-source, composite review, revisioned commit,
  and complete artifact build in one tool call;
- `icad.agent.bootstrap`: prompt classification, complete maintained source,
  acceptance criteria, and parameter strategy;
- `icad.agent.review`: compilation, constraints, manufacturing, topology,
  metrics, interference, and a compact agent-readable design map in one response;
- `icad.language`: concise source-language and workflow guide;
- `icad.materials`: PBR presets plus the versioned, supplier-sourced physical
  material catalog, optionally filtered by class;
- `icad.material.inspect`: one complete material profile by stable ID or alias;
- `icad.compile`: compiler diagnostics from complete source text;
- `icad.validate`: constraints and manufacturing validation;
- `icad.measure`: area, volume, and bounds;
- `icad.inspect`: canonical IR counts, revision, ownership, and metrics;
- `icad.visualize`: three ordered 512x512 lossless PNG `image_url` inputs
  (front, right, top), also returned as native MCP image blocks for direct model
  vision, plus deterministic depth rasters, a component legend, per-body
  bounds/triangle counts, and current joint state;
- `icad.compare`: two complete source documents compared by body and mechanism
  graph, per-body spatial envelopes/roles/materials, scene programs, topology
  cost, and two embedded four-view snapshots. Shared-world-bounds difference
  grids mark first-only (`A`), second-only (`B`), common-body (`=`), and changed
  body identity (`!`) cells. Its intent-aware optimization matrix prevents an
  agent from selecting a cheaper design that performs the wrong function;
- `icad.topology`: stable solid/shell/face/edge/vertex IDs and analytic geometry;
- `icad.distance`: closest points and exact-polyhedral body distance;
- `icad.interference`: penetrating, contained, and surface-contact classification;
- `icad.section`: plane-section segments with body/part provenance;
- `icad.build`: complete staged artifact package.

Durable project tools allow several agents to collaborate without lost edits:

- `icad.project.read`: source plus exact hexadecimal revision;
- `icad.project.write`: validated atomic commit with `expectedRevision`;
- `icad.project.set_parameter`: narrow typed parameter transaction;
- `icad.project.set_parameters`: multiple named dimensions in one validated
  atomic transaction;
- `icad.project.history`: immutable revision inventory;
- `icad.project.restore`: validated historical restore;
- `icad.project.build`: build only the exact expected project revision.

Read-only tools accept source text and return structured JSON plus a text copy
for broad host compatibility. `icad.build` accepts a workspace-relative output
directory, rejects traversal and symlink escapes, and reports every committed
artifact. See [Agent integration](docs/agent-integration.md) for protocol
examples and the provider-independent repair loop.

Use `expectedRevision: "absent"` only when creating a new `.icad` project.
Every later mutation requires the 16-character revision returned by the last
read or commit. A stale agent receives `ICAD-PROJECT-CONFLICT`; invalid source
never replaces the current file. Commits use a cross-process lock, exclusive
temporary file, atomic rename, and immutable snapshots under `.icad-history/`.

## Language basics

ICAD is deliberately line-oriented and token-efficient:

```icad
PROJECT agent_part
UNITS mm

MATERIAL shell CARBON_FIBER

BODY enclosure
MATERIAL shell
FEATURE case
TYPE BOX
WIDTH 100 mm
DEPTH 60 mm
HEIGHT 20 mm
END
END
```

The implemented geometry types are:

- `BOX`: `WIDTH`, `DEPTH`, `HEIGHT`
- `CYLINDER`: `RADIUS`, `HEIGHT`
- `CONE`: `RADIUS1`, `RADIUS2`, `HEIGHT`
- `SPHERE`: `RADIUS`

Large agent-authored designs can be split into deterministic source modules:

```icad
PROJECT factory_cell
UNITS mm
IMPORT "parts/base.icad"
INJECT "generated/gripper.icad"
```

`IMPORT` and the explicit `INJECT` alias insert declarative `.icad` fragments
at that source position. Modules normally omit `PROJECT` and `UNITS`. Resolution
is relative to the importing file, remains inside the entry file's project
directory, accepts only `.icad`, rejects cycles, and enforces depth and total
source-size limits. Imported text is parsed by the same compiler—it never
executes native code, shell commands, or plugins. CLI checks/builds, the LSP,
and live viewer all use this resolver; the viewer also invalidates its cache
when a dependency changes on disk.

Every type accepts `ORIGIN_X`, `ORIGIN_Y`, `ORIGIN_Z`, `ROTATION_X`,
`ROTATION_Y`, and `ROTATION_Z`. Length units are
`mm`, `cm`, `m`, `in`, and `ft`; angles are `deg` and `rad`; scene time units
are `ms`, `s`, and `min`.

Named parameters can replace compatible feature quantities.

## Sketch-first feature history

Normal ICAD parts are authored in the same order as a CAD designer builds
them: sketch on a datum plane, create the first solid, select a resulting face,
sketch again, then add or remove material.

The production `MULTI_SHAPE_SKETCH_V1` capability treats a sketch as a
constrained 2D workspace containing named SVG-path-like shapes, not as one
implicit polygon. A shape declares `OPEN|CLOSED`, an explicit
`STOCK|ADDITIVE|HOLE|CONSTRUCTION` role, and named `POINT`, `LINE`, `ARC`, or
`CIRCLE` entities. Each closed path lowers to a stable
`body::sketch.shape` profile consumed explicitly by `PAD` or `POCKET`.
`SOLVE FULL` rejects under-constrained workspaces, while `SOLVE ALLOW_UNDER`
keeps exploratory sketches possible. The compiler rejects broken chains,
self-intersections, touching/intersecting region boundaries, and holes not
contained by exactly one stock/additive region.

The `SKETCH_REGION_ARRANGEMENT_V1` capability makes material arrangement
explicit instead of inferring feature intent from winding:

```icad
REGION perforated_plate
OUTER outer
HOLES bore_left bore_right
END
PAD plate FROM layout.perforated_plate DEPTH plate_thickness NEW
```

`ADVANCED_SKETCH_CONSTRAINTS_V1` adds horizontal/vertical distance,
parallel/perpendicular, equal length/radius, concentric, midpoint, and symmetry
equations over qualified point and entity names.
`SKETCH_LINE_ARC_TANGENCY_V1` adds explicit endpoint tangency as
`CONSTRAINT name TANGENT line arc AT shared_point`; the entity order may be
reversed, but the point must be an endpoint common to the line and a
non-full-circle arc. See
[`examples/rounded_tangent_plate.icad`](examples/rounded_tangent_plate.icad)
for the fully constrained capsule profile and
[`examples/region_plate.icad`](examples/region_plate.icad) for the native
one-feature, two-hole region acceptance model.

For solid-edge treatment, `SEMANTIC_EDGE_LOOP_SELECTION_V1` provides stable
circular rim intent on the current annular result:

```icad
FEATURE soften_inner_rim
TYPE FILLET
SELECT EDGE TOP INNER
RADIUS 3 mm
END
```

`TOP|BOTTOM` selects the axial side and `INNER|OUTER` classifies concave versus
convex loops. The native engine executes both fillet classifications and the
same selectors for chamfers. `visual.json` reports the selected loop and legal
operations. See
[`examples/selective_round_vessel.icad`](examples/selective_round_vessel.icad).
That acceptance source places its independently filleted inner-rim and
outer-rim comparison bodies side by side. Coincident comparison bodies are
forbidden because they create z-fighting and a false impression of internal
plates.

`TOPOLOGY_QUERY_V1` makes the same proven native topology addressable by a
named, inspectable query:

```icad
SELECTION upper_inner_rim
FROM wall_solid
EDGES WHERE
LOOP
CIRCULAR
CONCAVE
ADJACENT_TO FACE top
END

FEATURE soften_rim
TYPE FILLET
SELECT EDGESET upper_inner_rim
RADIUS 3 mm
END
```

This avoids hidden triangle/edge indices. The query becomes a typed IR and
dependency node; `visual.json` returns its stable topology ID, match reason,
legal operations, and explicit reasons that face/body operations do not apply.
The current bound is honest: the source is an annular REGION extrusion and the
modifier immediately follows it. Arbitrary edge chains, remapping through
later features, shell, offset-face/edge, draft, split, and projection remain
capability-gated until their native topology operations are validated. See
[`examples/topology_query_vessel.icad`](examples/topology_query_vessel.icad).

Shapes feed ordered solid features; bodies form manufacturable solids;
components add interfaces and engineering identity; assemblies add
occurrences, mates, and motion. The formal dependency model, future selectors,
material vs.
appearance separation, and agent/viewer data contract are documented in
[Grammar v2 design](docs/grammar-v2-design.md). The detailed compiler contract
and proposed syntax are in the [Grammar v2 RFC](docs/grammar-v2-rfc.md) and
[proposed EBNF](grammar/icad-v2-proposal.ebnf). The matching
[compiler and native-engine architecture](docs/compiler-v2-architecture.md)
defines the conceptual lexer, semantic passes, exact topology, incremental
execution, and thread-safety model. These are design contracts; agents must not
emit proposed constructs until `icad.language` advertises the matching compiler
capability.

The lexer retains comments and exact byte spans, recognizes reserved v2
punctuation/string syntax, and reports recoverable string/exponent diagnostics
while preserving compatible signed numeric literals. Advertised parser and
semantic slices now include typed scalar expressions and multi-shape sketches;
unadvertised v2 declarations remain proposals.

Source files can now pin their implemented contract with `REQUIRES ICAD 1.0`
and `REQUIRES CAPABILITY NAME` before `PROJECT`. Run `build/bin/icad language`
or call MCP `icad.language` for the authoritative capability list. Unsupported
future requirements fail during preflight before their dependent syntax is
parsed.

The first typed-expression layer is production-ready for parameters, angles,
and feature properties. It supports precedence, unary signs, parentheses,
forward dependencies, project-qualified scalar names, unit checking, cycle
detection, and division-by-zero diagnostics:

```icad
REQUIRES ICAD 1.0
REQUIRES CAPABILITY PARAMETER_EXPRESSIONS_V1
REQUIRES CAPABILITY QUALIFIED_VALUE_REFERENCES_V1
PROJECT bracket
UNITS mm
PARAMETER wall 8 mm
PARAMETER width 120 mm
PARAMETER inner bracket.width - 2 * wall
```

```icad
BODY mounting_bracket
SKETCH base ON PLANE XY
POINT p0 0 mm 0 mm FIXED
POINT p1 100 mm 0 mm FIXED
POINT p2 100 mm 60 mm FIXED
POINT p3 0 mm 60 mm FIXED
END
PAD base_solid FROM base DEPTH 12 mm NEW

FACE top_of_base FROM base_solid.face.top
SKETCH boss ON FACE top_of_base
POINT p0 25 mm 15 mm FIXED
POINT p1 75 mm 15 mm FIXED
POINT p2 75 mm 45 mm FIXED
POINT p3 25 mm 45 mm FIXED
END
PAD raised_boss FROM boss DEPTH 20 mm ADD

SKETCH bore ON FACE raised_boss Z_MAX
CIRCLE 50 mm 30 mm 8 mm
END
POCKET mounting_bore FROM bore DEPTH 20 mm
END
```

`XY`, `XZ`, and `YZ` are supported datum planes. With
`PERSISTENT_FACE_REFERENCES_V1`, `FACE name FROM feature.face.top|bottom`
creates a named, provenance-preserving cap-face reference; sketches may attach
through that name or directly through `ON FACE feature.face.top|bottom`.
`visual-json` reports its canonical `body/feature/face.role` topology ID and the
dependency graph retains the alias edge. The legacy explicit principal selectors
`X_MIN`, `X_MAX`, `Y_MIN`, `Y_MAX`, `Z_MIN`, and `Z_MAX` remain compatible.
References and aliases must select an earlier feature in the same body, and a
singular reference is never repaired by proximity. `PAD ... NEW` must create a body's first
solid; later pads use `ADD`, while `POCKET` cuts inward from the selected face.
Every body-local sketch requires `ON PLANE` or `ON FACE` and must be consumed by
a later operation. Sketch names are local to their body, so two bodies may both
use `base`; canonical IR and agent output identify them as `body::base`. The
compiler rejects forward references, duplicate history names within a body,
mixed circle and point sketches, unused body sketches, invalid depth
dimensions, and invalid boolean results.
`visual-json` exposes the same ordered history under `featureHistory`, allowing
an agent to reason about how a part was made instead of reverse engineering its
triangles. See [`examples/sketch_history.icad`](examples/sketch_history.icad)
and [`examples/persistent_face_plate.icad`](examples/persistent_face_plate.icad).

Dependency graphs retain stable string IDs for diagnostics and agent tools, and
also expose compact integer dependency-to-consumer edges for high-frequency
layout or visualization without repeating ID lookups. Faceted topology uses a
reserved hash edge index and pre-sized entity storage, keeping construction and
validation linear on large triangle sets without an additional graph library.

### Low-level and advanced features

The explicit `FEATURE` block remains available for primitives, revolve, sweep,
loft, freeform, fillet, chamfer, pattern, mirror, and precise legacy models.
Prefer the sketch-first history above for new prismatic parts. Custom profiles
support native triangulated extrusion and full revolution:

```icad
PARAMETER thickness 8 mm
PROFILE bracket_section
POINT 0 mm 0 mm
POINT 40 mm 0 mm
POINT 40 mm 20 mm
POINT 0 mm 20 mm
END

BODY bracket
FEATURE plate
TYPE EXTRUDE
PROFILE bracket_section
HEIGHT thickness
END
END
```

Profiles are checked for closure semantics, area, winding, and
self-intersection. Rounded exact profiles use an explicit start, tangent lines,
center-defined circular arcs, and closure:

```icad
PROFILE rounded_plate
START 10 mm 0 mm
LINE 40 mm 0 mm
ARC 50 mm 10 mm CENTER 40 mm 10 mm CCW
LINE 50 mm 20 mm
ARC 40 mm 30 mm CENTER 40 mm 20 mm CCW
LINE 10 mm 30 mm
ARC 0 mm 20 mm CENTER 10 mm 20 mm CCW
LINE 0 mm 10 mm
ARC 10 mm 0 mm CENTER 10 mm 10 mm CCW
CLOSE
END
```

A full circular profile is `CIRCLE center_x center_y radius`, with units on all
three quantities. `EXTRUDE` preserves line and circular-arc edges as analytic
topology while tessellating them deterministically for STL, OBJ, and the web
viewer. `REVOLVE` supports full `360 deg` line, arc, and circle profiles. Curved
revolutions (including torus geometry) use validated faceted topology and
tessellated measurements; line-only revolutions retain analytic topology.
Surface area and volume are evaluated analytically for every current primitive,
curved extrusion, and line-profile revolution instead of being estimated from
STL triangles. Curved-revolution measurements and all world bounds come from
the deterministic delivery mesh.

The owned engine also provides a deterministic sweep-and-prune AABB index plus
segment/plane, ray/triangle, triangle/triangle, and point-in-solid tests.
Triangle results distinguish point, segment, and coplanar overlap.
`interference-json` and `inspect-json` separate penetrating or contained solid
pairs from surface-only contact. Results are honestly labeled polyhedral solid
classification rather than analytic intersection volume.

Features inside a body are evaluated in source order. The first feature is
`NEW` by default; later operands can request native CSG explicitly:

```icad
BODY bracket
FEATURE stock
TYPE BOX
WIDTH 40 mm
DEPTH 30 mm
HEIGHT 10 mm
END
FEATURE opening
TYPE CYLINDER
OPERATION CUT
RADIUS 4 mm
HEIGHT 12 mm
ORIGIN_X 20 mm
ORIGIN_Y 15 mm
ORIGIN_Z -1 mm
END
END
```

`OPERATION UNION`, `CUT`, and `INTERSECT` use ICAD's owned BSP classifier,
vertex welding, conforming boundary splitting, degenerate/duplicate removal,
and connected-solid separation. The result is a validated faceted B-Rep;
primitive operands retain analytic topology only until a boolean consumes
them. `inspect-json` reports operation counts and every applied shape-repair
action. Empty intersections fail with an explicit geometry diagnostic. See
[`examples/boolean_showcase.icad`](examples/boolean_showcase.icad) for the
acceptance model.

Native follow-on modifiers use semantic references instead of triangle IDs:

```icad
POINT3 target_edge 20 mm 10 mm 5 mm
VECTOR row_axis 1 0 0

FEATURE soften
TYPE FILLET
SELECT EDGE NEAREST target_edge
RADIUS 2 mm
END

FEATURE repeat
TYPE LINEAR_PATTERN
DIRECTION row_axis
COUNT 4
SPACING 25 mm
END
```

`CHAMFER` uses `DISTANCE` with the same selector. The deterministic selector
searches all 12 sharp edges of a translated axis-aligned box and resolves ties
by a stable principal-axis order. `FILLET` uses an eight-segment native arc
approximation in the selected edge frame. `LINEAR_PATTERN` accepts 2–1000
instances along a named normalized vector. `MIRROR` uses `PLANE point NORMAL
vector` and retains the source plus its reflected copy. Pattern and mirror
components remain independently named solids; use `UNION` explicitly when one
merged solid is required. Inspection returns each modifier and its selection
provenance. The executable reference is
[`examples/modeling_tools.icad`](examples/modeling_tools.icad).

Named profiles also drive native advanced faceted surfaces:

```icad
FEATURE rail
TYPE SWEEP
PROFILE square
PATH rail_start rail_rise rail_end
END

FEATURE transition
TYPE LOFT
PROFILE square
TARGET_PROFILE inset
HEIGHT 20 mm
END

FEATURE twisted_transition
TYPE FREEFORM
PROFILE square
TARGET_PROFILE diamond
HEIGHT 30 mm
TWIST 90 deg
COUNT 9
END
```

`SWEEP` translates a profile through two or more named 3D path points with a
fixed XY orientation. `LOFT` deterministically resamples and connects two
closed profiles. `FREEFORM` uses the same profile morph with 3–128 sections
and cumulative twist. These outputs and curved full revolutions are validated
faceted B-Reps with tessellated measurements; they are not labeled NURBS or
analytic toroidal surfaces. See
[`examples/advanced_surfaces.icad`](examples/advanced_surfaces.icad).

## Solved dimensional sketches

```icad
SKETCH mounting_outline
POINT origin 0 mm 0 mm FIXED
POINT lower_right 38 mm 2 mm
POINT upper_right 39 mm 28 mm
CONSTRAINT bottom HORIZONTAL origin lower_right
CONSTRAINT right VERTICAL lower_right upper_right
CONSTRAINT width DISTANCE origin lower_right plate_width
CONSTRAINT height DISTANCE lower_right upper_right plate_height
CONSTRAINT corner ANGLE origin lower_right upper_right 90 deg
END
```

The dependency-free solver accepts `HORIZONTAL`, `VERTICAL`, `COINCIDENT`,
`DISTANCE`, and unsigned 0–180° `ANGLE` constraints. With the advertised
advanced capability it also accepts `H_DISTANCE`, `V_DISTANCE`, `PARALLEL`,
`PERPENDICULAR`, `EQUAL_LENGTH`, `CONCENTRIC`, `EQUAL_RADIUS`, `MIDPOINT`, and
`SYMMETRIC ... ABOUT ...`. Initial point positions
are deterministic solution seeds; `FIXED` coordinates never move. Inspection
reports initial and solved coordinates, maximum residual, iteration count, and
remaining degrees of freedom. A consistent sketch with at least three points
also becomes a closed named profile, so `PROFILE mounting_outline` can directly
drive `EXTRUDE`, `REVOLVE`, `SWEEP`, `LOFT`, or `FREEFORM`. Inconsistent systems
fail compilation with `ICAD-S0038`. See
[`examples/constrained_sketch.icad`](examples/constrained_sketch.icad) and the
complete qualified-entity example
[`examples/advanced_sketch_constraints.icad`](examples/advanced_sketch_constraints.icad).

## Dependency graph and incremental compilation

Every canonical project exposes a stable dependency DAG covering parameters,
angles, spatial expressions, sketches, profiles, ordered features, bodies,
constraints, poses, materials, and scenes. `inspect-json` returns each node,
its direct `dependsOn` edges, and deterministic evaluation order. Embedders can
use `icad::compiler::IncrementalCompiler` for a stateful compile session: it
fingerprints each body from its lowered geometry dependencies, reuses unchanged
validated topology, recomputes dirty bodies, and reports reused, recomputed,
and removed body names. Dirty bodies run through a bounded worker pool and are
merged in deterministic source order. A mutex protects each complete cache
revision, so editor, LSP, and agent callers may safely share a compiler.
Project-name changes and `clear()` invalidate the cache.

## Tolerances, distance, and sections

```icad
TOLERANCE LINEAR 0.001 mm ANGULAR 0.01 deg
```

The canonical tolerance policy is unit-checked and included in revision
fingerprints and agent inspection. It drives contact classification and native
query tolerances. `icad distance-json model.icad body_a body_b` returns closest
points and exact Euclidean distance on the same validated polyhedral boundary
used by STEP/STL/OBJ and the viewer. `icad section-json model.icad px py pz nx
ny nz [body]` intersects that boundary with a normalized plane and returns
named segments. Curved source may use a tessellated delivery boundary, so both
JSON contracts state their representation instead of implying analytic-curve
accuracy. See [`examples/geometric_queries.icad`](examples/geometric_queries.icad).

## Agent-readable assemblies and mechanisms

ICAD source can name the spatial quantities that an LLM needs to understand a
mechanism before it looks at triangles:

```icad
PARAMETER shoulder_height 122 mm
ANGLE elbow_angle 55 deg

POINT3 shoulder 0 mm 0 mm shoulder_height
POINT3 elbow 98 mm 0 mm 184 mm
VECTOR hinge_axis 0 1 0
VECTOR tool_axis 1 0 0
VECTOR forearm_axis ROTATE tool_axis AROUND hinge_axis BY elbow_angle
POINT3 tool_tip FROM elbow ALONG tool_axis DISTANCE 140 mm
POINT3 posed_tool_tip FROM elbow ALONG forearm_axis DISTANCE 140 mm
VECTOR reach_axis FROM shoulder TO tool_tip

POSE arm_01 AT shoulder ROTATION 0 deg 0 deg 0 deg

JOINT shoulder_hinge REVOLUTE base arm_01 AT shoulder AXIS hinge_axis VALUE 0 deg LIMIT -90 deg 90 deg
JOINT elbow_hinge REVOLUTE arm_01 arm_02 AT elbow AXIS hinge_axis VALUE elbow_angle LIMIT -135 deg 135 deg
```

Reusable component occurrences use `INSTANCE occurrence OF body AT point
ROTATION X Y Z`. The body remains the shared geometry definition and its base
occurrence; each instance adds a named transformed occurrence without copying
feature source. Joints may connect bodies or instances. For instances, native
fixed/revolute/prismatic parent chains apply driven values around named points
and arbitrary normalized axes to meshes, topology, bounds, STEP/STL/OBJ, and
the viewer. Inspection reports definitions, seed transforms, joint-driven
status, and solved bounds. See
[`examples/assembly_instances.icad`](examples/assembly_instances.icad).

Face and edge mates use world-semantic selectors rather than mesh indices:

```icad
MATE seated FACE base Z_MAX cover Z_MIN OFFSET 0 mm
MATE aligned EDGE base X_AT_Y_MIN_Z_MAX cover X_AT_Y_MIN_Z_MIN TOLERANCE 0.001 mm
```

Face selectors are `X_MIN`, `X_MAX`, `Y_MIN`, `Y_MAX`, `Z_MIN`, and `Z_MAX`.
Edge selectors name the edge direction and fixed sides, for example
`X_AT_Y_MIN_Z_MAX`. Mates validate solved delivery geometry with the project
linear tolerance and block artifact generation when violated. They do not
silently move an occurrence; named points, instance transforms, and joints
remain the explicit assembly pose definition. See
[`examples/assembly_semantics.icad`](examples/assembly_semantics.icad).
Instance occurrences inherit the definition material in manufacturing checks
and appear as separate BOM items with an explicit definition reference.

Manufacturing interfaces describe how components are physically joined, not
just where they appear. Each side names a point, axis, occurrence, interface
type, and optional nominal size; `CONNECT` then records the process, standard,
fastener or fit, allowable clearance, and deterministic magnetic-seat check:

```icad
INTERFACE motor_shaft BODY motor AT shoulder AXIS shaft_axis TYPE SHAFT SIZE 12 mm
INTERFACE arm_bore BODY arm AT shoulder AXIS bore_axis TYPE BORE SIZE 12 mm
CONNECT shoulder_fit motor_shaft arm_bore METHOD SLIP_FIT STANDARD ISO_286 FIT H7_g6 CLEARANCE 0.02 mm AUTO
```

The compiler rejects incompatible pairs and incomplete process metadata.
Planar `MOUNT`, `FLANGE`, `WELD_SEAM`, and `BOND_FACE` interfaces must lie on
the referenced occurrence boundary and their axes must match the outward face
normal. Contact-only bolted, screwed, welded, brazed, and bonded connections
also reject solid-volume penetration. `visual-json` exposes these checks as
`attachmentValid`, `engineeringValid`, and an `INVALID_GEOMETRY` snap state in
addition to gap and axis alignment. `AUTO` evaluates a candidate but never
hides a source transform: an agent must repair the named datum or pose, then
run manufacturing and interference validation. Interference feedback annotates declared
engagements with their connection, method, and standard and reports anonymous
collisions separately as `unintendedPenetratingPartPairs`; release requires
that count to be zero. The example derives the instance origin and shared
mounting plane from `block_width`, `block_depth`, and `block_height`, so a size
edit propagates to its assembly datums instead of leaving stale coordinates. See
[`examples/manufacturing_connections.icad`](examples/manufacturing_connections.icad).

`POINT3` coordinates and `ANGLE`, `POSE`, joint, or constraint values may
reference a named value with the correct physical dimension. Vectors are
dimensionless and normalized during semantic lowering. Derived points use
`FROM point ALONG vector DISTANCE value`; derived vectors use `FROM point TO
point` or `ROTATE source AROUND axis BY angle`. Axis-angle rotation uses the
right-hand rule. The compiler evaluates mixed point/vector dependencies as a
graph, rejects unknown or cyclic expressions, and preserves each expression in
canonical IR instead of retaining only its numeric result. These forms make
link endpoint and forward-kinematic dependencies explicit without repeating
calculated coordinates. A body may have one explicit world-space
`POSE`; it is applied consistently to meshes, measurements, and exact topology.

Joint types are `FIXED`, `REVOLUTE`, and `PRISMATIC`. Moving joints require a
current `VALUE` and ordered `LIMIT` values. `WORLD` is a valid parent for a
ground joint. The compiler rejects multiple parents, cyclic joint graphs,
unknown bodies/points/axes, dimensional mismatches, and out-of-limit values.
`inspect-json` exposes every resolved value together with its derivation,
per-body bounds/center/size, named cross-body surface and volume contacts, poses, joint
limits, and the mechanism's degree-of-freedom count. An agent can therefore
reason from a spatial expression and adjacency graph instead of inferring
intent from anonymous mesh triangles.

Available constraints are:

- `MIN_DISTANCE body body distance` for world-space body clearance;
- `COINCIDENT point point tolerance` for named anchors;
- `PARALLEL vector vector` and `PERPENDICULAR vector vector` for axes;
- `ANGLE_BETWEEN vector vector angle` for a target direction angle.

A failed constraint or mate prevents artifact generation. Joint values solve
instance occurrence chains; direct definition-body joints remain compatible
metadata, while `POSE` defines a body definition's world arrangement.

## Materials and embedded textures

Declare a named material from a built-in preset, then assign it to a body:

```icad
MATERIAL piers CONCRETE

BODY substructure
MATERIAL piers
# features...
END
```

Built-in presets currently include:

```text
CONCRETE  STRUCTURAL_STEEL  ALUMINUM  ASPHALT  GLASS  WOOD
BRICK     GRANITE           MARBLE    COPPER    BRASS  TITANIUM
CHROME    RUSTED_STEEL      RUBBER    PLASTIC   CARBON_FIBER
CERAMIC   PLASTER           FABRIC    LEATHER   EARTH  GRASS
WATER     ICE               EMISSIVE_WHITE
```

Each preset resolves during semantic compilation to base color, alpha,
metallic, roughness, a stable procedural-texture type, and seed. ICAD generates
a deterministic 32×32 BMP texture and embeds it as a base64 data URI in the
scene. There are no loose texture assets and no image-library dependency.

Typed overrides use a block while keeping a preset as the reproducible base:

```icad
MATERIAL custom_panel
PRESET ALUMINUM
BASE_COLOR 0.12 0.20 0.80 1.0
METALLIC 0.85
ROUGHNESS 0.35
TEXTURE_SCALE 25 mm
UV_MODE BOX
END
```

Color and PBR channels are range checked, texture scale has a physical length,
and UV mode is restricted to `BOX`, `PLANAR`, `CYLINDRICAL`, or `SPHERICAL`.

## Programmable animation scenes

Scenes contain a duration, frame rate, background, and any number of BODY,
CAMERA, or JOINT tracks. Body/camera keyframes store time, position, and XYZ
rotation:

```icad
POINT3 lamp_position 200 mm 150 mm 300 mm
SCENE product_reveal
DURATION 8 s
FPS 30
BACKGROUND STUDIO
LOOP 3
LIGHT key POINT COLOR 1 0.92 0.8 INTENSITY 4 AT lamp_position
EVENT 4 s halfway
TRACK orbit CAMERA main_camera
EASING EASE_IN_OUT
KEYFRAME 0 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg
KEYFRAME 8 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 360 deg
END
TRACK lift BODY enclosure
KEYFRAME 0 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg
KEYFRAME 4 s POSITION 0 mm 0 mm 50 mm ROTATION 0 deg 0 deg 0 deg
KEYFRAME 8 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg
END
END
```

`VISIBILITY` tracks use `KEYFRAME time VISIBLE ON|OFF`. Easing supports
`LINEAR`, `EASE_IN`, `EASE_OUT`, `EASE_IN_OUT`, and `STEP`; loop counts,
lights, and events are compiler validated and retained in canonical scene IR.

Joint tracks use scalar values in the joint's physical dimension:

```icad
TRACK elbow_motion JOINT elbow_hinge
KEYFRAME 0 s VALUE -45 deg
KEYFRAME 1 s VALUE 70 deg
KEYFRAME 2 s VALUE -45 deg
END
```

The compiler rejects fixed-joint animation and values outside the declared
joint limits. The native scene evaluator interpolates each value, applies its delta
around the exported solved pivot and axis, and propagates motion through child
occurrences.

The compiler rejects unknown targets, unknown backgrounds, tracks with fewer
than two frames, non-increasing times, and frames outside scene duration.
Canonical values use millimetres, degrees, and seconds.

## Native Qt viewer

Build and run ICAD Studio with:

```sh
make viewer
# Linux/Windows: build/bin/icad-viewer examples/advanced.icad
# macOS:
open build/bin/icad-viewer.app --args --view front examples/advanced.icad
```

The split IDE keeps `.icad` as the authoritative document. Its graphite native
Qt shell provides an ICAD-only workspace tree, **File > Open Folder**, and
multiple closeable source tabs. Every tab owns an independent engine session,
saved state, and 100-operation back/next history; unsaved tabs are marked and
confirmed individually. Native File, Edit, View, and Scene menus also provide
recent files, save/export, standard views, and a debug overlay. The editor
includes language-specific highlighting and clickable diagnostics; the OpenGL
viewport retains the last valid scene while a coalescing worker incrementally
rebuilds only dirty bodies. Each background result carries the stable document
ID and exact source snapshot that produced it, so a late result from another tab
cannot replace the active tab's geometry or diagnostics. Before replacing that scene, live preview checks real mate
selectors, manufacturing-interface attachment, outward normals, and prohibited
connection penetration against the already-built cached mesh. Invalid size
edits therefore produce actionable syntax, topology, assembly, or manufacturing
diagnostics without uploading intersecting geometry or rebuilding the model
again for validation. The stationary two-sided viewport avoids disappearing faces
from mixed mesh winding and adds a clickable orientation cube. Model, scene,
property, projection, wireframe, component-selection, and screenshot tools are
native Qt widgets.
The orientation cube sits on a compact rounded-hexagon HUD backing so it stays
visually distinct from both the model and ordinary toolbar controls.

`--view` accepts `isometric`, `front`, `back`, `left`, `right`, `top`, or
`bottom`. The VS Code `icad.viewer.initialView` setting exposes the same list as
an autocomplete/dropdown and passes the selected side to ICAD Studio.

Manufacturing export atomically emits the 13-file
STEP/assembly STEP/STL/OBJ/glTF/GLB/3MF/scene/drawing/manufacturing package from
the current editor text. Release packages deploy only the Qt modules and
platform plugins used by ICAD Studio, and the dedicated viewer workflow creates
maximum-compression `.tar.xz` or `.7z` archives with SHA-256 checksums.
The macOS `.app` targets macOS 14+, includes the ICAD icon, versioned bundle
metadata, `.icad` document registration, and a hardened ad-hoc local signature
with bundled Qt library validation disabled for local distribution and the
minimal JIT entitlement required by Qt's PCRE2 syntax highlighter; Developer ID
signing can be selected with `-DICAD_MACOS_CODESIGN_IDENTITY="..."`.
Both the build-tree bundle and installed package are signed after their final
link/deployment step, and `viewer.macos_signature` runs strict recursive
verification to catch invalid-page launch failures before packaging.

## CAD output note

STEP is emitted directly as an ISO 10303-21 AP214 analytic B-Rep using
millimetres. Native topology maps vertices, line/circle curves,
plane/cylinder/cone/sphere surfaces, oriented edge loops, advanced faces,
closed shells, and manifold solids. Faceted native results remain planar
advanced-face B-Reps. `inspect-step` checks the Part 21 envelope, entity
uniqueness, references, closed shells, solid count, and assembly occurrences.
Assembly STEP preserves body/component hierarchy while using flattened
world-space geometry. STL, OBJ, embedded glTF 2.0, binary GLB 2.0, and OPC 3MF
are generated from the same validated delivery model. The drawings module also
emits an R2013 ASCII DXF containing genuine 2D LINE/TEXT entities.

## External engineering evidence

ICAD can bind a model to independently produced engineering evidence without
pretending that display geometry is CFD, FEA, fatigue, thermal, test, or
certification evidence. The public `icad.analysis.result.v1`,
`icad.evidence.manifest.v1`, and `icad.compliance.v1` schemas live in
[`docs/schemas`](docs/schemas). Evidence is invalidated when the model revision,
model SHA-256, referenced controlled-input digest, artifact digest, or units no
longer match.

```sh
build/bin/icad evidence-json model.icad --manifest model.evidence.json
build/bin/icad compliance-json model.icad --manifest model.evidence.json \
  --basis EASA-CS-E-AMENDMENT-8
build/bin/icad compliance-report model.icad --manifest model.evidence.json \
  --format html --output build/compliance.html
```

The same evaluation is available through the read-only MCP tools
`icad.evidence.inspect` and `icad.compliance.inspect`. The LSP reports manifest
line and column locations, and ICAD Studio presents the current lifecycle,
release state, and evidence diagnostics in its Evidence panel.

Only `BENCHMARK`, `DEVELOPMENT`, `GROUND_TEST_RELEASED`, and
`DEMONSTRATOR_VALIDATED` can be selected. `TYPE_CERTIFIED` is rejected; an
external authority program and issued certificate remain outside ICAD.

Shapr3D supports STEP for 3D body import, while its DWG/DXF import path is for
2D drawing data. ICAD therefore does not emit a misleading 3D DWG. The current
STEP writer is structurally tested here; final interoperability still needs a
live import in the target CAD version before a release is called certified.

## Agent workflow

Compress a raw mechanical request into one blueprint-aware concept pass before authoring:

```sh
build/bin/icad agent-concept "Design an industrial robot with animated joints and STEP STL OBJ"
build/bin/icad visual-json model.icad > build/model.visual.json
# revise named ICAD history operations directly from model.visual.json, then compile again
```

The concept result uses `icad.agent.concept.v1`, fixes `conceptualIterations` to one, and requires ICAD grammar-only generation. Iterative geometry feedback uses the direct `icad.visual.snapshot.v1` `visual.json`; comparison JSON is reserved for choosing between already-valid alternatives.

1. Start with `icad agent-create` or MCP `icad.agent.create` for the shortest
   prompt-to-artifact path. It embeds maintained compiler-valid robot and bridge
   designs instead of asking a model to rediscover assembly syntax.
2. For a custom topology, call `icad.agent.bootstrap`, edit the returned named
   parameters/vectors/angles, call `icad.visualize`, and reject a poor
   silhouette, placement error, or unintended overlap before calling
   `icad.agent.review`. Repair only its focused next actions.
3. Apply dimensional revisions together with `icad.project.set_parameters` so
   one model decision produces one compiler-validated revision.
4. When exploring alternatives, compare two major structural candidates at a
   time with `icad.compare` (or `compare-json`). Select one before generating
   the next pair. Then reference only stable semantic topology IDs returned by
   ICAD; never patch binary CAD or infer a joint axis from mesh triangles.
5. Read back STEP/STL and run the benchmark/viewer gates before delivery.

This keeps every design change diffable and reproducible. `icad lsp` provides
stdio synchronization, diagnostics, completion, definition navigation,
formatting, and safe syntax quick fixes. The packaged extension lives in
`editors/vscode`; it automatically downloads the matching checksum-verified
compiler/viewer release when no workspace build or configured executable is
available. Its Settings page controls LSP, format/check on save, MCP workspace
configuration, agentic helpers, and installation of the bundled Codex plugin.
Tagged GitHub releases attach its VSIX beside native CLI/viewer packages. The
`icad-agentic-cad` Codex plugin installs both
the guided skill and native MCP server. The roadmap in [`PLAN.md`](PLAN.md)
grows the owned engine toward advanced modeling tools, sketch solving, native
joint pose solving, and richer drawings.

## Project completeness

The v0.21 milestone completes the locally testable work packages in the current
plan. The remaining release boundary is external certification: importing the
generated STEP/DXF/3MF packages in named downstream CAD versions and signing
platform binaries. Structural read-back is not mislabeled as that certification.

## Tests and quality gates

```sh
make test              # all unit, transactions, JSON/MCP, CLI, sandbox, and benchmark tests
make test-unit         # compiler, geometry, engineering, agents, scenes
make test-integration  # CLI contracts and diagnostics
make test-sandbox      # clean-working-directory execution
make benchmark         # model corpus plus stateful incremental-reuse case
make quality           # warnings-as-errors and ASan/UBSan
make sanitizers
make thread-sanitizer  # race-check shared incremental compilation, then restore build
```

The configured suite includes analytic geometry, mesh-fidelity, native viewer,
integration, sandbox, and model benchmark gates. Known-shape tests lock exact
box bounds/area/volume, a hollow annular bore, 96-sample circular edge finishes,
non-overlapping comparison bodies, suppression of coplanar triangulation in CAD
wireframe, preservation of physical crease edges, crease-aware smooth normals,
compact graph-edge validity, and strict macOS bundle signature verification.

The bridge acceptance model contains 6 parameters, 3 materials, 5 bodies, 35
features, 206 feature properties, 1 scene, 2 tracks, and 5 keyframes. Its
benchmark validates all outputs and reads back 35 STEP solids.
It also locks the analytic topology baseline at 250 vertices, 375 edges, and
195 faces.

The robotic-arm benchmark compares against
`examples/Robotic_Arm_3D_Model`: 10 reference STL component files, 23,314
reference facets, 20 reference STEP solids, and 21 reference assembly
occurrences. The native `robotic_arm.icad` acceptance design builds 10
components, 27 solids, and 2,612 deterministic facets. It uses one coherent
world datum chain, tapered arm/wrist shells, toothed gear profiles, opposed
hooked gripper fingers, and flange fasteners. The benchmark validates four
deterministic agent-readable depth views, exact mesh-volume attachment at all
nine parent-child interfaces, and three samples of a 27-keyframe scene that
drives all nine moving degrees of freedom while the base remains grounded. Its
topology baseline is 272 exact vertices, 408 exact edges, and 190 exact faces. This is a
recognizable articulated acceptance model, not a claim that it duplicates the
supplied proprietary surface model.

The agentic prompt benchmark runs a single `agent-create` command from a short
robotic-arm request, requires one expected model iteration, verifies 10 bodies,
10 joints, 9 driven degrees of freedom, valid topology, and structurally reads
back the resulting 25-solid STEP assembly and the visual snapshot. That same test directly validates the
supplied reference folder's 10 STL components and 20-solid STEP baseline before
comparing the generated structural result.

The boolean benchmark evaluates overlapping `UNION`, through `CUT`, and
`INTERSECT` results with a combined volume of 2,840 mm3. It validates repaired
closed-shell topology and reads back three native STEP and STL solids.

The modeling-tools benchmark verifies stable edge selection, fillet, chamfer,
four linear-pattern instances, named-plane mirror, eight validated solids, and
STEP/STL structural read-back.

The advanced-surfaces benchmark verifies named-point polyline sweep,
two-profile loft, a nine-section twisted free-form morph, curved full
revolution, four validated solids, 2,148 facets, and STEP/STL read-back.

The constrained-sketch benchmark solves a parameter-driven rectangle to zero
remaining DOF, converts the result into an extrusion profile, and validates the
solid through STEP/STL structural read-back.

The incremental benchmarks prove full reuse for unchanged input, selective
one-body recomputation in a two-body project, and 8-of-10 delivery-mesh reuse
after a robotic-arm parameter edit. The latter also byte-compares the live
incremental viewer model with a clean full rebuild.

The geometric-query benchmark locks a 0.001 mm policy, an exact 10 mm
polyhedral closest distance, and a tolerance-aware named-body section result.

## Repository layout

```text
ICAD/
├── cmake/                 build policy plus embedded agent design templates
├── docs/                  architecture notes
├── examples/              executable ICAD designs
├── grammar/               native language EBNF reference
├── include/icad/          public compiler, material, exchange, scene APIs
├── src/
│   ├── compiler/          frontend, resolver, semantic IR
│   ├── cad/               native geometry engine
│   ├── constraints/       geometric clearance validation
│   ├── document/          fingerprints, revisions, BOM export
│   ├── manufacturing/     manufacturability rule reporting
│   ├── drawings/          orthographic SVG output
│   ├── desktop_viewer/    native Qt/OpenGL live IDE and CAD viewport
│   ├── agent/             prompt intent, embedded templates, composite review
│   ├── ai/                stable JSON inspection and diagnostics
│   ├── lsp/               dependency-free stdio diagnostics server
│   ├── mcp/               provider-neutral MCP tool server
│   ├── json/              strict native JSON parser and serializer
│   ├── project/           staged full artifact-package builder
│   ├── materials/         predefined PBR material library
│   ├── exchange/          direct STEP, STL, and OBJ writers
│   ├── scene/             renderer-neutral native scene and texture writer
│   └── cli/               command-line driver
├── editors/vscode/        packaged ICAD language extension
├── .github/workflows/     CI and tagged multi-platform releases
├── tests/                 unit, integration, sandbox, benchmark
├── CMakeLists.txt
├── Makefile
└── PLAN.md
```

Manufacturing validation covers extents, wall-thickness proxy, hole diameter,
tool radius, bend and overhang rules, stock bounds, tolerances, and
process/material compatibility. Drawings project actual native mesh edges into
three views and include overall dimensions, datum/tolerance notes, title data,
and BOM count. These are drawing/manufacturing checks, not CAM toolpaths.

## IntelliSense and cleanup

VS Code settings recommend clangd and CMake Tools. `make` generates
`build/compile_commands.json`; `.clangd` uses it for C++23 navigation and
diagnostics. Install the release VSIX with `code --install-extension
build/icad-agentic-cad.vsix` after running the extension package script.

```sh
make help
make clean
make distclean
```
