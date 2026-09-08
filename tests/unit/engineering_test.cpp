#include "icad/cad/analysis.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/document/exporter.hpp"
#include "icad/document/revision.hpp"
#include "icad/drawings/exporter.hpp"
#include "icad/manufacturing/validator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT EngineeringTest
UNITS mm
MATERIAL frame STRUCTURAL_STEEL
BODY chassis
MATERIAL frame
FEATURE rail
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 5 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto contents(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok()) {
        return fail("engineering fixture did not compile");
    }
    const auto& project = *compilation.ir_project;
    const auto analysis = icad::cad::analyze(project);
    if (analysis.parts.size() != 1 || analysis.volume_mm3 < 999.9 ||
        analysis.surface_area_mm2 < 699.9) {
        return fail("engineering analysis produced incorrect metrics");
    }
    icad::document::RevisionStore revisions{project};
    const auto first_revision = revisions.revision();
    auto changed = project;
    changed.name = "EngineeringTestV2";
    if (revisions.commit(changed, first_revision + 1) ||
        !revisions.commit(std::move(changed), first_revision) || !revisions.undo() ||
        !revisions.redo()) {
        return fail("optimistic revision history failed");
    }

    const auto report = icad::manufacturing::validate(project);
    if (!report.passed || !report.issues.empty() || report.process != "GENERAL" ||
        report.checked_rules != 8) {
        return fail("manufacturing validation rejected a sound solid");
    }
    auto sheet_rules = icad::manufacturing::Rules{};
    sheet_rules.process = "SHEET_METAL";
    const auto sheet_report = icad::manufacturing::validate(project, sheet_rules);
    if (!sheet_report.passed) {
        return fail("sheet-metal rules rejected structural steel");
    }
    auto incompatible = project;
    incompatible.materials.front().preset = "PLASTIC";
    if (icad::manufacturing::validate(incompatible, sheet_rules).passed) {
        return fail("sheet-metal rules accepted an incompatible material");
    }
    const auto conical_hole = icad::compiler::compile(
        "PROJECT conical_hole\nUNITS mm\nMATERIAL alloy ALUMINUM\nBODY nozzle\n"
        "MATERIAL alloy\nFEATURE stock\nTYPE CYLINDER\nRADIUS 5 mm\nHEIGHT 10 mm\nEND\n"
        "FEATURE bore\nTYPE CONE\nOPERATION CUT\nRADIUS1 1 mm\nRADIUS2 2 mm\n"
        "HEIGHT 12 mm\nORIGIN_Z -1 mm\nEND\nEND\n");
    if (!conical_hole.ok() ||
        !icad::manufacturing::validate(*conical_hole.ir_project).passed)
        return fail("manufacturing rules treated a valid conical bore as zero diameter");

    const auto output_root = std::filesystem::current_path() / "engineering-test-output";
    std::filesystem::create_directories(output_root);
    const auto bom_path = output_root / "model.bom.json";
    const auto report_path = output_root / "model.manufacturing.json";
    const auto drawing_path = output_root / "model.drawing.svg";
    const auto dxf_path = output_root / "model.drawing.dxf";
    if (!icad::document::write_bom(project, bom_path).success ||
        !icad::manufacturing::write_report(project, report_path) ||
        !icad::drawings::write_svg(project, drawing_path).success ||
        !icad::drawings::write_dxf(project, dxf_path).success ||
        !icad::drawings::inspect_dxf(dxf_path).success) {
        return fail("engineering artifact export failed");
    }
    if (!contents(bom_path).contains("\"volumeMm3\":") ||
        !contents(bom_path).contains("\"schema\":\"icad.bom.v2\"") ||
        !contents(bom_path).contains("\"languages\":[\"en\",\"fr\"]") ||
        !contents(bom_path).contains("\"fr\":\"Nomenclature calculée\"") ||
        !contents(bom_path).contains("\"fr\":\"Nomenclature calculée générée\"") ||
        !contents(bom_path).contains("\"occurrences\":[") ||
        !contents(bom_path).contains("\"componentLineItems\":") ||
        !contents(report_path).contains("\"passed\":true") ||
        !contents(report_path).contains("\"checkedRules\":8") ||
        !contents(drawing_path).contains("<svg") ||
        !contents(drawing_path).contains("data-sheet-kind=\"part\"") ||
        !contents(drawing_path).contains("PART DETAIL") ||
        !contents(drawing_path).contains("FEATURE AND PARAMETER SCHEDULE") ||
        !contents(drawing_path).contains("SKETCH / PROFILE SCHEDULE") ||
        !contents(drawing_path).contains("data-sheet-kind=\"assembly\"") ||
        !contents(drawing_path).contains("GENERAL ARRANGEMENT") ||
        !contents(drawing_path).contains("LONGITUDINAL SECTION A-A") ||
        !contents(drawing_path).contains("class=\"centerline\"") ||
        !contents(drawing_path).contains("ISO 129-1") ||
        !contents(drawing_path).contains("ISO 5456-2") ||
        !contents(drawing_path).contains("ISO 7200") ||
        !contents(drawing_path).contains("ASSEMBLY CONNECTION SCHEDULE") ||
        !contents(drawing_path).contains("Third-angle projected native edges") ||
        !contents(drawing_path).contains("DATUMS: A | B | C") ||
        !contents(dxf_path).contains("VISIBLE_TOP") ||
        !contents(dxf_path).contains("TITLE_BLOCK") ||
        !contents(dxf_path).contains("GENERAL TOLERANCE")) {
        return fail("engineering artifacts do not contain required data");
    }

    const auto welded = icad::compiler::compile(
        "REQUIRES CAPABILITY CALCULATED_BILINGUAL_BOM_V1\nPROJECT welded\nUNITS mm\n"
        "POINT3 seam 10 mm 5 mm 5 mm\nVECTOR xp 1 0 0\nVECTOR xn -1 0 0\n"
        "MATERIAL steel STRUCTURAL_STEEL\n"
        "BODY first\nMATERIAL steel\nFEATURE stock\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\n"
        "HEIGHT 10 mm\nEND\nEND\n"
        "BODY second\nMATERIAL steel\nFEATURE stock\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\n"
        "HEIGHT 10 mm\nORIGIN_X 10 mm\nEND\nEND\n"
        "INTERFACE seam_a BODY first AT seam AXIS xp TYPE WELD_SEAM SIZE 3 mm\n"
        "INTERFACE seam_b BODY second AT seam AXIS xn TYPE WELD_SEAM SIZE 3 mm\n"
        "CONNECT welded_joint seam_a seam_b METHOD WELDED STANDARD ISO_2553 FILLER LINCOLN_LNT_26_ER70S_6_2_4X1000 "
        "PROCESS GTAW QUANTITY 2 WELD_SIZE 3 mm WELD_LENGTH 1000 mm "
        "FILLER_DIAMETER 1.6 mm STOCK_LENGTH 1000 mm DEPOSITION_EFFICIENCY 0.8 AUTO\n");
    if (!welded.ok())
        return fail("weld resource fixture did not compile");
    const auto weld_bom_path = output_root / "welded.bom.json";
    if (!icad::document::write_bom(*welded.ir_project, weld_bom_path).success)
        return fail("weld resource BOM export failed");
    const auto weld_bom = contents(weld_bom_path);
    if (!weld_bom.contains("\"fr\":\"Baguette de soudage\"") ||
        !weld_bom.contains("\"model\":\"LINCOLN_LNT_26_ER70S_6_2_4X1000\"") ||
        !weld_bom.contains("\"calculatedRodQuantity\":6") ||
        !weld_bom.contains("\"supplierPackageMassKg\":5") ||
        !weld_bom.contains("\"calculatedPackageQuantity\":1") ||
        !weld_bom.contains("\"quantityExplicit\":true")) {
        return fail("calculated bilingual weld and fastener BOM data is incomplete");
    }
    return 0;
}
