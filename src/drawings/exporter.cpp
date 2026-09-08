#include "icad/drawings/exporter.hpp"

#include "icad/cad/analysis.hpp"
#include "../cad/model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace icad::drawings {
namespace {

constexpr double sheet_width = 1600.0;
constexpr double sheet_height = 1200.0;

[[nodiscard]] auto xml(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

[[nodiscard]] auto number(double value, int precision = 3) -> std::string {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    auto result = output.str();
    while (result.contains('.') && result.ends_with('0'))
        result.pop_back();
    if (result.ends_with('.'))
        result.pop_back();
    return result;
}

[[nodiscard]] auto coordinate(const cad::Point3& point, std::size_t axis) -> double {
    if (axis == 0)
        return point.x;
    if (axis == 1)
        return point.y;
    return point.z;
}

[[nodiscard]] auto triangle_normal(const cad::Point3& first, const cad::Point3& second,
                                   const cad::Point3& third) -> std::array<double, 3> {
    const std::array first_edge{second.x - first.x, second.y - first.y, second.z - first.z};
    const std::array second_edge{third.x - first.x, third.y - first.y, third.z - first.z};
    std::array normal{first_edge[1] * second_edge[2] - first_edge[2] * second_edge[1],
                      first_edge[2] * second_edge[0] - first_edge[0] * second_edge[2],
                      first_edge[0] * second_edge[1] - first_edge[1] * second_edge[0]};
    const double length = std::hypot(normal[0], normal[1], normal[2]);
    if (length > 1.0e-12) {
        for (auto& component : normal)
            component /= length;
    }
    return normal;
}

[[nodiscard]] auto drawing_bounds(const std::vector<const cad::Part*>& parts) -> cad::Bounds {
    const double high = std::numeric_limits<double>::max();
    const double low = std::numeric_limits<double>::lowest();
    cad::Bounds bounds{{high, high, high}, {low, low, low}};
    for (const auto* part : parts) {
        for (const auto& vertex : part->vertices) {
            const std::array values{vertex.x, vertex.y, vertex.z};
            for (std::size_t axis = 0; axis < values.size(); ++axis) {
                bounds.minimum[axis] = std::min(bounds.minimum[axis], values[axis]);
                bounds.maximum[axis] = std::max(bounds.maximum[axis], values[axis]);
            }
        }
    }
    if (parts.empty() || bounds.minimum[0] == high)
        return {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    return bounds;
}

auto projected_view(std::ostream& stream, const std::vector<const cad::Part*>& parts,
                    const cad::Bounds& bounds, std::size_t horizontal_axis,
                    std::size_t vertical_axis, double x, double y, double width, double height,
                    std::string_view label) -> void {
    const double horizontal_extent =
        std::max(bounds.maximum[horizontal_axis] - bounds.minimum[horizontal_axis], 0.001);
    const double vertical_extent =
        std::max(bounds.maximum[vertical_axis] - bounds.minimum[vertical_axis], 0.001);
    const double scale = std::min((width - 34.0) / horizontal_extent,
                                  (height - 52.0) / vertical_extent);
    const double horizontal_center =
        (bounds.minimum[horizontal_axis] + bounds.maximum[horizontal_axis]) * 0.5;
    const double vertical_center =
        (bounds.minimum[vertical_axis] + bounds.maximum[vertical_axis]) * 0.5;
    const double center_x = x + width * 0.5;
    const double center_y = y + height * 0.5 + 8.0;
    const std::size_t view_axis = 3U - horizontal_axis - vertical_axis;

    stream << "<g class=\"view\"><rect x=\"" << x << "\" y=\"" << y
           << "\" width=\"" << width << "\" height=\"" << height
           << "\"/><text class=\"view-label\" x=\"" << x + 12.0 << "\" y=\""
           << y + 22.0 << "\">" << label << "</text>\n";
    stream << "<line class=\"centerline\" x1=\"" << x + 8.0 << "\" y1=\"" << center_y
           << "\" x2=\"" << x + width - 8.0 << "\" y2=\"" << center_y
           << "\"/><line class=\"centerline\" x1=\"" << center_x << "\" y1=\"" << y + 30.0
           << "\" x2=\"" << center_x << "\" y2=\"" << y + height - 8.0 << "\"/>\n";
    for (const auto* part : parts) {
        stream << "<path data-part=\"" << xml(part->name) << "\" d=\"";
        std::map<std::pair<std::size_t, std::size_t>, std::vector<std::array<double, 3>>> edges;
        for (const auto& triangle : part->triangles) {
            const auto normal = triangle_normal(part->vertices[triangle[0]],
                                                part->vertices[triangle[1]],
                                                part->vertices[triangle[2]]);
            for (std::size_t edge = 0; edge < 3; ++edge) {
                edges[{std::min(triangle[edge], triangle[(edge + 1) % 3]),
                       std::max(triangle[edge], triangle[(edge + 1) % 3])}]
                    .push_back(normal);
            }
        }
        for (const auto& [edge, normals] : edges) {
            bool draw = normals.size() != 2U;
            if (normals.size() == 2U) {
                const double normal_dot = normals[0][0] * normals[1][0] +
                                          normals[0][1] * normals[1][1] +
                                          normals[0][2] * normals[1][2];
                constexpr double crease_cosine = 0.9659258262890683; // 15 degrees
                constexpr double facing_epsilon = 1.0e-6;
                const double first_facing = normals[0][view_axis];
                const double second_facing = normals[1][view_axis];
                const bool silhouette =
                    (first_facing > facing_epsilon && second_facing < -facing_epsilon) ||
                    (first_facing < -facing_epsilon && second_facing > facing_epsilon);
                draw = normal_dot < crease_cosine || silhouette;
            }
            if (!draw)
                continue;
            const auto [first, second] = edge;
            const auto& a = part->vertices[first];
            const auto& b = part->vertices[second];
            stream << 'M' << center_x + (coordinate(a, horizontal_axis) - horizontal_center) * scale
                   << ',' << center_y - (coordinate(a, vertical_axis) - vertical_center) * scale
                   << 'L' << center_x + (coordinate(b, horizontal_axis) - horizontal_center) * scale
                   << ',' << center_y - (coordinate(b, vertical_axis) - vertical_center) * scale;
        }
        stream << "\"/>\n";
    }
    stream << "</g>\n";
}

auto dimension(std::ostream& stream, double x, double y, double length,
               std::string_view label) -> void {
    stream << "<g class=\"dimension\"><line class=\"dimension-line\" x1=\"" << x << "\" y1=\"" << y
           << "\" x2=\"" << x + length << "\" y2=\"" << y
           << "\"/><line x1=\"" << x << "\" y1=\"" << y - 7.0 << "\" x2=\""
           << x << "\" y2=\"" << y + 7.0 << "\"/><line x1=\"" << x + length
           << "\" y1=\"" << y - 7.0 << "\" x2=\"" << x + length << "\" y2=\""
           << y + 7.0 << "\"/><text text-anchor=\"middle\" x=\"" << x + length * 0.5 << "\" y=\"" << y - 7.0
           << "\">" << xml(label) << "</text></g>\n";
}

auto vertical_dimension(std::ostream& stream, double x, double y, double length,
                        std::string_view label) -> void {
    stream << "<g class=\"dimension\"><line class=\"dimension-line\" x1=\"" << x << "\" y1=\"" << y
           << "\" x2=\"" << x << "\" y2=\"" << y + length
           << "\"/><line x1=\"" << x - 7.0 << "\" y1=\"" << y << "\" x2=\"" << x + 7.0
           << "\" y2=\"" << y << "\"/><line x1=\"" << x - 7.0 << "\" y1=\"" << y + length
           << "\" x2=\"" << x + 7.0 << "\" y2=\"" << y + length
           << "\"/><text text-anchor=\"middle\" transform=\"translate(" << x - 9.0 << ' ' << y + length * 0.5
           << ") rotate(-90)\">" << xml(label) << "</text></g>\n";
}

[[nodiscard]] auto drawing_family(std::string_view name) -> std::string {
    std::string family{name};
    const auto separator = family.find_last_of('_');
    if (separator == std::string::npos || separator + 1U == family.size())
        return family;
    const auto suffix = std::string_view{family}.substr(separator + 1U);
    if (std::ranges::all_of(suffix, [](unsigned char character) { return std::isdigit(character); }))
        family.resize(separator);
    return family;
}

[[nodiscard]] auto definition_for_occurrence(const compiler::ir::Project& project,
                                             std::string_view occurrence) -> std::string_view {
    const auto instance = std::ranges::find(project.instances, occurrence,
                                            &compiler::ir::ComponentInstance::name);
    return instance == project.instances.end() ? occurrence : std::string_view{instance->body};
}

[[nodiscard]] auto operation_name(compiler::ir::FeatureOperation operation) -> std::string_view {
    switch (operation) {
    case compiler::ir::FeatureOperation::create: return "NEW";
    case compiler::ir::FeatureOperation::unite: return "ADD";
    case compiler::ir::FeatureOperation::cut: return "CUT";
    case compiler::ir::FeatureOperation::intersect: return "INTERSECT";
    }
    return "UNKNOWN";
}

[[nodiscard]] auto solve_status_name(compiler::ir::SketchSolveStatus status) -> std::string_view {
    switch (status) {
    case compiler::ir::SketchSolveStatus::fully_constrained: return "FULLY CONSTRAINED";
    case compiler::ir::SketchSolveStatus::under_constrained: return "UNDER CONSTRAINED";
    case compiler::ir::SketchSolveStatus::inconsistent: return "INCONSISTENT";
    }
    return "UNKNOWN";
}

} // namespace

auto write_svg(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto model = cad::build_model(project);
    const auto analysis = cad::analyze(project, model);
    return write_svg(project, analysis, model, output);
}

auto write_svg(const compiler::ir::Project& project, const cad::ProjectAnalysis& analysis,
               const cad::Model& model, const std::filesystem::path& output) -> ExportResult {
    if (analysis.parts.empty()) {
        return {false, "project has no drawable parts"};
    }
    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return {false, "cannot open SVG drawing output"};
    }
    std::set<std::string> part_families;
    for (const auto& body : project.bodies)
        part_families.insert(drawing_family(body.name));
    const std::size_t sheet_count = part_families.size() + 2U;
    const double drawing_height = sheet_height * static_cast<double>(sheet_count);
    stream << std::setprecision(12) << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
           << sheet_width << "\" height=\"" << drawing_height << "\" viewBox=\"0 0 "
           << sheet_width << ' ' << drawing_height
           << "\"><defs><marker id=\"arrow\" markerWidth=\"8\" markerHeight=\"8\" refX=\"4\" refY=\"4\" orient=\"auto-start-reverse\"><path d=\"M0,0 L8,4 L0,8 z\" fill=\"#102030\"/></marker></defs><style>"
              ".sheet{fill:#fff;stroke:#102030;stroke-width:1}.view rect{fill:#fbfdff;stroke:#8090a0}"
              ".view path{fill:none;stroke:#102030;stroke-width:.7;vector-effect:non-scaling-stroke}"
              "text{font-family:Arial,sans-serif;fill:#102030;text-anchor:start}.title{font-size:28px;font-weight:700}"
              ".subtitle{font-size:13px;fill:#526273}.view-label{font-size:13px;font-weight:700}"
              ".centerline{stroke:#526273;stroke-width:.65;stroke-dasharray:12 3 2 3}.dimension{fill:none;stroke:#102030;stroke-width:.8}.dimension-line{marker-start:url(#arrow);marker-end:url(#arrow)}.dimension text{fill:#102030;"
              "stroke:none;font-size:12px}.section{font-size:15px;font-weight:700}"
              ".row{font-size:12px}.small{font-size:10px}.rule{stroke:#a8b3bd;stroke-width:.7}"
              "@media print{.sheet-group{break-after:page}}</style>\n";

    std::vector<const cad::Part*> overview_parts;
    overview_parts.reserve(model.parts.size());
    for (const auto& part : model.parts)
        overview_parts.push_back(&part);
    const auto overview_bounds = drawing_bounds(overview_parts);
    const std::array overview_extent{overview_bounds.maximum[0] - overview_bounds.minimum[0],
                                     overview_bounds.maximum[1] - overview_bounds.minimum[1],
                                     overview_bounds.maximum[2] - overview_bounds.minimum[2]};
    const bool building_project = project.name.find("hall") != std::string::npos;
    const auto drawing_status = building_project
                                    ? "REFERENCE ONLY — NOT FOR CONSTRUCTION"
                                    : "DEVELOPMENT MANUFACTURING DETAIL — NOT RELEASED";
    stream << "<g class=\"sheet-group\" id=\"assembly-overview\" data-sheet-kind=\"assembly\">"
              "<rect class=\"sheet\" x=\"20\" y=\"20\" width=\"1560\" height=\"1160\"/>"
              "<text class=\"title\" x=\"50\" y=\"62\">GENERAL ARRANGEMENT — "
           << xml(project.name)
           << "</text><text class=\"subtitle\" x=\"50\" y=\"87\">A-A · THIRD-ANGLE PROJECTION · "
           << drawing_status << " · Sheet 1 of " << sheet_count << "</text>";
    projected_view(stream, overview_parts, overview_bounds, 0, 2, 50, 118, 900, 360,
                   building_project ? "TRANSVERSE ELEVATION" : "SIDE ELEVATION");
    projected_view(stream, overview_parts, overview_bounds, 1, 2, 1000, 118, 540, 360,
                   building_project ? "LONGITUDINAL ELEVATION" : "INLET END VIEW");
    projected_view(stream, overview_parts, overview_bounds, 0, building_project ? 1 : 2,
                   50, 555, 1120, 355,
                   building_project ? "ROOF PLAN" : "LONGITUDINAL SECTION A-A");
    if (building_project)
        dimension(stream, 82, 945, 1060,
                  "OVERALL LENGTH " + number(overview_extent[1], 1) + " mm");
    dimension(stream, 82, 510, 835,
              std::string{building_project ? "OVERALL WIDTH " : "OVERALL LENGTH "} +
                  number(overview_extent[0], 1) + " mm");
    vertical_dimension(stream, 1515, 170, 260,
                       std::string{building_project ? "OVERALL HEIGHT " : "MAX DIA Ø"} +
                           number(building_project
                                      ? overview_extent[2]
                                      : std::max(overview_extent[1], overview_extent[2]),
                                  1) +
                           " mm");
    stream << "<text class=\"section\" x=\"1205\" y=\"565\">"
           << (building_project ? "BUILDING DATUM / STATUS" : "ENGINE DATUM / STATUS")
           << "</text><text class=\"row\" x=\"1205\" y=\"593\">"
           << (building_project ? "A — FOUNDATION / COLUMN GRID" : "A — ENGINE AXIS / SHAFT JOURNALS")
           << "</text><text class=\"row\" x=\"1205\" y=\"616\">"
           << (building_project ? "B — LEFT COLUMN BASE LINE" : "B — FRONT MOUNTING FLANGE")
           << "</text><text class=\"row\" x=\"1205\" y=\"639\">"
           << (building_project ? "C — FINISHED FLOOR LEVEL" : "C — VERTICAL MOUNT PLANE")
           << "</text>"
              "<text class=\"row\" x=\"1205\" y=\"680\">GENERAL TOLERANCE ISO 2768-mK</text>"
              "<text class=\"row\" x=\"1205\" y=\"703\">DIMENSIONING ISO 129-1</text>"
              "<text class=\"row\" x=\"1205\" y=\"726\">PROJECTION ISO 5456-2</text>"
              "<text class=\"row\" x=\"1205\" y=\"749\">TITLE BLOCK ISO 7200</text>"
              "<rect x=\"1040\" y=\"1015\" width=\"510\" height=\"135\" fill=\"none\" stroke=\"#102030\"/>"
              "<line class=\"rule\" x1=\"1040\" y1=\"1050\" x2=\"1550\" y2=\"1050\"/>"
              "<line class=\"rule\" x1=\"1040\" y1=\"1085\" x2=\"1550\" y2=\"1085\"/>"
              "<text class=\"row\" x=\"1055\" y=\"1038\">TITLE: " << xml(project.name)
           << " — GENERAL ARRANGEMENT</text><text class=\"row\" x=\"1055\" y=\"1073\">DWG: ICAD-"
           << xml(project.name) << "-GA · REV A · SHEET 1/" << sheet_count
           << "</text><text class=\"row\" x=\"1055\" y=\"1108\">UNITS: "
           << xml(project.canonical_length_unit)
           << " · SCALE: NTS · THIRD ANGLE</text><text class=\"row\" x=\"1055\" y=\"1137\">STATUS: "
           << drawing_status << " · DATUMS A | B | C</text></g>\n";

    std::size_t sheet_index = 1;
    std::set<std::string> emitted_families;
    for (const auto& body : project.bodies) {
        const auto family = drawing_family(body.name);
        if (!emitted_families.insert(family).second)
            continue;
        const double offset = sheet_height * static_cast<double>(sheet_index);
        std::vector<const cad::Part*> parts;
        for (const auto& part : model.parts) {
            if (part.body == body.name)
                parts.push_back(&part);
        }
        const auto bounds = drawing_bounds(parts);
        const std::array extent{bounds.maximum[0] - bounds.minimum[0],
                                bounds.maximum[1] - bounds.minimum[1],
                                bounds.maximum[2] - bounds.minimum[2]};
        std::size_t quantity = static_cast<std::size_t>(std::ranges::count_if(
            project.bodies, [&family](const auto& candidate) {
                return drawing_family(candidate.name) == family;
            }));
        quantity += static_cast<std::size_t>(std::ranges::count_if(
            project.instances, [&family](const auto& instance) {
                return drawing_family(instance.body) == family;
            }));
        double surface_area = 0.0;
        double volume = 0.0;
        for (const auto& part : analysis.parts) {
            if (part.body == body.name) {
                surface_area += part.surface_area_mm2;
                volume += part.volume_mm3;
            }
        }

        stream << "<g class=\"sheet-group\" id=\"part-sheet-" << sheet_index + 1U
               << "\" data-sheet-kind=\"part\" transform=\"translate(0 " << offset
               << ")\"><rect class=\"sheet\" x=\"20\" y=\"20\" width=\"1560\" height=\"1160\"/>"
                  "<text class=\"title\" x=\"50\" y=\"62\">PART DETAIL — "
               << xml(family) << "</text><text class=\"subtitle\" x=\"50\" y=\"87\">"
               << xml(project.name) << " · Third-angle projected native edges · Sheet "
               << sheet_index + 1U << " of " << sheet_count << "</text>";
        projected_view(stream, parts, bounds, 0, 1, 50, 115, 470, 330, "TOP (X/Y)");
        projected_view(stream, parts, bounds, 0, 2, 565, 115, 470, 330, "FRONT (X/Z)");
        projected_view(stream, parts, bounds, 1, 2, 1080, 115, 470, 330, "RIGHT (Y/Z)");
        const bool airfoil = family.contains("blade") || family.contains("vane");
        dimension(stream, 65, 478, 420, (airfoil ? "AXIAL CHORD " : "OVERALL LENGTH ") + number(extent[0]) + " mm");
        dimension(stream, 580, 478, 420, (airfoil ? "RADIAL SPAN " : "OVERALL WIDTH ") + number(extent[1]) + " mm");
        dimension(stream, 1095, 478, 420, (airfoil ? "MAX THICKNESS " : "OVERALL HEIGHT ") + number(extent[2]) + " mm");

        stream << "<text class=\"section\" x=\"50\" y=\"535\">MANUFACTURING DEFINITION</text>"
                  "<text class=\"row\" x=\"50\" y=\"560\">MATERIAL: "
               << xml(body.material) << "</text><text class=\"row\" x=\"310\" y=\"560\">QUANTITY: "
               << quantity << "</text><text class=\"row\" x=\"470\" y=\"560\">SOLIDS: "
               << parts.size() << "</text><text class=\"row\" x=\"610\" y=\"560\">VOLUME: "
               << number(volume) << " mm³</text><text class=\"row\" x=\"890\" y=\"560\">AREA: "
               << number(surface_area) << " mm²</text><text class=\"row\" x=\"1190\" y=\"560\">"
                  "TOLERANCE: ±"
               << number(project.tolerance.linear_mm) << " mm / ±"
               << number(project.tolerance.angular_degrees) << "°</text>"
                  "<text class=\"section\" x=\"50\" y=\"610\">FEATURE AND PARAMETER SCHEDULE</text>";
        double row_y = 635.0;
        std::size_t feature_rows = 0;
        for (const auto& feature : body.features) {
            if (feature_rows >= 6U)
                break;
            std::string properties;
            for (const auto& property : feature.properties) {
                if (!properties.empty())
                    properties += " | ";
                properties += property.name + "=" + number(property.value.value) + property.value.unit;
            }
            constexpr std::size_t schedule_width = 68U;
            if (properties.size() > schedule_width) {
                properties.resize(schedule_width - 3U);
                properties += "...";
            }
            stream << "<text class=\"row\" x=\"50\" y=\"" << row_y << "\">"
                   << feature_rows + 1U << ". " << xml(feature.name) << " | "
                   << xml(feature.source_keyword) << '/' << xml(feature.type) << " | "
                   << operation_name(feature.operation) << " | " << xml(properties) << "</text>";
            row_y += 21.0;
            ++feature_rows;
        }
        if (body.features.size() > feature_rows) {
            stream << "<text class=\"small\" x=\"50\" y=\"" << row_y
                   << "\">+ " << body.features.size() - feature_rows
                   << " additional feature operations in authoritative ICAD source</text>";
            row_y += 18.0;
        }

        row_y += 18.0;
        stream << "<text class=\"section\" x=\"50\" y=\"" << row_y
               << "\">SKETCH / PROFILE SCHEDULE</text>";
        row_y += 22.0;
        std::size_t sketch_rows = 0;
        for (const auto& sketch : project.sketches) {
            if (sketch.body != body.name || sketch_rows >= 10U)
                continue;
            stream << "<text class=\"small\" x=\"50\" y=\"" << row_y << "\">SKETCH "
                   << xml(sketch.name) << " | PLANE " << xml(sketch.plane) << " | "
                   << solve_status_name(sketch.status) << " | DOF " << sketch.degrees_of_freedom
                   << " | MAX RESIDUAL " << number(sketch.maximum_residual, 6) << "</text>";
            row_y += 18.0;
            ++sketch_rows;
            for (const auto& shape : sketch.shapes) {
                if (sketch_rows >= 10U)
                    break;
                std::string circular_sizes;
                for (const auto& entity_name : shape.entities) {
                    const auto entity = std::ranges::find(sketch.entities, entity_name,
                                                          &compiler::ir::SketchEntity::name);
                    if (entity == sketch.entities.end() || !entity->full_circle)
                        continue;
                    if (!circular_sizes.empty())
                        circular_sizes += ", ";
                    circular_sizes += "DIA " + number(entity->radius_mm * 2.0) + " mm";
                }
                stream << "<text class=\"small\" x=\"70\" y=\"" << row_y << "\">SHAPE "
                       << xml(shape.name) << " | " << xml(shape.role) << " | AREA "
                       << number(shape.area_mm2) << " mm²";
                if (!shape.containing_shape.empty())
                    stream << " | IN " << xml(shape.containing_shape);
                if (!circular_sizes.empty())
                    stream << " | " << circular_sizes;
                stream << "</text>";
                row_y += 18.0;
                ++sketch_rows;
            }
        }
        if (sketch_rows == 0U)
            stream << "<text class=\"small\" x=\"50\" y=\"" << row_y
                   << "\">No authored sketch workspace; inspect low-level feature properties above.</text>";

        stream << "<text class=\"section\" x=\"850\" y=\"610\">INTERFACES / INSPECTION</text>";
        double interface_y = 635.0;
        std::size_t interface_count = 0;
        for (const auto& interface : project.interfaces) {
            if (definition_for_occurrence(project, interface.occurrence) != body.name)
                continue;
            stream << "<text class=\"row\" x=\"850\" y=\"" << interface_y << "\">"
                   << xml(interface.name) << " | " << xml(interface.kind) << " | AT "
                   << xml(interface.point) << " | AXIS " << xml(interface.axis);
            if (interface.has_size)
                stream << " | SIZE " << number(interface.size_mm) << " mm";
            stream << "</text>";
            interface_y += 21.0;
            ++interface_count;
        }
        if (interface_count == 0U)
            stream << "<text class=\"row\" x=\"850\" y=\"635\">No named assembly interfaces on this definition.</text>";

        const auto body_sketches = static_cast<std::size_t>(std::ranges::count(
            project.sketches, body.name, &compiler::ir::Sketch::body));
        stream << "<text class=\"section\" x=\"50\" y=\"930\">RELEASE NOTES</text>"
                  "<text class=\"row\" x=\"50\" y=\"955\">DATUM A: primary XY support · DATUM B: XZ orientation · DATUM C: YZ orientation</text>"
                  "<text class=\"row\" x=\"50\" y=\"978\">SKETCH WORKSPACES: "
               << body_sketches << " · FEATURE OPERATIONS: " << body.features.size()
               << " · Verify all named holes, fits, edge treatments, and mating faces against source.</text>"
                  "<text class=\"row\" x=\"50\" y=\"1001\">GENERAL: deburr edges; ISO 2768-mK unless specified; dimensions ISO 129-1; third-angle projection ISO 5456-2.</text>"
                  "<rect x=\"1040\" y=\"1040\" width=\"510\" height=\"110\" fill=\"none\" stroke=\"#102030\"/>"
                  "<text class=\"row\" x=\"1055\" y=\"1066\">TITLE: "
               << xml(family) << "</text><text class=\"row\" x=\"1055\" y=\"1091\">UNITS: "
               << xml(project.canonical_length_unit) << " · SCALE: NTS · PROJECTION: THIRD ANGLE</text>"
                  "<text class=\"row\" x=\"1055\" y=\"1116\">DATUMS: A | B | C · REV A · STATUS: MANUFACTURING DETAIL</text>"
                  "<text class=\"row\" x=\"1055\" y=\"1141\">ISO 7200 TITLE DATA · SOURCE: DECLARATIVE ICAD MODEL</text></g>\n";
        ++sheet_index;
    }

    const double assembly_offset = sheet_height * static_cast<double>(sheet_index);
    std::vector<const cad::Part*> assembly_parts;
    assembly_parts.reserve(model.parts.size());
    for (const auto& part : model.parts)
        assembly_parts.push_back(&part);
    const auto assembly_bounds = drawing_bounds(assembly_parts);
    stream << "<g class=\"sheet-group\" id=\"assembly-sheet\" data-sheet-kind=\"assembly\" transform=\"translate(0 "
           << assembly_offset
           << ")\"><rect class=\"sheet\" x=\"20\" y=\"20\" width=\"1560\" height=\"1160\"/>"
              "<text class=\"title\" x=\"50\" y=\"62\">ASSEMBLY SHEET — "
           << xml(project.name) << "</text><text class=\"subtitle\" x=\"50\" y=\"87\">Final assembly after all part detail sheets · Sheet "
           << sheet_count << " of " << sheet_count << "</text>";
    projected_view(stream, assembly_parts, assembly_bounds, 0, 1, 50, 115, 470, 330, "ASSEMBLY TOP");
    projected_view(stream, assembly_parts, assembly_bounds, 0, 2, 565, 115, 470, 330, "ASSEMBLY FRONT");
    projected_view(stream, assembly_parts, assembly_bounds, 1, 2, 1080, 115, 470, 330, "ASSEMBLY RIGHT");
    stream << "<text class=\"section\" x=\"50\" y=\"495\">BILL OF MATERIALS</text>"
              "<text class=\"small\" x=\"50\" y=\"518\">ITEM</text><text class=\"small\" x=\"105\" y=\"518\">DEFINITION</text>"
              "<text class=\"small\" x=\"390\" y=\"518\">QTY</text><text class=\"small\" x=\"445\" y=\"518\">MATERIAL</text>";
    double bom_y = 541.0;
    for (std::size_t index = 0; index < project.bodies.size(); ++index) {
        const auto& body = project.bodies[index];
        const auto quantity = 1U + static_cast<unsigned int>(std::ranges::count(
                                       project.instances, body.name,
                                       &compiler::ir::ComponentInstance::body));
        stream << "<text class=\"row\" x=\"50\" y=\"" << bom_y << "\">" << index + 1U
               << "</text><text class=\"row\" x=\"105\" y=\"" << bom_y << "\">"
               << xml(body.name) << "</text><text class=\"row\" x=\"390\" y=\"" << bom_y
               << "\">" << quantity << "</text><text class=\"row\" x=\"445\" y=\"" << bom_y
               << "\">" << xml(body.material) << "</text>";
        bom_y += 22.0;
    }

    stream << "<text class=\"section\" x=\"760\" y=\"495\">ASSEMBLY CONNECTION SCHEDULE</text>";
    double connection_y = 520.0;
    for (const auto& connection : project.connections) {
        stream << "<text class=\"small\" x=\"760\" y=\"" << connection_y << "\">"
               << xml(connection.name) << " | " << xml(connection.first_interface) << " ↔ "
               << xml(connection.second_interface) << " | " << xml(connection.method) << " | "
               << xml(connection.standard);
        if (!connection.fastener.empty())
            stream << " | FASTENER " << xml(connection.fastener);
        if (!connection.fit.empty())
            stream << " | FIT " << xml(connection.fit);
        stream << " | CLR " << number(connection.clearance_mm) << " mm | GAP "
               << number(connection.interface_gap_mm) << " mm</text>";
        connection_y += 18.0;
    }
    if (project.connections.empty())
        stream << "<text class=\"row\" x=\"760\" y=\"520\">No authored assembly connections.</text>";

    const double notes_y = std::max({780.0, bom_y + 25.0, connection_y + 25.0});
    stream << "<text class=\"section\" x=\"50\" y=\"" << notes_y << "\">ASSEMBLY / INSPECTION NOTES</text>"
              "<text class=\"row\" x=\"50\" y=\"" << notes_y + 25.0
           << "\">1. Manufacture and inspect every preceding part sheet before assembly.</text>"
              "<text class=\"row\" x=\"50\" y=\"" << notes_y + 48.0
           << "\">2. Seat named interfaces to authored gap, axis alignment, fit, fastener, and standard requirements.</text>"
              "<text class=\"row\" x=\"50\" y=\"" << notes_y + 71.0
           << "\">3. Verify zero unintended penetration, joint limits, overall envelope, and first/middle/last inspection frames.</text>"
              "<text class=\"row\" x=\"50\" y=\"" << notes_y + 94.0
           << "\">4. General tolerance: ±" << number(project.tolerance.linear_mm) << " mm linear / ±"
           << number(project.tolerance.angular_degrees) << "° angular unless individually specified.</text>"
              "<rect x=\"1040\" y=\"1040\" width=\"510\" height=\"110\" fill=\"none\" stroke=\"#102030\"/>"
              "<text class=\"row\" x=\"1055\" y=\"1066\">TITLE: " << xml(project.name)
           << " ASSEMBLY</text><text class=\"row\" x=\"1055\" y=\"1091\">BOM ITEMS: "
           << project.bodies.size() << " · CONNECTIONS: " << project.connections.size()
           << " · JOINTS: " << project.joints.size() << "</text>"
              "<text class=\"row\" x=\"1055\" y=\"1116\">DATUMS: A | B | C · STATUS: ASSEMBLY RELEASE</text>"
              "<text class=\"row\" x=\"1055\" y=\"1141\">UNITS: "
           << xml(project.canonical_length_unit) << " · SCALE: NTS · ISO 129-1 / ISO 5456-2</text></g>\n</svg>\n";
    return {static_cast<bool>(stream),
            "SVG manufacturing drawing set complete: general arrangement, part families, and assembly release"};
}

namespace {

auto line(std::ostream& stream, std::string_view layer, double x1, double y1, double x2,
          double y2) -> void {
    stream << "0\nLINE\n8\n" << layer << "\n10\n" << x1 << "\n20\n" << y1
           << "\n30\n0\n11\n" << x2 << "\n21\n" << y2 << "\n31\n0\n";
}

auto rectangle(std::ostream& stream, std::string_view layer, double x1, double y1, double x2,
               double y2) -> void {
    line(stream, layer, x1, y1, x2, y1);
    line(stream, layer, x2, y1, x2, y2);
    line(stream, layer, x2, y2, x1, y2);
    line(stream, layer, x1, y2, x1, y1);
}

auto text(std::ostream& stream, std::string_view layer, std::string_view value, double x,
          double y, double height) -> void {
    stream << "0\nTEXT\n8\n" << layer << "\n10\n" << x << "\n20\n" << y
           << "\n30\n0\n40\n" << height << "\n1\n" << value << "\n";
}

} // namespace

auto write_dxf(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto model = cad::build_model(project);
    const auto analysis = cad::analyze(project, model);
    return write_dxf(project, analysis, model, output);
}

auto write_dxf(const compiler::ir::Project& project, const cad::ProjectAnalysis& analysis,
               const cad::Model& model, const std::filesystem::path& output) -> ExportResult {
    if (analysis.parts.empty())
        return {false, "project has no drawable parts"};
    std::ofstream stream{output, std::ios::binary};
    if (!stream)
        return {false, "cannot open DXF drawing output"};
    stream << std::setprecision(17)
           << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1027\n9\n$INSUNITS\n70\n4\n"
              "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    const double width = analysis.bounds.maximum[0] - analysis.bounds.minimum[0];
    const double depth = analysis.bounds.maximum[1] - analysis.bounds.minimum[1];
    const double front_offset = width + 20.0;
    const double right_offset = front_offset + width + 20.0;
    for (const auto& part : model.parts) {
        std::set<std::pair<std::size_t, std::size_t>> edges;
        for (const auto& triangle : part.triangles) {
            for (std::size_t edge = 0; edge < 3; ++edge) {
                edges.emplace(std::min(triangle[edge], triangle[(edge + 1) % 3]),
                              std::max(triangle[edge], triangle[(edge + 1) % 3]));
            }
        }
        for (const auto& [first, second] : edges) {
            const auto& a = part.vertices[first];
            const auto& b = part.vertices[second];
            line(stream, "VISIBLE_TOP", a.x, a.y, b.x, b.y);
            line(stream, "VISIBLE_FRONT", front_offset + a.x, a.z, front_offset + b.x, b.z);
            line(stream, "VISIBLE_RIGHT", right_offset + a.y, a.z, right_offset + b.y, b.z);
        }
    }
    const double dimension_y = analysis.bounds.maximum[1] + 8.0;
    line(stream, "DIMENSION", analysis.bounds.minimum[0], dimension_y,
         analysis.bounds.maximum[0], dimension_y);
    line(stream, "DIMENSION", analysis.bounds.minimum[0], dimension_y - 2.0,
         analysis.bounds.minimum[0], dimension_y + 2.0);
    line(stream, "DIMENSION", analysis.bounds.maximum[0], dimension_y - 2.0,
         analysis.bounds.maximum[0], dimension_y + 2.0);
    text(stream, "DIMENSION", "OVERALL WIDTH " + std::to_string(width) + " mm",
         analysis.bounds.minimum[0], dimension_y + 2.0, 2.5);
    text(stream, "ANNOTATION", project.name, 0.0, depth + 12.0, 5.0);
    text(stream, "ANNOTATION", "TOP", 0.0, -8.0, 3.0);
    text(stream, "ANNOTATION", "FRONT", front_offset, -8.0, 3.0);
    text(stream, "ANNOTATION", "RIGHT", right_offset, -8.0, 3.0);
    const double title_y = dimension_y + 18.0;
    rectangle(stream, "TITLE_BLOCK", 0.0, title_y, right_offset + depth, title_y + 20.0);
    text(stream, "TITLE_BLOCK", "TITLE " + project.name, 2.0, title_y + 14.0, 3.0);
    text(stream, "TITLE_BLOCK", "DATUMS A B C", 2.0, title_y + 9.0, 2.5);
    text(stream, "TITLE_BLOCK",
         "GENERAL TOLERANCE +/- " + std::to_string(project.tolerance.linear_mm) + " mm", 2.0,
         title_y + 4.0, 2.5);
    stream << "0\nENDSEC\n0\nEOF\n";
    return {static_cast<bool>(stream), "DXF R2013 orthographic drawing export complete"};
}

auto inspect_dxf(const std::filesystem::path& input) -> ExportResult {
    std::ifstream stream{input, std::ios::binary};
    if (!stream)
        return {false, "cannot open DXF drawing"};
    const std::string content{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
    if (!content.contains("$ACADVER\n1\nAC1027") || !content.contains("0\nSECTION\n") ||
        !content.ends_with("0\nEOF\n") || !content.contains("0\nLINE\n"))
        return {false, "DXF structure is invalid"};
    return {true, "DXF structural validation passed"};
}

} // namespace icad::drawings
