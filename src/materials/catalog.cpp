#include "icad/materials/library.hpp"

#include "icad/json/value.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace icad::materials {
namespace {

using Optional = std::optional<double>;

[[nodiscard]] auto value(Optional minimum, Optional typical, Optional maximum,
                         std::string unit, std::string qualifier) -> NumericValue {
    return {minimum, typical, maximum, std::move(unit), std::move(qualifier)};
}

[[nodiscard]] auto exact(double number, std::string unit, std::string qualifier = "nominal")
    -> NumericValue {
    return value(std::nullopt, number, std::nullopt, std::move(unit), std::move(qualifier));
}

[[nodiscard]] auto limits(Optional minimum, Optional maximum, std::string unit = "mass_percent")
    -> NumericValue {
    return value(minimum, std::nullopt, maximum, std::move(unit), "specified_limit");
}

[[nodiscard]] auto composition(std::string constituent, Optional minimum, Optional typical,
                               Optional maximum, bool balance = false,
                               std::string qualifier = "specified_limit") -> CompositionEntry {
    return {std::move(constituent),
            value(minimum, typical, maximum, "mass_percent", std::move(qualifier)), balance, {}};
}

[[nodiscard]] auto property(std::string name, NumericValue property_value,
                            Optional temperature_c = std::nullopt, std::string condition = {},
                            std::string test_method = {}) -> Property {
    return {std::move(name), std::move(property_value), temperature_c, std::move(condition),
            std::move(test_method)};
}

[[nodiscard]] auto source(std::string publisher, std::string title, std::string document_id,
                          std::string revision, std::string url, std::string location,
                          std::string source_type = "manufacturer_datasheet") -> Source {
    return {std::move(publisher), std::move(title), std::move(document_id), std::move(revision),
            std::move(url), "2026-09-07", std::move(location), std::move(source_type)};
}

const std::vector<Profile> profiles{
    {"KAISER_ALUMINUM_6061_T6_T651_SHEET_PLATE",
     "Kaiser Aluminum 6061-T6/T651 sheet, coil and plate", "metal", "aluminum_alloy", "6061",
     "T6/T651", {"AA6061_T6", "AL6061_T6", "6061_T651"}, "ALUMINUM", "fully_disclosed",
     {composition("Si", 0.40, {}, 0.80), composition("Fe", {}, {}, 0.70),
      composition("Cu", 0.15, {}, 0.40), composition("Mn", {}, {}, 0.15),
      composition("Mg", 0.80, {}, 1.20), composition("Cr", 0.04, {}, 0.35),
      composition("Zn", {}, {}, 0.25), composition("Ti", {}, {}, 0.15),
      composition("Al", {}, {}, {}, true)},
     {property("density", exact(2.70, "g/cm3"), 20.0),
      property("elastic_modulus", exact(68.3, "GPa", "typical"), 20.0, "T6/T651"),
      property("ultimate_tensile_strength", exact(310.0, "MPa", "typical"), 20.0,
               "T6/T651"),
      property("yield_strength", exact(276.0, "MPa", "typical"), 20.0, "T6/T651"),
      property("elongation_4D", exact(17.0, "percent", "typical"), 20.0, "T6/T651"),
      property("thermal_conductivity", exact(167.0, "W/(m*K)", "typical"), 20.0,
               "T6/T651")},
     {{"sheet_coil_plate", "T6/T651", "supplier technical data", "supplier availability",
       "Do not infer purchasable thickness from this profile."}},
     {source("Kaiser Aluminum", "Sheet Coil & Plate Alloy 6061 Technical Data", "1015",
             "05/06",
             "https://online.kaiseraluminum.com/depot/PublicProductInformation/Document/1015/"
             "Kaiser_Aluminum_6061_Sheet_Coil_and_Plate.pdf",
             "pages 1-2")},
     "Typical mechanical values are selection data, not design allowables."},

    {"KAISER_ALUMINUM_7075_T651_SHEET_PLATE",
     "Kaiser Aluminum 7075-T651 sheet, coil and plate", "metal", "aluminum_alloy", "7075",
     "T651", {"AA7075_T651", "AL7075_T651"}, "ALUMINUM", "fully_disclosed",
     {composition("Si", {}, {}, 0.40), composition("Fe", {}, {}, 0.50),
      composition("Cu", 1.20, {}, 2.00), composition("Mn", {}, {}, 0.30),
      composition("Mg", 2.10, {}, 2.90), composition("Cr", 0.18, {}, 0.28),
      composition("Zn", 5.10, {}, 6.10), composition("Ti", {}, {}, 0.20),
      composition("Al", {}, {}, {}, true)},
     {property("density", exact(2.80, "g/cm3"), 20.0),
      property("thermal_conductivity", exact(130.0, "W/(m*K)", "typical"), 20.0,
               "T651")},
     {{"sheet_coil_plate", "T651", "supplier technical data", "supplier availability", {}}},
     {source("Kaiser Aluminum", "Sheet Coil & Plate Alloy 7075 Technical Data", "1017",
             "05/06",
             "https://online.kaiseraluminum.com/depot/PublicProductInformation/Document/1017/"
             "Kaiser_Aluminum_7075_Sheet_Coil_and_Plate.pdf",
             "page 2")},
     "Strength allowables depend on form, thickness, direction, and governing specification."},

    {"ATI_TI_6AL_4V_GRADE5_BAR", "ATI 6-4 Grade 5 bar", "metal", "titanium_alloy",
     "Ti-6Al-4V / Grade 5 / UNS R56400", "bar", {"TI6AL4V", "GRADE5_TI", "UNS_R56400"},
     "TITANIUM", "fully_disclosed",
     {composition("Al", 5.50, {}, 6.75), composition("V", 3.50, {}, 4.50),
      composition("Fe", {}, {}, 0.40), composition("O", {}, {}, 0.20),
      composition("C", {}, {}, 0.08), composition("N", {}, {}, 0.05),
      composition("H", {}, {}, 0.015), composition("Ti", {}, {}, {}, true)},
     {property("density", exact(4.43, "g/cm3")),
      property("ultimate_tensile_strength", limits(931.0, {} , "MPa"), 20.0, "bar"),
      property("yield_strength", limits(862.0, {}, "MPa"), 20.0, "bar"),
      property("elongation", limits(15.0, {}, "percent"), 20.0, "bar"),
      property("hardness", limits({}, 36.0, "HRC"), 20.0, "bar")},
     {{"bar", "supplier form", "ASTM B265; ASME SB-265; MIL-T-9046; AMS 4911",
       "supplier availability", {}}},
     {source("ATI", "ATI 6-4 Titanium", "ATI-6-4-GRADE5", "live product page",
             "https://www.atimaterials.com/Products/Pages/ati-6-4-grade5.aspx",
             "Bar composition and properties")},
     "The ATI page reports different limits by form; this profile is specifically the bar row."},

    {"SPECIAL_METALS_INCONEL_718", "Special Metals INCONEL alloy 718", "metal",
     "nickel_superalloy", "INCONEL 718 / UNS N07718", "solution treated and age-hardenable",
     {"IN718", "ALLOY718", "UNS_N07718"}, "TITANIUM", "fully_disclosed",
     {composition("Ni+Co", 50.0, {}, 55.0), composition("Cr", 17.0, {}, 21.0),
      composition("Fe", {}, {}, {}, true), composition("Nb+Ta", 4.75, {}, 5.50),
      composition("Mo", 2.80, {}, 3.30), composition("Ti", 0.65, {}, 1.15),
      composition("Al", 0.20, {}, 0.80), composition("Co", {}, {}, 1.00),
      composition("C", {}, {}, 0.08), composition("Mn", {}, {}, 0.35),
      composition("Si", {}, {}, 0.35), composition("P", {}, {}, 0.015),
      composition("S", {}, {}, 0.015), composition("B", {}, {}, 0.006),
      composition("Cu", {}, {}, 0.30)},
     {property("density", exact(8.19, "g/cm3", "typical"), 20.0)},
     {{"sheet_plate_bar_forging_tube_wire", "condition varies", "AMS/ASTM forms in bulletin",
       "see applicable procurement specification", {}}},
     {source("Special Metals", "INCONEL alloy 718", "SMC-045", "September 2007",
             "https://www.specialmetals.com/documents/technical-bulletins/inconel/"
             "inconel-alloy-718.pdf",
             "Table 1 and product-form specification tables")},
     "Properties are heat-treatment, product-form, temperature, and orientation dependent."},

    {"SPECIAL_METALS_INCONEL_625", "Special Metals INCONEL alloy 625", "metal",
     "nickel_superalloy", "INCONEL 625 / UNS N06625", "annealed", {"IN625", "UNS_N06625"},
     "TITANIUM", "fully_disclosed",
     {composition("Ni", 58.0, {}, {}), composition("Cr", 20.0, {}, 23.0),
      composition("Fe", {}, {}, 5.0), composition("Mo", 8.0, {}, 10.0),
      composition("Nb+Ta", 3.15, {}, 4.15), composition("C", {}, {}, 0.10),
      composition("Mn", {}, {}, 0.50), composition("Si", {}, {}, 0.50),
      composition("P", {}, {}, 0.015), composition("S", {}, {}, 0.015),
      composition("Al", {}, {}, 0.40), composition("Ti", {}, {}, 0.40),
      composition("Co", {}, {}, 1.0)},
     {},
     {{"billet_bar_plate_sheet_tube_wire", "condition varies", "see bulletin specifications",
       "supplier availability", {}}},
     {source("Special Metals", "INCONEL alloy 625", "SMC-063", "August 2013",
             "https://www.specialmetals.com/documents/technical-bulletins/inconel/"
             "inconel-alloy-625.pdf",
             "Table 1")},
     "Bulletin properties are typical and explicitly not specification values."},

    {"OUTOKUMPU_PRODEC_304L_4307", "Outokumpu Prodec 304L/4307", "metal",
     "austenitic_stainless_steel", "304L / EN 1.4307 / UNS S30403", "solution annealed",
     {"304L", "S30403", "EN_1_4307"}, "STRUCTURAL_STEEL", "typical_only",
     {composition("C", {}, 0.02, {}, false, "typical"),
      composition("Cr", {}, 18.1, {}, false, "typical"),
      composition("Ni", {}, 8.1, {}, false, "typical")},
     {property("density", exact(7.9, "g/cm3", "typical"), 20.0),
      property("elastic_modulus", exact(200.0, "GPa", "typical"), 20.0)},
     {{"plate_hot_rolled_bar_cold_drawn_bar", "annealed", "EN 10088-2/-3",
       "see datasheet thickness/diameter rows", {}}},
     {source("Outokumpu", "Prodec range datasheet", "PRODEC-RANGE", "June 2016",
             "https://www.outokumpu.com/-/media/files/products/prodec/"
             "outokumpu_prodec_range_datasheet.pdf",
             "Tables 1-3")},
     "Composition entries are supplier typical values; the ordered standard controls limits."},

    {"OUTOKUMPU_PRODEC_316L_4404", "Outokumpu Prodec 316L/4404", "metal",
     "austenitic_stainless_steel", "316L / EN 1.4404 / UNS S31603", "solution annealed",
     {"316L", "S31603", "EN_1_4404"}, "STRUCTURAL_STEEL", "typical_only",
     {composition("C", {}, 0.02, {}, false, "typical"),
      composition("Cr", {}, 17.2, {}, false, "typical"),
      composition("Ni", {}, 10.1, {}, false, "typical"),
      composition("Mo", {}, 2.1, {}, false, "typical")},
     {property("density", exact(8.0, "g/cm3", "typical"), 20.0),
      property("elastic_modulus", exact(200.0, "GPa", "typical"), 20.0)},
     {{"plate_hot_rolled_bar_cold_drawn_bar", "annealed", "EN 10088-2/-3",
       "see datasheet thickness/diameter rows", {}}},
     {source("Outokumpu", "Prodec range datasheet", "PRODEC-RANGE", "June 2016",
             "https://www.outokumpu.com/-/media/files/products/prodec/"
             "outokumpu_prodec_range_datasheet.pdf",
             "Tables 1-3")},
     "Composition entries are supplier typical values; the ordered standard controls limits."},

    {"OUTOKUMPU_PRODEC_17_4PH_P800", "Outokumpu Prodec 17-4PH", "metal",
     "precipitation_hardening_stainless_steel", "17-4PH / EN 1.4542 / UNS S17400", "+P800",
     {"17_4PH", "S17400", "EN_1_4542"}, "STRUCTURAL_STEEL", "typical_only",
     {composition("C", {}, 0.02, {}, false, "typical"),
      composition("Cr", {}, 15.5, {}, false, "typical"),
      composition("Ni", {}, 4.8, {}, false, "typical"),
      composition("Cu", {}, 3.4, {}, false, "typical"),
      composition("Nb", {}, std::nullopt, {}, false, "present_not_quantified")},
     {property("density", exact(7.8, "g/cm3", "typical"), 20.0),
      property("elastic_modulus", exact(200.0, "GPa", "typical"), 20.0),
      property("yield_strength", limits(520.0, {}, "MPa"), 20.0, "+P800 hot rolled bar"),
      property("ultimate_tensile_strength", value(800.0, {}, 950.0, "MPa", "specified_range"),
               20.0, "+P800 hot rolled bar")},
     {{"hot_rolled_bar_cold_drawn_bar", "+P800", "EN 10088-3", "see diameter rows", {}}},
     {source("Outokumpu", "Prodec range datasheet", "PRODEC-RANGE", "June 2016",
             "https://www.outokumpu.com/-/media/files/products/prodec/"
             "outokumpu_prodec_range_datasheet.pdf",
             "Tables 1-3")},
     "Do not substitute H900/H1150 properties; this record is explicitly +P800."},

    {"ARCELORMITTAL_S235JR_EN_10025_2", "ArcelorMittal S235JR hot-rolled structural steel",
     "metal", "non_alloy_structural_steel", "S235JR / EN 10025-2", "hot rolled",
     {"S235JR", "EN_10025_2_S235JR"}, "STRUCTURAL_STEEL", "fully_disclosed",
     {composition("C", {}, {}, 0.21), composition("Mn", {}, {}, 1.40),
      composition("P", {}, {}, 0.040), composition("S", {}, {}, 0.040),
      composition("N", {}, {}, 0.012), composition("Cu", {}, {}, 0.55),
      composition("Fe", {}, {}, {}, true)},
     {property("density", exact(7.85, "g/cm3", "nominal"), 20.0),
      property("yield_strength", limits(235.0, {}, "MPa"), 20.0,
               "nominal thickness <= 16 mm"),
      property("yield_strength", limits(225.0, {}, "MPa"), 20.0,
               "nominal thickness > 16 mm and <= 40 mm"),
      property("ultimate_tensile_strength", value(360.0, {}, 510.0, "MPa", "specified_range"),
               20.0, "EN 10025-2 product range"),
      property("charpy_impact_energy", limits(27.0, {}, "J"), 20.0, "JR")},
     {{"hot_rolled_sections_and_plates", "as rolled", "EN 10025-2:2019; EN 10365",
       "section and thickness dependent", "Use the ordered product certificate for acceptance."}},
     {source("ArcelorMittal Europe - Long Products", "Sections and Merchant Bars sales programme",
             "SECTIONS-MB-FR-EN-DE", "2021",
             "https://sections.arcelormittal.com/repository2/Sections/"
             "Sections_MB_ArcelorMittal_FR_EN_RU.pdf",
             "EN 10025-2 chemical and mechanical property tables")},
     "Strength and chemistry depend on thickness and product form; this catalog entry is not a "
     "3.1 inspection certificate."},

    {"ARCELORMITTAL_S275JR_EN_10025_2", "ArcelorMittal S275JR hot-rolled structural steel",
     "metal", "non_alloy_structural_steel", "S275JR / EN 10025-2", "hot rolled",
     {"S275JR", "EN_10025_2_S275JR"}, "STRUCTURAL_STEEL", "fully_disclosed",
     {composition("C", {}, {}, 0.21), composition("Mn", {}, {}, 1.50),
      composition("P", {}, {}, 0.040), composition("S", {}, {}, 0.040),
      composition("N", {}, {}, 0.012), composition("Cu", {}, {}, 0.55),
      composition("Fe", {}, {}, {}, true)},
     {property("density", exact(7.85, "g/cm3", "nominal"), 20.0),
      property("yield_strength", limits(275.0, {}, "MPa"), 20.0,
               "nominal thickness <= 16 mm"),
      property("yield_strength", limits(265.0, {}, "MPa"), 20.0,
               "nominal thickness > 16 mm and <= 40 mm"),
      property("ultimate_tensile_strength", value(410.0, {}, 560.0, "MPa", "specified_range"),
               20.0, "EN 10025-2 product range"),
      property("charpy_impact_energy", limits(27.0, {}, "J"), 20.0, "JR")},
     {{"hot_rolled_sections_and_plates", "as rolled", "EN 10025-2:2019; EN 10365",
       "section and thickness dependent", "Use the ordered product certificate for acceptance."}},
     {source("ArcelorMittal Europe - Long Products", "Sections and Merchant Bars sales programme",
             "SECTIONS-MB-FR-EN-DE", "2021",
             "https://sections.arcelormittal.com/repository2/Sections/"
             "Sections_MB_ArcelorMittal_FR_EN_RU.pdf",
             "EN 10025-2 chemical and mechanical property tables")},
     "Strength and chemistry depend on thickness and product form; this catalog entry is not a "
     "3.1 inspection certificate."},

    {"ARCELORMITTAL_HACIERCO_85_280_S350GD_Z275_HAIRPLUS25_075",
     "ArcelorMittal Hacierco 85/280 S350GD+Z275 Hairplus 25, 0.75 mm",
     "metal", "coated_structural_steel_sheet", "S350GD+Z275 / EN 10346", "Hairplus 25",
     {"HACIERCO_85_280_075", "HACIERCO_S350GD_Z275_075"}, "STRUCTURAL_STEEL",
     "grade_chemistry_not_declared_by_selected_product_sources", {},
     {property("density", exact(7.85, "g/cm3", "nominal steel substrate"), 20.0),
      property("nominal_thickness", exact(0.75, "mm", "selected catalog option")),
      property("mass_per_covered_area", exact(7.814159292, "kg/m2",
               "derived from 1500 mm coil width, 1130 mm profile width, 0.75 mm substrate and 7850 kg/m3")),
      property("minimum_yield_strength", limits(350.0, {}, "MPa"), 20.0,
               "0.3-6.0 mm longitudinal"),
      property("minimum_tensile_strength", limits(420.0, {}, "MPa"), 20.0,
               "0.3-6.0 mm longitudinal"),
      property("minimum_elongation_A80", limits(16.0, {}, "percent"), 20.0,
               "0.7-6.0 mm longitudinal"),
      property("metallic_coating_mass", exact(275.0, "g/m2", "Z275 designation")),
      property("organic_coating_thickness", exact(25.0, "micrometre", "Hairplus 25"))},
     {{"roof_profile_Hacierco_85_280", "profiled and coated", "EN 10346; EN 10143; EN 10169",
       "0.75 mm; 1130 mm profile width; 2-16.5 m length",
       "The project load/span selection remains subject to the product design tables."}},
     {source("ArcelorMittal Building Solutions", "Hacierco 85/280 technical data sheet",
             "HACIERCO-85-280", "2025",
             "https://cdn.buildingsolutions.arcelormittal.com/datasheet/10368571768862/"
             "Technical-datasheet-Hacierco-85-280-2025.pdf",
             "technical parameters and design tables"),
      source("ArcelorMittal Europe - Flat Products", "Hot dip galvanised products catalogue",
             "E20", "live catalogue 2026-09-07",
             "https://industry.arcelormittal.com/catalogue/E20/EN",
             "S350GD+Z dimensions and mechanical properties", "manufacturer_product_catalogue")},
     "The calculation note does not specify a roofing product. Hacierco 85/280 is a controlled "
     "modeling assumption pending the architectural envelope schedule; the "
     "selected product sources do not publish a grade-specific chemical composition table."},

    {"ARCELORMITTAL_TRAPEZA_8_125_25_S350GD_Z275_HAIRPLUS25_075",
     "ArcelorMittal Trapéza 8.125.25 S350GD+Z275 Hairplus 25, 0.75 mm",
     "metal", "coated_structural_steel_sheet", "S350GD+Z275 / EN 10346", "Hairplus 25",
     {"TRAPEZA_8_125_25_075", "TRAPEZA_WALL_075"}, "STRUCTURAL_STEEL",
     "grade_chemistry_not_declared_by_selected_product_sources", {},
     {property("density", exact(7.85, "g/cm3", "nominal steel substrate"), 20.0),
      property("nominal_thickness", exact(0.75, "mm", "selected catalog option")),
      property("mass_per_covered_area", exact(6.98, "kg/m2", "manufacturer table")),
      property("minimum_yield_strength", limits(350.0, {}, "MPa"), 20.0,
               "0.3-6.0 mm longitudinal"),
      property("minimum_tensile_strength", limits(420.0, {}, "MPa"), 20.0,
               "0.3-6.0 mm longitudinal"),
      property("minimum_elongation_A80", limits(16.0, {}, "percent"), 20.0,
               "0.7-6.0 mm longitudinal"),
      property("metallic_coating_mass", exact(275.0, "g/m2", "Z275 designation")),
      property("organic_coating_thickness", exact(25.0, "micrometre", "Hairplus 25"))},
     {{"wall_profile_Trapeza_8_125_25", "profiled and coated",
       "EN 10346; professional recommendations RAGE", "0.75 mm; 1000 mm useful width; 1.8-13 m length",
       "Final color, laps, supports and fixings require the architectural cladding schedule."}},
     {source("ArcelorMittal Building Solutions France", "Produits de bardage, édition 6A",
             "PDT-BARDAGE-6A", "September 2025",
             "https://cdn.buildingsolutions.arcelormittal.com/datasheet/10366245765150/"
             "Pdts-Bardage-Ed.-6A-Septembre-2025-BD.pdf",
             "page 60: geometry, dimensions, thickness, length and mass tables"),
      source("ArcelorMittal Europe - Flat Products", "Hot dip galvanised products catalogue",
             "E20", "live catalogue 2026-09-07",
             "https://industry.arcelormittal.com/catalogue/E20/EN",
             "S350GD+Z dimensions and mechanical properties", "manufacturer_product_catalogue")},
     "The calculation note does not specify a wall-cladding product. Trapéza 8.125.25 is a "
     "controlled modeling assumption pending the architectural envelope schedule; the selected "
     "product sources do not publish a grade-specific chemical composition table."},

    {"LINCOLN_LNT_26_ER70S_6_2_4X1000", "Lincoln Electric LNT 26 ER70S-6 TIG rod 2.4 x 1000 mm",
     "welding_consumable", "carbon_steel_filler_rod", "AWS A5.18 ER70S-6 / EN ISO 636-A W 42 5 3Si1",
     "as supplied", {"LNT26_2_4", "ER70S_6_2_4X1000"}, "STRUCTURAL_STEEL", "typical_only",
     {composition("C", {}, 0.10, {}, false, "typical"),
      composition("Mn", {}, 1.50, {}, false, "typical"),
      composition("Si", {}, 0.90, {}, false, "typical"),
      composition("Fe", {}, {}, {}, true)},
     {property("all_weld_metal_yield_strength", exact(460.0, "MPa", "typical"), 20.0,
               "as welded with I1 shielding gas"),
      property("all_weld_metal_tensile_strength", exact(580.0, "MPa", "typical"), 20.0,
               "as welded with I1 shielding gas"),
      property("all_weld_metal_elongation", exact(26.0, "percent", "typical"), 20.0,
               "as welded with I1 shielding gas"),
      property("density", exact(7.85, "g/cm3", "nominal carbon-steel filler density")),
      property("package_mass", exact(5.0, "kg", "supplier packaging"))},
     {{"tig_filler_rod", "as supplied", "AWS A5.18; EN ISO 636-A",
       "2.4 mm diameter x 1000 mm; 5 kg PE tube; item T24T005R6S00",
       "Qualification testing controls suitability for the actual weld procedure."}},
     {source("Lincoln Electric", "LNT 26 TIG rod mild steel", "LNT-26-EN", "2022-10-11",
             "https://ch-delivery.lincolnelectric.com/api/public/content/"
             "198e4a158b9944d481e64f8373506486?v=2ca2469b",
             "classification, typical chemistry, properties, packaging and available sizes")},
     "Supplier values are typical test results and do not replace a qualified WPS/PQR or lot "
     "certificate."},

    {"SSAB_DOMEX_355MC", "SSAB Domex 355MC", "metal", "structural_hsla_steel", "S355MC",
     "thermomechanically rolled", {"S355MC", "EN_10149_2_S355MC"}, "STRUCTURAL_STEEL",
     "fully_disclosed",
     {composition("C", {}, {}, 0.10), composition("Si", {}, {}, 0.03),
      composition("Mn", {}, {}, 1.50), composition("P", {}, {}, 0.025),
      composition("S", {}, {}, 0.010), composition("Al", 0.015, {}, {}),
      composition("Nb", {}, {}, 0.09), composition("V", {}, {}, 0.20),
      composition("Ti", {}, {}, 0.15), composition("Fe", {}, {}, {}, true)},
     {property("yield_strength", limits(355.0, {}, "MPa"), 20.0,
               "1.80-16.00 mm longitudinal"),
      property("ultimate_tensile_strength", value(430.0, {}, 550.0, "MPa", "specified_range"),
               20.0, "1.80-16.00 mm longitudinal")},
     {{"coil_slit_coil_cut_to_length", "thermomechanically rolled", "EN 10149-2",
       "1.80-16.00 mm thick; width up to 1860 mm", {}}},
     {source("SSAB", "SSAB Domex 355MC", "DOMEX-355MC", "live product page",
             "https://www.ssab.com/en-gb/brands-and-products/ssab-domex/product-offer/355mc",
             "dimension, mechanical, and ladle-analysis tables")},
     "Nb+V+Ti is additionally limited to 0.22 mass percent; the Si value is coating-category A."},

    {"UDDEHOLM_ORVAR_SUPREME", "Uddeholm Orvar Supreme", "metal", "hot_work_tool_steel",
     "AISI H13 / W.Nr. 1.2344", "soft annealed ESR", {"H13_ESR", "1_2344"},
     "STRUCTURAL_STEEL", "typical_only",
     {composition("C", {}, 0.39, {}, false, "nominal"),
      composition("Si", {}, 1.00, {}, false, "nominal"),
      composition("Mn", {}, 0.40, {}, false, "nominal"),
      composition("Cr", {}, 5.20, {}, false, "nominal"),
      composition("Mo", {}, 1.40, {}, false, "nominal"),
      composition("V", {}, 0.90, {}, false, "nominal"),
      composition("Fe", {}, {}, {}, true)},
     {property("supplied_hardness", exact(180.0, "HB", "approximate"), 20.0,
               "soft annealed")},
     {{"round_bar_flat_bar", "soft annealed", "AISI H13; W.Nr. 1.2344",
       "supplier stock program", {}}},
     {source("Uddeholm", "Uddeholm Orvar Supreme", "ORVAR-SUPREME", "live product page",
             "https://www.uddeholm.com/us/en-us/products/uddeholm-orvar-supreme-2/",
             "Chemical composition and designations")},
     "Nominal analysis is not a purchase acceptance range."},

    {"OVAKO_100CR6_803D", "Ovako 100Cr6 variant 803D", "metal", "bearing_steel",
     "100Cr6 / SAE 52100 / EN 1.3505", "803D ingot cast", {"SAE52100", "100CR6_803D"},
     "CHROME", "fully_disclosed",
     {composition("C", 0.98, {}, 1.05), composition("Si", 0.20, {}, 0.35),
      composition("Mn", 0.30, {}, 0.40), composition("P", {}, {}, 0.025),
      composition("S", 0.017, {}, 0.023), composition("Cr", 1.40, {}, 1.60),
      composition("Ni", {}, {}, 0.25), composition("Mo", {}, {}, 0.08),
      composition("Fe", {}, {}, {}, true)},
     {},
     {{"bar_tube_ring", "variant 803D", "ISO 683-17", "supplier availability", {}}},
     {source("Ovako", "100Cr6 material data sheet", "100Cr6-803D", "2025-01-15",
             "https://steelnavigator.ovako.com/steel-grades/100cr6/pdf",
             "Chemical composition, variant 803D")},
     "Other Ovako 100Cr6 variants have different chemistry limits and must use separate IDs."},

    {"CDA_C11000_ETP_COPPER", "CDA C11000 electrolytic tough pitch copper", "metal",
     "wrought_copper", "UNS C11000", "temper-specific", {"C11000", "ETP_COPPER"}, "COPPER",
     "fully_disclosed",
     {composition("Cu+Ag", 99.90, {}, {})},
     {property("density", exact(8.91, "g/cm3", "typical"), 20.0),
      property("elastic_modulus", exact(117.2, "GPa", "typical"), 20.0),
      property("thermal_conductivity", exact(391.1, "W/(m*K)", "typical"), 20.0)},
     {{"flat_bar_forging_tube", "temper-specific", "ASTM B370/B124/B698/B903",
       "standard-specific", {}}},
     {source("Copper Development Association", "C11000 Alloy", "C11000", "live database",
             "https://alloys.copper.org/alloy/C11000", "composition and physical properties",
             "industry_association_database")},
     "Mechanical properties are form, temper, section-size, and cold-work dependent."},

    {"EASTERN_ALLOYS_ZAMAK_3_INGOT", "Eastern Alloys Zamak 3 ingot", "metal",
     "zinc_die_casting_alloy", "Zamak 3 / ASTM B240", "ingot", {"ZAMAK3", "ASTM_B240_ZAMAK3"},
     "CHROME", "fully_disclosed",
     {composition("Al", 3.90, {}, 4.30), composition("Mg", 0.03, {}, 0.06),
      composition("Cu", {}, {}, 0.10), composition("Fe", {}, {}, 0.035),
      composition("Pb", {}, {}, 0.004), composition("Cd", {}, {}, 0.003),
      composition("Sn", {}, {}, 0.0015), composition("Zn", {}, {}, {}, true)},
     {property("density", exact(6.6, "g/cm3", "typical"), 20.0),
      property("ultimate_tensile_strength", exact(283.0, "MPa", "typical"), 20.0,
               "die cast"),
      property("yield_strength", exact(221.0, "MPa", "typical"), 20.0, "die cast")},
     {{"ingot_for_die_casting", "ingot", "ASTM B240", "supplier ingot dimensions", {}}},
     {source("Eastern Alloys", "Zamak 3", "ZAMAK-3", "live product page",
             "https://www.eazall.com/zamak-3", "ASTM B240 ingot chemical-analysis row")},
     "The ASTM B86 die-cast chemistry row differs slightly and is not represented by this ID."},

    {"VICTREX_PEEK_450G", "VICTREX PEEK 450G", "polymer", "unreinforced_thermoplastic",
     "PEEK 450G", "semi-crystalline natural granules", {"PEEK450G", "VICTREX450G"}, "PLASTIC",
     "proprietary_formulation_not_disclosed", {},
     {property("density", exact(1.30, "g/cm3", "nominal"), 23.0, "crystalline", "ISO 1183"),
      property("tensile_modulus", exact(4000.0, "MPa", "nominal"), 23.0, {}, "ISO 527-1"),
      property("tensile_stress_yield", exact(98.0, "MPa", "nominal"), 23.0, {}, "ISO 527-2"),
      property("flexural_modulus", exact(3800.0, "MPa", "nominal"), 23.0, {}, "ISO 178")},
     {{"granules", "natural", "supplier grade", "injection moulding and extrusion", {}}},
     {source("Victrex", "VICTREX PEEK Polymer 450G datasheet", "450G", "2026-03-17",
             "https://www.victrex.com/-/media/downloads/datasheets/victrex_tds_450g.pdf",
             "Material properties")},
     "Supplier publishes polymer identity and test properties, not a complete additive formulation."},

    {"COVESTRO_MAKROLON_2407", "Covestro Makrolon 2407", "polymer",
     "unreinforced_thermoplastic", "polycarbonate 2407", "injection moulding grade",
     {"MAKROLON2407", "PC2407"}, "PLASTIC", "proprietary_formulation_not_disclosed", {},
     {property("density", exact(1200.0, "kg/m3", "nominal"), 23.0, {}, "ISO 1183-1"),
      property("tensile_modulus", exact(2400.0, "MPa", "nominal"), 23.0, {},
               "ISO 527-1/-2"),
      property("yield_stress", exact(62.0, "MPa", "nominal"), 23.0, {}, "ISO 527-1/-2")},
     {{"pellets", "supplier grade", "supplier grade", "injection moulding", {}}},
     {source("Covestro", "Makrolon 2407", "2407", "live product page",
             "https://solutions.covestro.com/en/products/makrolon/makrolon-2407_000000000086286874",
             "physical and mechanical property tables")},
     "Values are datasheet selection values; colorant and lot can affect properties."},

    {"ROECHLING_SUSTARIN_C_NATURAL", "Roechling Sustarin C natural", "polymer",
     "engineering_thermoplastic", "POM-C", "natural semi-finished stock", {"POM_C", "SUSTARIN_C"},
     "PLASTIC", "proprietary_formulation_not_disclosed", {},
     {property("density", exact(1.41, "g/cm3", "guideline"), 23.0, {}, "DIN EN ISO 1183-1"),
      property("yield_stress", exact(67.0, "MPa", "guideline"), 23.0, {}, "DIN EN ISO 527"),
      property("tensile_modulus", exact(2800.0, "MPa", "guideline"), 23.0, {},
               "DIN EN ISO 527"),
      property("service_temperature_long_term", value(-50.0, {}, 100.0, "degC", "guideline"))},
     {{"semi_finished_stock", "natural", "supplier grade", "supplier availability", {}}},
     {source("Roechling Industrial", "Sustarin C natural Technical Data Sheet", "591054",
             "4.0 / 2025-04-14",
             "https://www.roechling.com/fileadmin/assets/Technical%20Data%20Sheet%20Sustarin%C2%AE%"
             "20C%20natural%20591054%20EN.pdf",
             "page 1")},
     "The datasheet labels these properties guideline values, not guaranteed acceptance limits."},

    {"WACKER_ELASTOSIL_R_531_60_E2003", "Wacker ELASTOSIL R 531/60 E/2003", "polymer",
     "silicone_elastomer", "VMQ silicone rubber", "press cured 10 min at 135 C",
     {"ELASTOSIL_R531_60", "VMQ_60A"}, "RUBBER", "proprietary_formulation_not_disclosed", {},
     {property("density", exact(1.36, "g/cm3", "guideline"), 23.0, "cured",
               "DIN EN ISO 1183-1 A"),
      property("hardness", exact(61.0, "Shore A", "guideline"), 23.0, "cured", "DIN ISO 48-4"),
      property("tensile_strength", exact(6.5, "MPa", "guideline"), 23.0, "cured",
               "ISO 37 type 1"),
      property("elongation_at_break", exact(400.0, "percent", "guideline"), 23.0, "cured",
               "ISO 37 type 1")},
     {{"uncured_compound", "E/2003", "supplier grade", "compression moulding", {}}},
     {source("Wacker Chemie", "ELASTOSIL R 531/60 E/2003 technical data", "R53160-E2003",
             "2024-05-06 v13",
             "https://www.wacker.com/h/en-gb/medias/ELASTOSIL-R-53160-E2003-en-2024.05.06-v13.pdf",
             "Technical data")},
     "Supplier states all figures are guidance and require buyer trials."},

    {"TORAYCA_T700S_12K", "Torayca T700S 12K carbon fibre", "composite_constituent",
     "carbon_fibre_reinforcement", "T700S", "12K tow", {"T700S", "T700S_12K"},
     "CARBON_FIBER", "partially_disclosed",
     {composition("Carbon", 93.0, {}, {}),
      {"Na+K", value({}, {}, 0.005, "mass_percent", "maximum_converted_from_50_ppm"), false, {}}},
     {property("fibre_tensile_strength", exact(4900.0, "MPa", "typical"), 23.0, "fibre",
               "TY-030B-01"),
      property("fibre_tensile_modulus", exact(230.0, "GPa", "typical"), 23.0, "fibre",
               "TY-030B-01"),
      property("fibre_density", exact(1.80, "g/cm3", "typical"), 23.0, "fibre", "TY-030B-02"),
      property("linear_yield", exact(800.0, "g/1000m", "typical"), {}, "12K tow",
               "TY-030B-03")},
     {{"continuous_tow", "12K", "supplier grade", "weaving/braiding/winding/prepreg", {}}},
     {source("Toray Composite Materials America", "TORAYCA T700S Standard Modulus data sheet",
             "T700S", "current 2026",
             "https://www.toraycma.com/wp-content/uploads/T700S-Data-Sheet.pdf",
             "Fibre properties and chemical composition")},
     "This is fibre constituent data. It is not a cured-laminate allowables profile."},

    {"COORSTEK_ALUMINA_99_5", "CoorsTek 99.5 percent alumina", "ceramic", "oxide_ceramic",
     "Alumina 99.5", "fired", {"ALUMINA995", "AL2O3_995"}, "CERAMIC", "minimum_purity_only",
     {composition("Al2O3", 99.5, {}, {})}, {},
     {{"custom_component", "fired", "supplier material family", "supplier capabilities", {}}},
     {source("CoorsTek", "Alumina material properties", "ALUMINA", "live product page",
             "https://www2.coorstek.com/en/materials/alumina/", "99.5 percent Al2O3 row")},
     "Remaining constituents and grade-specific property columns are not disclosed in this profile."},

    {"SCHOTT_BOROFLOAT_33", "SCHOTT BOROFLOAT 33", "glass", "borosilicate_glass",
     "BOROFLOAT 33", "float glass", {"BOROFLOAT33"}, "GLASS",
     "proprietary_formulation_not_disclosed", {},
     {property("density", exact(2.23, "g/cm3", "nominal"), 25.0),
      property("youngs_modulus", exact(64.0, "GPa", "nominal"), 23.0, {}, "DIN 13316"),
      property("poisson_ratio", exact(0.20, "ratio", "nominal"), 23.0, {}, "DIN 13316"),
      property("thermal_expansion_20_300C", exact(3.25, "1e-6/K", "nominal"), {}, {},
               "ISO 7991"),
      property("thermal_conductivity", exact(1.2, "W/(m*K)", "nominal"), 90.0)},
     {{"float_sheet", "as supplied", "supplier grade", "supplier thickness availability", {}}},
     {source("SCHOTT", "BOROFLOAT 33 Technical Data", "BOROFLOAT-33", "2018",
             "https://media.schott.com/api/public/content/69b4abd8191246e3869c46c717f07b29?"
             "download=true&v=3b812f89",
             "thermal and mechanical properties")},
     "Bending strength is installation and surface-condition dependent; no design allowable is set."},
};

[[nodiscard]] auto normalized(std::string_view text) -> std::string {
    std::string result;
    result.reserve(text.size());
    for (const char raw_character : text) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isalnum(character) != 0)
            result.push_back(static_cast<char>(std::toupper(character)));
        else if (!result.empty() && result.back() != '_')
            result.push_back('_');
    }
    while (!result.empty() && result.back() == '_')
        result.pop_back();
    return result;
}

