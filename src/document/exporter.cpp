#include "icad/document/exporter.hpp"

#include "icad/cad/analysis.hpp"
#include "icad/materials/library.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace icad::document {
namespace {

[[nodiscard]] auto json_string(std::string_view value) -> std::string {
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

struct LocalizedText {
    std::string_view en;
    std::string_view fr;
};

[[nodiscard]] auto component_description(std::string_view name) -> LocalizedText {
    const auto contains = [name](std::string_view token) { return name.find(token) != name.npos; };
    if (contains("foundation") || contains("footing")) return {"Concrete foundation", "Fondation en béton"};
    if (contains("base_plate")) return {"Column base plate", "Platine de pied de poteau"};
    if (contains("column")) return {"Structural column", "Poteau de charpente"};
    if (contains("rafter")) return {"Roof rafter", "Arbalétrier de toiture"};
    if (contains("roof_panel")) return {"Profiled steel roofing sheet", "Bac acier profilé de couverture"};
    if (contains("wall_cladding")) return {"Profiled steel wall cladding sheet", "Tôle d'acier profilée de bardage"};
    if (contains("purlin")) return {"Roof purlin", "Panne de toiture"};
    if (contains("eave_beam")) return {"Eave beam", "Poutre de rive"};
    if (contains("ridge_beam")) return {"Ridge beam", "Poutre faîtière"};
    if (contains("brace")) return {"Structural bracing member", "Élément de contreventement"};
    if (contains("anchor")) return {"Anchor rod assembly", "Ensemble de tige d'ancrage"};
    if (contains("bolt") || contains("fastener")) return {"Fastener model", "Modèle de fixation"};
    if (contains("shaft")) return {"Machined shaft", "Arbre usiné"};
    if (contains("blade")) return {"Turbomachinery blade", "Aube de turbomachine"};
    if (contains("vane") || contains("stator")) return {"Stator vane", "Aube statorique"};
    if (contains("bearing")) return {"Bearing component", "Composant de palier"};
    if (contains("liner")) return {"Combustor liner", "Chemise de chambre de combustion"};
    if (contains("injector")) return {"Fuel injector component", "Composant d'injecteur de carburant"};
    if (contains("casing")) return {"Engine casing component", "Composant de carter moteur"};
    if (contains("flange")) return {"Mounting flange", "Bride de montage"};
    if (contains("bracket") || contains("mount")) return {"Mounting bracket", "Support de montage"};
    if (contains("cover")) return {"Inspection cover", "Trappe de visite"};
    if (contains("disk")) return {"Rotor disk", "Disque de rotor"};
    if (contains("spacer")) return {"Axial spacer", "Entretoise axiale"};
    if (contains("cone")) return {"Engine cone", "Cône moteur"};
    if (contains("nozzle")) return {"Engine nozzle", "Tuyère moteur"};
    return {"Manufactured component", "Composant fabriqué"};
}

[[nodiscard]] auto profile_numeric_property(const materials::Profile* profile,
                                            std::string_view name,
                                            std::string_view unit) -> std::optional<double> {
    if (profile == nullptr)
        return std::nullopt;
    const auto found = std::ranges::find(profile->properties, name, &materials::Property::name);
    if (found == profile->properties.end() || found->value.unit != unit)
        return std::nullopt;
    if (found->value.typical)
        return found->value.typical;
    if (found->value.minimum && found->value.maximum)
        return (*found->value.minimum + *found->value.maximum) * 0.5;
    return found->value.minimum ? found->value.minimum : found->value.maximum;
}

struct FastenerSpecification {
    std::optional<double> nominal_diameter_mm;
    std::optional<double> length_mm;
    std::string property_class;
};

[[nodiscard]] auto parse_fastener_specification(std::string_view model)
    -> FastenerSpecification {
    FastenerSpecification result;
    if (model.empty() || model.front() != 'M')
        return result;
    std::size_t cursor = 1;
    while (cursor < model.size() && std::isdigit(static_cast<unsigned char>(model[cursor])))
        ++cursor;
    if (cursor == 1)
        return result;
    result.nominal_diameter_mm = std::stod(std::string{model.substr(1, cursor - 1)});

    if (cursor < model.size() && (model[cursor] == 'x' || model[cursor] == 'X')) {
        const auto length_begin = ++cursor;
        while (cursor < model.size() &&
               (std::isdigit(static_cast<unsigned char>(model[cursor])) || model[cursor] == '.'))
            ++cursor;
        if (cursor > length_begin)
            result.length_mm = std::stod(std::string{model.substr(length_begin, cursor - length_begin)});
    }

    constexpr std::string_view class_marker{"_CLASS_"};
    const auto class_position = model.find(class_marker);
    if (class_position != model.npos) {
        result.property_class = std::string{model.substr(class_position + class_marker.size())};
        std::ranges::replace(result.property_class, '_', '.');
    }
    return result;
}

auto append_string_array(std::ostringstream& stream, const std::vector<std::string_view>& values)
    -> void {
    stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            stream << ',';
        stream << json_string(values[index]);
    }
    stream << ']';
}

} // namespace

