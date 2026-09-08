#include "icad/compiler/compiler.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/document/exporter.hpp"
#include "icad/manufacturing/validator.hpp"
#include "icad/scene/exporter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT assembly_semantics
UNITS mm
TOLERANCE LINEAR 0.001 mm ANGULAR 0.001 deg
POINT3 origin 0 mm 0 mm 0 mm
POINT3 upper 0 mm 0 mm 10 mm
POINT3 hinge_point 0 mm 0 mm 10 mm
VECTOR hinge_axis 0 0 1
MATERIAL alloy ALUMINUM
BODY link_definition
MATERIAL alloy
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
END
INSTANCE upper_link OF link_definition AT upper ROTATION 0 deg 0 deg 0 deg
JOINT hinge REVOLUTE WORLD upper_link AT hinge_point AXIS hinge_axis VALUE 0 deg LIMIT -90 deg 90 deg
MATE seated FACE link_definition Z_MAX upper_link Z_MIN OFFSET 0 mm
MATE aligned EDGE link_definition X_AT_Y_MIN_Z_MAX upper_link X_AT_Y_MIN_Z_MIN TOLERANCE 0.001 mm
SCENE articulate
DURATION 2 s
FPS 30
BACKGROUND STUDIO
TRACK hinge_motion JOINT hinge
KEYFRAME 0 s VALUE -45 deg
KEYFRAME 1 s VALUE 45 deg
KEYFRAME 2 s VALUE -45 deg
END
END
)ICAD";

[[nodiscard]] auto contains(const std::string& text, std::string_view value) -> bool {
    return text.find(value) != std::string::npos;
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok()) {
        std::cerr << "mate and joint-animation fixture did not compile\n";
        return 1;
    }
    const auto& project = *compilation.ir_project;
    if (project.mates.size() != 2 || project.scenes.size() != 1 ||
        project.scenes.front().tracks.front().target_kind != "JOINT" ||
        project.scenes.front().tracks.front().keyframes[1].joint_value != 45.0) {
        std::cerr << "mate or joint animation did not lower to canonical IR\n";
        return 1;
    }
    const auto validation = icad::constraints::validate(project);
    if (validation.size() != 2 || !icad::constraints::all_passed(validation) ||
        validation[0].actual_mm != 0.0 || validation[1].actual_mm != 0.0) {
        std::cerr << "face or edge mate validation failed\n";
        return 1;
    }
    const auto manufacturing = icad::manufacturing::validate(project);
    if (!manufacturing.passed || !manufacturing.issues.empty()) {
        std::cerr << "instance did not inherit its definition manufacturing material\n";
        return 1;
    }

    const auto base =
        std::filesystem::current_path() / "assembly-semantics-output" / "articulation";
    const auto exported = icad::scene::export_scene(project, base);
    if (!exported.success || exported.tracks != 1 || exported.keyframes != 3) {
        std::cerr << "joint-driven native scene export failed\n";
        return 1;
    }
    std::ifstream scene{base.string() + ".scene.json", std::ios::binary};
    const std::string json{std::istreambuf_iterator<char>{scene},
                           std::istreambuf_iterator<char>{}};
    if (!contains(json, "\"targetKind\":\"JOINT\"") ||
        !contains(json, "\"value\":45") || !contains(json, "\"pivotMm\":[0,0,10]") ||
        !contains(json, "\"axisUnit\":[0,0,1]")) {
        std::cerr << "joint animation metadata is incomplete\n";
        return 1;
    }
    const auto bom_path = base.string() + ".bom.json";
    if (!icad::document::write_bom(project, bom_path).success) {
        std::cerr << "assembly BOM export failed\n";
        return 1;
    }
    std::ifstream bom{bom_path, std::ios::binary};
    const std::string bom_json{std::istreambuf_iterator<char>{bom},
                               std::istreambuf_iterator<char>{}};
    if (!contains(bom_json, "\"occurrences\":[\"link_definition\",\"upper_link\"]") ||
        !contains(bom_json, "\"definition\":\"link_definition\"")) {
        std::cerr << "assembly BOM omitted the instance occurrence\n";
        return 1;
    }

    const auto invalid_selector = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY a\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\n"
        "HEIGHT 1 mm\nEND\nEND\nBODY b\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\n"
        "HEIGHT 1 mm\nEND\nEND\nMATE invalid FACE a TOP b Z_MIN OFFSET 0 mm\n");
    if (invalid_selector.ok() || invalid_selector.diagnostics.back().code != "ICAD-S0034") {
        std::cerr << "invalid semantic mate selector was accepted\n";
        return 1;
    }

    const auto invalid_keyframe = icad::compiler::compile(
        "PROJECT bad_animation\nUNITS mm\nPOINT3 o 0 mm 0 mm 0 mm\nVECTOR z 0 0 1\n"
        "BODY d\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n"
        "INSTANCE i OF d AT o ROTATION 0 deg 0 deg 0 deg\n"
        "JOINT j REVOLUTE WORLD i AT o AXIS z VALUE 0 deg LIMIT -10 deg 10 deg\n"
        "SCENE s\nDURATION 1 s\nFPS 30\nBACKGROUND STUDIO\nTRACK t JOINT j\n"
        "KEYFRAME 0 s VALUE 0 deg\nKEYFRAME 1 s VALUE 20 deg\nEND\nEND\n");
    if (invalid_keyframe.ok()) {
        std::cerr << "out-of-limit joint animation keyframe was accepted\n";
        return 1;
    }
    return 0;
}
