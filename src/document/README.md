# Document model

This module owns deterministic project fingerprints, exact hexadecimal source
revisions, compiler-validated atomic source commits, cross-process optimistic
concurrency, immutable source history, restore, parameter transactions,
in-memory IR undo/redo, structural count diffs, and JSON BOM export. The
`icad.bom.v2` exporter is bilingual English/French and derives component
occurrences, fastener totals, connection schedules, and weld-rod estimates
from canonical assembly data. Weld estimates require explicit geometry,
stock, and deposition inputs and remain manufacturing-review estimates.
The exporter reports fastener quantity completeness independently from
procurement readiness. A standard and thread size alone are deliberately not
procurement-ready: length, property class, finish, nut, and washer selections
must also be resolved.
Use `icad bom-json MODEL.icad` for BOM-only automation without producing the
full exchange/render artifact package.
Feature dependency recomputation remains future work. It consumes canonical
IR without exposing mesh indices to agents.
