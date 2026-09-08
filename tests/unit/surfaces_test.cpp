#include "icad/ai/inspector.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT surfaces
UNITS mm
POINT3 p0 0 mm 0 mm 0 mm
POINT3 p1 0 mm 0 mm 20 mm
POINT3 p2 10 mm 0 mm 30 mm
PROFILE square
POINT 0 mm 0 mm
POINT 8 mm 0 mm
POINT 8 mm 8 mm
POINT 0 mm 8 mm
END
PROFILE inset
POINT 2 mm 2 mm
POINT 6 mm 2 mm
POINT 6 mm 6 mm
POINT 2 mm 6 mm
END
PROFILE diamond
POINT 4 mm 0 mm
POINT 8 mm 4 mm
POINT 4 mm 8 mm
POINT 0 mm 4 mm
END
PROFILE torus
CIRCLE 10 mm 0 mm 2 mm
END
BODY swept
FEATURE rail
TYPE SWEEP
PROFILE square
PATH p0 p1 p2
END
END
BODY lofted
FEATURE taper
TYPE LOFT
PROFILE square
TARGET_PROFILE inset
HEIGHT 20 mm
ORIGIN_X 30 mm
END
END
BODY freeform
FEATURE twist
TYPE FREEFORM
PROFILE square
TARGET_PROFILE diamond
HEIGHT 30 mm
TWIST 90 deg
COUNT 9
ORIGIN_X 60 mm
END
END
BODY ring
FEATURE torus
TYPE REVOLVE
PROFILE torus
ANGLE 360 deg
ORIGIN_X 100 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto has_code(const icad::compiler::CompileResult& result,
                            std::string_view code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok() || !compilation.topology_model ||
        !icad::cad::validate_topology(*compilation.topology_model).valid() ||
        compilation.topology_model->solids.size() != 4)
        return fail("advanced surface fixture did not produce four valid solids");
    const auto metrics = icad::cad::analyze(*compilation.ir_project);
    if (std::abs(metrics.volume_mm3 - 4120.0) > 1e-2) {
        return fail("advanced surface volume changed unexpectedly");
    }
    if (compilation.topology_model->solids.back().euler_characteristic() != 0)
        return fail("curved revolution did not preserve torus genus");
    const auto inspection = icad::ai::project_json(*compilation.ir_project);
    if (!inspection.contains("\"surfaceOperations\":4") ||
        !inspection.contains("\"path\":[\"p0\",\"p1\",\"p2\"]") ||
        !inspection.contains("\"targetProfile\":\"diamond\"") ||
        !inspection.contains("\"sections\":9"))
        return fail("agent inspection omitted advanced-surface semantics");

    const auto repeated_path = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nPOINT3 p 0 mm 0 mm 0 mm\nPROFILE s\n"
        "POINT 0 mm 0 mm\nPOINT 1 mm 0 mm\nPOINT 0 mm 1 mm\nEND\nBODY b\n"
        "FEATURE f\nTYPE SWEEP\nPROFILE s\nPATH p p\nEND\nEND\n");
    if (repeated_path.ok() || !has_code(repeated_path, "ICAD-S0036"))
        return fail("semantic analysis accepted a repeated sweep path point");
    const auto bad_freeform = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nPROFILE s\nPOINT 0 mm 0 mm\nPOINT 1 mm 0 mm\n"
        "POINT 0 mm 1 mm\nEND\nBODY b\nFEATURE f\nTYPE FREEFORM\nPROFILE s\n"
        "TARGET_PROFILE s\nHEIGHT 1 mm\nTWIST 0 deg\nCOUNT 2\nEND\nEND\n");
    if (bad_freeform.ok() || !has_code(bad_freeform, "ICAD-S0036"))
        return fail("semantic analysis accepted an unsafe free-form section count");
    return 0;
}
