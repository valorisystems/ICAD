#include "icad/compiler/compiler.hpp"
#include "icad/json/value.hpp"
#include "icad/materials/library.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    if (icad::materials::all_profiles().size() != 25)
        return fail("engineering material catalog coverage changed unexpectedly");
    if (icad::materials::profile_classes().size() != 6 ||
        icad::materials::profile_subclasses().size() < 18)
        return fail("material class taxonomy is unexpectedly narrow");
    const auto* titanium = icad::materials::find_profile("Ti6Al4V");
    if (titanium == nullptr || titanium->id != "ATI_TI_6AL_4V_GRADE5_BAR" ||
        titanium->composition.size() != 8 || titanium->sources.empty()) {
        return fail("material alias did not resolve to the source-traceable profile");
    }
    const auto* roof = icad::materials::find_profile("HACIERCO_85_280_075");
    const auto* wall = icad::materials::find_profile("TRAPEZA_8_125_25_075");
    const auto* filler = icad::materials::find_profile("LNT26_2_4");
    if (roof == nullptr || wall == nullptr || filler == nullptr || roof->sources.size() < 2 ||
        wall->sources.size() < 2 || filler->sources.empty()) {
        return fail("hangar envelope or weld-consumable supplier profile is incomplete");
    }
    const auto package_mass = std::ranges::find(filler->properties, "package_mass",
                                                 &icad::materials::Property::name);
    if (package_mass == filler->properties.end() || package_mass->value.typical != 5.0 ||
        package_mass->value.unit != "kg") {
        return fail("weld-consumable supplier package mass is not preserved");
    }
    const auto catalog = icad::json::parse(icad::materials::catalog_json());
    if (!catalog.ok() || catalog.value->find("schema") == nullptr ||
        catalog.value->find("profiles") == nullptr ||
        catalog.value->find("profiles")->array()->size() != 25) {
        return fail("material catalog did not serialize as valid versioned JSON");
    }
    const auto superalloys = icad::json::parse(icad::materials::catalog_json("nickel_superalloy"));
    if (!superalloys.ok() || superalloys.value->find("profiles")->array()->size() != 2)
        return fail("material subclass filtering did not select both nickel superalloys");
    const auto compiled = icad::compiler::compile(
        "REQUIRES CAPABILITY MATERIAL_PROFILE_V1\nPROJECT material_profile\nUNITS mm\n"
        "MATERIAL blade\nPROFILE ATI_TI_6AL_4V_GRADE5_BAR\nEND\n"
        "BODY sample\nMATERIAL blade\nFEATURE stock\nTYPE BOX\nWIDTH 1 mm\nDEPTH 2 mm\n"
        "HEIGHT 3 mm\nEND\nEND\n");
    if (!compiled.ok() || compiled.ir_project->materials.size() != 1 ||
        compiled.ir_project->materials.front().profile != "ATI_TI_6AL_4V_GRADE5_BAR" ||
        compiled.ir_project->materials.front().preset != "TITANIUM") {
        return fail("physical material profile did not lower with its default appearance");
    }
    const auto unknown = icad::compiler::compile(
        "PROJECT bad_material\nUNITS mm\nMATERIAL bad\nPROFILE INVENTED_ALLOY\nEND\n");
    if (unknown.ok() || unknown.diagnostics.back().code != "ICAD-S0037")
        return fail("unknown engineering material profile was accepted");
    return 0;
}