[[nodiscard]] auto json_number(const Optional& number) -> json::Value {
    return number ? json::Value{*number} : json::Value{nullptr};
}

[[nodiscard]] auto numeric_json(const NumericValue& number) -> json::Value {
    return json::Value{json::Value::Object{{"minimum", json_number(number.minimum)},
                                           {"typical", json_number(number.typical)},
                                           {"maximum", json_number(number.maximum)},
                                           {"unit", number.unit},
                                           {"qualifier", number.qualifier}}};
}

[[nodiscard]] auto profile_json(const Profile& profile) -> json::Value {
    json::Value::Array aliases;
    for (const auto& alias : profile.aliases)
        aliases.emplace_back(alias);
    json::Value::Array composition_entries;
    for (const auto& entry : profile.composition) {
        composition_entries.emplace_back(json::Value::Object{
            {"constituent", entry.constituent}, {"massPercent", numeric_json(entry.mass_percent)},
            {"balance", entry.balance}, {"note", entry.note}});
    }
    json::Value::Array properties;
    for (const auto& entry : profile.properties) {
        properties.emplace_back(json::Value::Object{
            {"name", entry.name}, {"value", numeric_json(entry.value)},
            {"temperatureC", json_number(entry.temperature_c)}, {"condition", entry.condition},
            {"testMethod", entry.test_method}});
    }
    json::Value::Array forms;
    for (const auto& form : profile.product_forms) {
        forms.emplace_back(json::Value::Object{{"form", form.form}, {"condition", form.condition},
                                               {"specification", form.specification},
                                               {"sizeScope", form.size_scope},
                                               {"note", form.note}});
    }
    json::Value::Array sources;
    for (const auto& entry : profile.sources) {
        sources.emplace_back(json::Value::Object{
            {"publisher", entry.publisher}, {"title", entry.title},
            {"documentId", entry.document_id}, {"revision", entry.revision},
            {"url", entry.url}, {"accessedOn", entry.accessed_on},
            {"location", entry.location}, {"sourceType", entry.source_type}});
    }
    return json::Value{json::Value::Object{
        {"id", profile.id}, {"displayName", profile.display_name},
        {"class", profile.material_class}, {"subclass", profile.material_subclass},
        {"grade", profile.grade}, {"condition", profile.condition},
        {"aliases", json::Value{std::move(aliases)}}, {"appearancePreset", profile.appearance_preset},
        {"compositionDisclosure", profile.composition_disclosure},
        {"composition", json::Value{std::move(composition_entries)}},
        {"properties", json::Value{std::move(properties)}},
        {"productForms", json::Value{std::move(forms)}}, {"sources", json::Value{std::move(sources)}},
        {"limitations", profile.limitations}}};
}

} // namespace

