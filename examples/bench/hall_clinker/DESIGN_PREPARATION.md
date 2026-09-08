# Hall Clinker CEN26A-01-88101 — design preparation / préparation de conception

Status / Statut: reference-driven CAD acceptance model / modèle CAO d'acceptation basé sur la référence.
It reproduces the calculation-note arrangement and schedules for BOM/export testing; it is not an
independent structural calculation, construction release, or approval. / Il reproduit l'implantation
et les nomenclatures de la note de calcul pour tester les exports; il ne constitue ni une note de
calcul indépendante, ni une autorisation de construction, ni une approbation.

## Evidence / Données de référence

- **Given / Donné:** PDF `CEN26A-01-88101_1 -- HALL CLINKER - NDC.pdf`, revision A,
  first issue dated 20 July 2026, 92 pages.
- **Given / Donné:** 90,000 mm hall length, approximately 47,900 mm width, 9,000 mm bays,
  12,500 mm eaves and 17,300 mm ridge.
- **Given / Donné:** IPE 600 columns, IPE 450 rafters, HEA 120/140 secondary members,
  CAE 80x8 bracing, and Z250x3 purlins.
- **Given / Donné:** S275 hot-rolled primary steel, S235 plate, ordinary class 6.8 bolts,
  C25/30 concrete, HA500 reinforcement and 2 bar stated soil bearing capacity.
- **Given / Donné:** calculation bases CM66 + addendum 80, NV65, NF EN 1993-1-8:2005,
  BAEL 91 rev. 99 and DTU 13.12. The source also reports Robot Structural Analysis 2024
  connection checks against NF EN 1993-1-8:2005/NA:2007/AC:2009.
- **Derived / Dérivé:** 11 portal frames at 0–90 m, with ten 9 m longitudinal bays.
- **Derived / Dérivé:** each roof half is approximately 24,426 mm long at 11.31 degrees.
- **Assumed / Supposé:** roof sheeting uses ArcelorMittal Hacierco 85/280 in selected
  S350GD+Z275, Hairplus 25 and 0.75 mm form. The 1,130 mm profile width and 2–16.5 m
  length range come from the 2025 manufacturer sheet; two approximately 12.29 m pieces with
  a 150 mm longitudinal lap cover each 24,426.267 mm roof half. A 1.25 mm CAD separation at
  the lap prevents coincident solids while the final lap fastening/sealing detail remains open.
  / La couverture utilise, par hypothèse contrôlée, le profil
  ArcelorMittal Hacierco 85/280 en S350GD+Z275, Hairplus 25 et 0,75 mm.
- **Assumed / Supposé:** longitudinal wall cladding uses a 1,000 mm useful-width,
  0.75 mm Trapeza-family profile represented with eight 125 mm corrugation pitches and
  25 mm depth. The exact ordered wall reference, color, laps, flashings, openings and
  fixing schedule remain open. / Le bardage longitudinal est une hypothèse de profil
  Trapeza de largeur utile 1 000 mm; la référence commandée reste à confirmer.
- **Assumed / Supposé:** this acceptance model represents the principal steel/concrete system,
  connection plates, anchors, selected bracing, roof sheets and both longitudinal wall skins;
  gable skins, complete reinforcement cages, drainage, doors, louvers, insulation, gutters,
  flashings and every local fabrication detail remain open.
- **Open / Ouvert:** a construction issue requires signed structural design, current national
  applicability review, geotechnical report, certified material lots, complete load combinations,
  detailed connection schedule, erection method and independent checking.

## Architecture and manufacturing / Architecture et fabrication

- Grounded concrete foundations / semelles en béton ancrées au sol.
- Eleven transverse portal frames / onze portiques transversaux.
- IPE 600 built from analytic web and flange solids using published section dimensions.
- IPE 450 rafters built from analytic web and flange solids, inclined to the ridge.
- HEA and Z-section longitudinal members, CAE 80x8 angle bracing, S235 end/base plates.
- Genuine 0.75 mm profiled roof and longitudinal wall sheet solids, grouped by purchasable
  panel definition and counted by assembly occurrence in the bilingual BOM.
- Bolted knees, ridge splices and base anchors are counted from declared connection quantities.
- Weld rod demand is calculated only from explicitly declared weld size, length, joint quantity,
  stock diameter/length and deposition efficiency.

## Traceability / Traçabilité

| Requirement / Exigence | ICAD entities | Verification / Vérification |
|---|---|---|
| 90 m x 47.9 m envelope | `hall_length`, `hall_width`, frame instances | `inspect-json`, front/right/top renders |
| 9 m bays | `bay_spacing`, frame/purlin instances | instance bounds and drawing dimensions |
| IPE/HEA/CAE member families | named member bodies and features | topology, BOM definition/profile fields |
| S275/S235 material identity | `primary_steel`, `plate_steel` profiles | material catalog and BOM profile IDs |
| Principal connection hardware | named `INTERFACE` and `CONNECT` records | manufacturing and BOM schedules |
| Roof and longitudinal side walls | `roof_panel_*`, `wall_cladding_*` | grouped quantities, developed area and calculated mass |
| Bilingual BOM | `icad.bom.v2` | English/French field assertions |
| Exchange fidelity | STEP, assembly STEP, GLB, 3MF, SVG and DXF | independent read-back commands |

## Acceptance / Réception

The source must compile without diagnostics, produce genuine nonzero solids, render coherent front,
right and top views, report no unintended penetration, produce a bilingual calculated BOM, pass
manufacturing/topology checks and read back the exported exchange files. Any pass is a CAD/export
acceptance result only. / La source doit compiler sans diagnostic, produire des solides réels,
présenter des vues cohérentes, ne signaler aucune pénétration non prévue, générer une nomenclature
bilingue calculée et permettre la relecture des exports. Le résultat reste une validation CAO/export.