auto bom_json(const compiler::ir::Project& project) -> std::string {
    const auto analysis = cad::analyze(project);
    std::map<std::string, std::pair<std::size_t, double>> body_metrics;
    for (const auto& part : analysis.parts) {
        auto& metrics = body_metrics[part.body];
        ++metrics.first;
        metrics.second += part.volume_mm3;
    }
    std::ostringstream stream;
    const auto material_for = [&](std::string_view symbol) -> const compiler::ir::Material* {
        const auto found = std::ranges::find(project.materials, symbol,
                                             &compiler::ir::Material::name);
        return found == project.materials.end() ? nullptr : &*found;
    };
    double partial_calculated_mass_kg = 0.0;
    std::size_t mass_line_items = 0;
    stream << std::setprecision(17)
           << "{\"schema\":\"icad.bom.v2\",\"project\":" << json_string(project.name)
           << ",\"languages\":[\"en\",\"fr\"],\"documentTitle\":{"
              "\"en\":\"Calculated bill of materials\","
              "\"fr\":\"Nomenclature calculée\"},\"calculationBasis\":{"
              "\"componentQuantities\":\"assembly_occurrences\","
              "\"fastenerQuantities\":\"declared_connection_quantity\","
              "\"weldConsumables\":\"triangular_fillet_volume_divided_by_declared_deposition_"
              "efficiency_and_rod_volume\","
              "\"status\":{\"en\":\"Calculated estimate; manufacturing review required\","
              "\"fr\":\"Estimation calculée ; revue de fabrication requise\"}},"
              "\"components\":[";
    const std::size_t component_count = project.bodies.size() + project.instances.size();
    for (std::size_t index = 0; index < project.bodies.size(); ++index) {
        const auto& body = project.bodies[index];
        std::vector<std::string_view> occurrences{body.name};
        for (const auto& instance : project.instances) {
            if (instance.body == body.name)
                occurrences.push_back(instance.name);
        }
        std::size_t solid_count = 0;
        double total_volume_mm3 = 0.0;
        for (const auto occurrence : occurrences) {
            const auto metrics = body_metrics[std::string{occurrence}];
            solid_count += metrics.first;
            total_volume_mm3 += metrics.second;
        }
        if (index != 0) {
            stream << ',';
        }
        const auto* material = material_for(body.material);
        const auto description = component_description(body.name);
        const auto* profile = material == nullptr ? nullptr : materials::find_profile(material->profile);
        const auto density_g_cm3 = profile_numeric_property(profile, "density", "g/cm3");
        const auto density_kg_m3 = density_g_cm3 ? std::optional<double>{*density_g_cm3 * 1000.0}
                                                 : profile_numeric_property(profile, "density", "kg/m3");
        const auto nominal_thickness_mm =
            profile_numeric_property(profile, "nominal_thickness", "mm");
        const auto mass_per_covered_area =
            profile_numeric_property(profile, "mass_per_covered_area", "kg/m2");
        const auto calculated_area_m2 = nominal_thickness_mm
                                            ? std::optional<double>{total_volume_mm3 /
                                                                    *nominal_thickness_mm * 1e-6}
                                            : std::nullopt;
        const auto calculated_mass_kg = mass_per_covered_area && calculated_area_m2
                                            ? std::optional<double>{*mass_per_covered_area *
                                                                    *calculated_area_m2}
                                            : density_kg_m3
                                                  ? std::optional<double>{total_volume_mm3 *
                                                                          *density_kg_m3 * 1e-9}
                                                  : std::nullopt;
        if (calculated_mass_kg) {
            partial_calculated_mass_kg += *calculated_mass_kg;
            ++mass_line_items;
        }
        stream << "{\"item\":" << index + 1 << ",\"type\":\"manufactured_component\""
               << ",\"description\":{\"en\":" << json_string(description.en)
               << ",\"fr\":" << json_string(description.fr) << '}'
               << ",\"body\":" << json_string(body.name)
               << ",\"definition\":" << json_string(body.name)
               << ",\"occurrences\":[";
        for (std::size_t occurrence_index = 0; occurrence_index < occurrences.size();
             ++occurrence_index) {
            if (occurrence_index != 0)
                stream << ',';
            stream << json_string(occurrences[occurrence_index]);
        }
        stream << "]"
               << ",\"material\":" << json_string(body.material)
               << ",\"quantity\":" << occurrences.size()
               << ",\"materialProfile\":"
               << json_string(material == nullptr ? std::string_view{} : material->profile)
               << ",\"solidCount\":" << solid_count
               << ",\"unitVolumeMm3\":"
               << total_volume_mm3 / static_cast<double>(occurrences.size())
               << ",\"totalVolumeMm3\":" << total_volume_mm3
               << ",\"volumeMm3\":" << total_volume_mm3;
        stream << ",\"densityKgM3\":";
        if (density_kg_m3)
            stream << *density_kg_m3;
        else
            stream << "null";
        stream << ",\"calculatedMassKg\":";
        if (calculated_mass_kg)
            stream << *calculated_mass_kg;
        else
            stream << "null";
        stream << ",\"massCalculationMethod\":"
               << json_string(mass_per_covered_area && calculated_area_m2
                                  ? "supplier_mass_per_covered_area"
                                  : density_kg_m3 ? "density_times_solid_volume"
                                                  : "unavailable");
        if (nominal_thickness_mm && calculated_area_m2) {
            stream << ",\"nominalSheetThicknessMm\":" << *nominal_thickness_mm
                   << ",\"calculatedSheetAreaM2\":" << *calculated_area_m2;
            if (mass_per_covered_area)
                stream << ",\"supplierMassPerCoveredAreaKgM2\":" << *mass_per_covered_area;
        }
        stream << '}';
    }
    struct FastenerTotal {
        std::size_t quantity{};
        bool all_explicit{true};
    };
    std::map<std::pair<std::string, std::string>, FastenerTotal> fasteners;
    for (const auto& connection : project.connections) {
        if (!connection.fastener.empty()) {
            auto& total = fasteners[{connection.standard, connection.fastener}];
            total.quantity += connection.quantity;
            total.all_explicit = total.all_explicit && connection.quantity_explicit;
        }
    }
    stream << "],\"fasteners\":[";
    std::size_t fastener_index = 0;
    bool all_fasteners_procurement_ready = true;
    for (const auto& [identity, total] : fasteners) {
        if (fastener_index++ != 0)
            stream << ',';
        const bool hex_head = identity.first == "ISO_4017";
        const auto specification = parse_fastener_specification(identity.second);
        std::vector<std::string_view> missing_fields;
        if (!specification.length_mm)
            missing_fields.push_back("length");
        if (specification.property_class.empty())
            missing_fields.push_back("property_class");
        missing_fields.insert(missing_fields.end(), {"finish", "nut", "washer"});
        const bool procurement_ready = total.all_explicit && missing_fields.empty();
        all_fasteners_procurement_ready = all_fasteners_procurement_ready && procurement_ready;
        stream << "{\"item\":" << fastener_index << ",\"type\":\"purchased_fastener\""
               << ",\"description\":{\"en\":"
               << json_string(hex_head ? "Hexagon head bolt" : "Assembly fastener")
               << ",\"fr\":"
               << json_string(hex_head ? "Vis à tête hexagonale" : "Élément de fixation")
               << "},\"standard\":" << json_string(identity.first)
               << ",\"model\":" << json_string(identity.second)
               << ",\"quantity\":" << total.quantity
               << ",\"quantitySource\":"
               << json_string(total.all_explicit ? "explicit_connection_quantity"
                                                 : "default_one_for_unspecified_connection")
               << ",\"quantityReady\":" << (total.all_explicit ? "true" : "false")
               << ",\"nominalDiameterMm\":";
        if (specification.nominal_diameter_mm)
            stream << *specification.nominal_diameter_mm;
        else
            stream << "null";
        stream << ",\"lengthMm\":";
        if (specification.length_mm)
            stream << *specification.length_mm;
        else
            stream << "null";
        stream << ",\"propertyClass\":"
               << (specification.property_class.empty() ? "null"
                                                        : json_string(specification.property_class))
               << ",\"procurementReady\":" << (procurement_ready ? "true" : "false")
               << ",\"missingProcurementFields\":";
        append_string_array(stream, missing_fields);
        stream << '}';
    }
    stream << "],\"connections\":[";
    for (std::size_t index = 0; index < project.connections.size(); ++index) {
        if (index != 0)
            stream << ',';
        const auto& connection = project.connections[index];
        const auto method_fr = [&]() -> std::string_view {
            if (connection.method == "BOLTED") return "boulonné";
            if (connection.method == "SCREWED") return "vissé";
            if (connection.method == "PINNED") return "goupillé";
            if (connection.method == "PRESS_FIT") return "emmanchement serré";
            if (connection.method == "SLIP_FIT") return "ajustement glissant";
            if (connection.method == "BEARING") return "palier à roulement";
            if (connection.method == "WELDED") return "soudé";
            if (connection.method == "BRAZED") return "brasé";
            if (connection.method == "BONDED") return "collé";
            return "non traduit";
        }();
        stream << "{\"item\":" << index + 1 << ",\"name\":" << json_string(connection.name)
               << ",\"description\":{\"en\":" << json_string(connection.method)
               << ",\"fr\":" << json_string(method_fr) << "},\"method\":"
               << json_string(connection.method) << ",\"standard\":"
               << json_string(connection.standard) << ",\"fastenerModel\":"
               << json_string(connection.fastener) << ",\"quantity\":" << connection.quantity
               << ",\"quantityExplicit\":"
               << (connection.quantity_explicit ? "true" : "false")
               << ",\"fit\":" << json_string(connection.fit)
               << ",\"clearanceMm\":" << connection.clearance_mm << '}';
    }
    stream << "],\"weldConsumables\":[";
    std::size_t weld_index = 0;
    std::size_t total_rods = 0;
    double total_filler_mass_kg = 0.0;
    std::size_t total_filler_packages = 0;
    bool weld_mass_complete = true;
    for (const auto& connection : project.connections) {
        if (connection.method != "WELDED")
            continue;
        if (weld_index++ != 0)
            stream << ',';
        const double deposited_volume = 0.5 * connection.weld_size_mm * connection.weld_size_mm *
                                        connection.weld_length_mm *
                                        static_cast<double>(connection.quantity);
        const double required_filler_volume = deposited_volume / connection.deposition_efficiency;
        const double rod_volume = std::numbers::pi * connection.filler_diameter_mm *
                                  connection.filler_diameter_mm * 0.25 * connection.stock_length_mm;
        const auto rod_count = static_cast<std::size_t>(std::ceil(required_filler_volume / rod_volume));
        total_rods += rod_count;
        const auto* filler_profile = materials::find_profile(connection.filler);
        const auto filler_density_g_cm3 =
            profile_numeric_property(filler_profile, "density", "g/cm3");
        const auto filler_density_kg_m3 =
            filler_density_g_cm3
                ? std::optional<double>{*filler_density_g_cm3 * 1000.0}
                : profile_numeric_property(filler_profile, "density", "kg/m3");
        const auto package_mass_kg =
            profile_numeric_property(filler_profile, "package_mass", "kg");
        const auto required_filler_mass_kg =
            filler_density_kg_m3
                ? std::optional<double>{required_filler_volume * *filler_density_kg_m3 * 1e-9}
                : std::nullopt;
        const auto package_quantity = required_filler_mass_kg && package_mass_kg
                                          ? std::optional<std::size_t>{static_cast<std::size_t>(
                                                std::ceil(*required_filler_mass_kg /
                                                          *package_mass_kg))}
                                          : std::nullopt;
        weld_mass_complete = weld_mass_complete && required_filler_mass_kg.has_value() &&
                             package_quantity.has_value();
        if (required_filler_mass_kg)
            total_filler_mass_kg += *required_filler_mass_kg;
        if (package_quantity)
            total_filler_packages += *package_quantity;
        stream << "{\"item\":" << weld_index << ",\"type\":\"welding_consumable\""
               << ",\"description\":{\"en\":\"Welding filler rod\","
                  "\"fr\":\"Baguette de soudage\"}"
               << ",\"connection\":" << json_string(connection.name)
               << ",\"process\":" << json_string(connection.weld_process)
               << ",\"model\":" << json_string(connection.filler)
               << ",\"weldSizeMm\":" << connection.weld_size_mm
               << ",\"weldLengthMm\":" << connection.weld_length_mm
               << ",\"jointQuantity\":" << connection.quantity
               << ",\"depositionEfficiency\":" << connection.deposition_efficiency
               << ",\"depositedVolumeMm3\":" << deposited_volume
               << ",\"requiredFillerVolumeMm3\":" << required_filler_volume
               << ",\"rodDiameterMm\":" << connection.filler_diameter_mm
               << ",\"rodStockLengthMm\":" << connection.stock_length_mm
               << ",\"calculatedRodQuantity\":" << rod_count
               << ",\"fillerDensityKgM3\":";
        if (filler_density_kg_m3)
            stream << *filler_density_kg_m3;
        else
            stream << "null";
        stream << ",\"requiredFillerMassKg\":";
        if (required_filler_mass_kg)
            stream << *required_filler_mass_kg;
        else
            stream << "null";
        stream << ",\"supplierPackageMassKg\":";
        if (package_mass_kg)
            stream << *package_mass_kg;
        else
            stream << "null";
        stream << ",\"calculatedPackageQuantity\":";
        if (package_quantity)
            stream << *package_quantity;
        else
            stream << "null";
        stream
               << ",\"calculationModel\":\"equal_leg_triangular_fillet\"}";
    }
    std::size_t fastener_quantity = 0;
    bool connection_quantities_complete = true;
    for (const auto& [identity, total] : fasteners) {
        static_cast<void>(identity);
        fastener_quantity += total.quantity;
        connection_quantities_complete = connection_quantities_complete && total.all_explicit;
    }
    const bool mass_complete = mass_line_items == project.bodies.size();
    const bool has_envelope_sheet = std::ranges::any_of(project.bodies, [](const auto& body) {
        return body.name.find("roof_panel") != std::string::npos ||
               body.name.find("wall_cladding") != std::string::npos;
    });
    stream << "],\"procurementLimitations\":["
              "{\"en\":\"Fastener procurement requires every missing length, property class, "
              "finish, nut and washer specification to be closed.\","
              "\"fr\":\"L'approvisionnement des fixations exige de renseigner toutes les "
              "longueurs, classes de qualité, finitions, écrous et rondelles manquants.\"},"
              "{\"en\":\"Calculated masses use the selected catalog profile and retain its "
              "supplier qualifiers; null means that no defensible density was available.\","
              "\"fr\":\"Les masses calculées utilisent le profil de catalogue sélectionné et "
              "conservent ses réserves fournisseur ; null signifie qu'aucune masse volumique "
              "défendable n'était disponible.\"}";
    if (has_envelope_sheet) {
        stream << ",{\"en\":\"Roof and wall sheet fixings, flashings, openings, sealants, gutters "
                  "and drainage remain unquantified until the architectural envelope schedule is "
                  "approved.\",\"fr\":\"Les fixations de couverture et de bardage, solins, "
                  "ouvertures, mastics, gouttières et évacuations restent non quantifiés jusqu'à "
                  "l'approbation du calepinage architectural.\"}";
    }
    stream << "],\"summary\":{\"componentOccurrences\":" << component_count
           << ",\"componentLineItems\":" << project.bodies.size()
           << ",\"partialCalculatedMassKg\":" << partial_calculated_mass_kg
           << ",\"massLineItems\":" << mass_line_items
           << ",\"massComplete\":" << (mass_complete ? "true" : "false")
           << ",\"connectionCount\":" << project.connections.size()
           << ",\"fastenerQuantity\":" << fastener_quantity
           << ",\"weldRodQuantity\":" << total_rods
           << ",\"weldFillerMassKg\":" << total_filler_mass_kg
           << ",\"weldPackageQuantity\":" << total_filler_packages
           << ",\"weldMassComplete\":" << (weld_mass_complete ? "true" : "false")
           << ",\"connectionQuantitiesComplete\":"
           << (connection_quantities_complete ? "true" : "false")
           << ",\"procurementReady\":"
           << (mass_complete && all_fasteners_procurement_ready && weld_mass_complete &&
                       !has_envelope_sheet
                   ? "true"
                   : "false")
           << ",\"status\":{\"en\":\"Calculated BOM generated\","
              "\"fr\":\"Nomenclature calculée générée\"}"
           << "}}";
    return stream.str();
}

auto write_bom(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    std::ofstream stream{output, std::ios::binary};
    if (!stream)
        return {false, "cannot open BOM output"};
    stream << bom_json(project);
    return {static_cast<bool>(stream), "BOM export complete"};
}

} // namespace icad::document