auto find_profile(std::string_view id_or_alias) -> const Profile* {
    const auto key = normalized(id_or_alias);
    for (const auto& profile : profiles) {
        if (normalized(profile.id) == key)
            return &profile;
        if (std::ranges::any_of(profile.aliases,
                                [&](const auto& alias) { return normalized(alias) == key; }))
            return &profile;
    }
    return nullptr;
}

auto all_profiles() -> std::span<const Profile> { return profiles; }

auto profile_classes() -> std::vector<std::string_view> {
    std::set<std::string_view> classes;
    for (const auto& profile : profiles)
        classes.insert(profile.material_class);
    return {classes.begin(), classes.end()};
}

auto profile_subclasses() -> std::vector<std::string_view> {
    std::set<std::string_view> subclasses;
    for (const auto& profile : profiles)
        subclasses.insert(profile.material_subclass);
    return {subclasses.begin(), subclasses.end()};
}

auto catalog_json(std::optional<std::string_view> material_class,
                  std::optional<std::string_view> id) -> std::string {
    json::Value::Array records;
    for (const auto& profile : profiles) {
        if (material_class && normalized(profile.material_class) != normalized(*material_class) &&
            normalized(profile.material_subclass) != normalized(*material_class))
            continue;
        if (id && find_profile(*id) != &profile)
            continue;
        records.emplace_back(profile_json(profile));
    }
    json::Value::Array classes;
    for (const auto entry : profile_classes())
        classes.emplace_back(std::string{entry});
    json::Value::Array subclasses;
    for (const auto entry : profile_subclasses())
        subclasses.emplace_back(std::string{entry});
    return json::serialize(json::Value{json::Value::Object{
        {"schema", "icad.material.catalog.v1"}, {"catalogRevision", std::string{catalog_revision()}},
        {"coverage", "curated_supplier_profiles_not_exhaustive"},
        {"classCount", static_cast<double>(classes.size())},
        {"subclassCount", static_cast<double>(subclasses.size())},
        {"profileCount", static_cast<double>(records.size())},
        {"classes", json::Value{std::move(classes)}},
        {"subclasses", json::Value{std::move(subclasses)}},
        {"profiles", json::Value{std::move(records)}}}});
}

auto catalog_revision() noexcept -> std::string_view { return "2026-09-07.5"; }

} // namespace icad::materials
