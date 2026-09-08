#include "icad/ai/inspector.hpp"

#include "icad/cad/analysis.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/dependency_graph.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/document/revision.hpp"
#include "icad/manufacturing/validator.hpp"
#include "icad/scene/evaluator.hpp"

#include "../cad/model.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace icad::ai {
namespace {

[[nodiscard]] auto quoted(std::string_view value) -> std::string {
    std::string result{"\""};
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(character);
            break;
        }
    }
    result.push_back('"');
    return result;
}

auto write_topology_selection(std::ostringstream& output,
                              const compiler::ir::TopologySelection& selection) -> void {
    output << "{\"name\":" << quoted(selection.name)
           << ",\"sourceFeature\":" << quoted(selection.source_feature)
           << ",\"kind\":" << quoted(selection.entity_kind)
           << ",\"geometry\":" << quoted(selection.geometry)
           << ",\"predicates\":{\"loop\":true,\"convexity\":"
           << quoted(selection.convexity) << ",\"adjacentFace\":"
           << quoted(selection.adjacent_face) << "},\"matchedTopologyId\":"
           << quoted(selection.topology_id)
           << ",\"matchReason\":\"matched one circular "
           << (selection.convexity == "CONCAVE" ? "concave" : "convex")
           << " edge loop adjacent to the "
           << (selection.adjacent_face == "TOP" ? "top" : "bottom")
           << " face of the source feature\",\"applicability\":{\"allowed\":[\"FILLET\",\"CHAMFER\"],"
              "\"rejected\":[{\"operation\":\"SHELL\",\"reason\":\"requires a body or face selection\"},"
              "{\"operation\":\"OFFSET_FACE\",\"reason\":\"requires a face selection\"},"
              "{\"operation\":\"SPLIT\",\"reason\":\"requires a plane or surface tool\"}]}}";
}

[[nodiscard]] auto feature_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += body.features.size();
    }
    return count;
}

[[nodiscard]] auto topology_selection_count(const compiler::ir::Project& project)
    -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies)
        count += body.topology_selections.size();
    return count;
}

[[nodiscard]] auto boolean_operation_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += static_cast<std::size_t>(std::ranges::count_if(body.features, [](const auto& feature) {
            return feature.operation != compiler::ir::FeatureOperation::create;
        }));
    }
    return count;
}

[[nodiscard]] auto modeling_operation_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += static_cast<std::size_t>(std::ranges::count_if(body.features, [](const auto& feature) {
            return feature.type == "CHAMFER" || feature.type == "FILLET" ||
                   feature.type == "LINEAR_PATTERN" || feature.type == "MIRROR";
        }));
    }
    return count;
}

[[nodiscard]] auto surface_operation_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += static_cast<std::size_t>(std::ranges::count_if(body.features, [](const auto& feature) {
            return feature.type == "SWEEP" || feature.type == "LOFT" ||
                   feature.type == "FREEFORM" || feature.type == "REVOLVE";
        }));
    }
    return count;
}

[[nodiscard]] auto profile_segment_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& profile : project.profiles)
        count += profile.segments.size();
    return count;
}

[[nodiscard]] auto curved_profile_segment_count(const compiler::ir::Project& project)
    -> std::size_t {
    std::size_t count = 0;
    for (const auto& profile : project.profiles) {
        count += static_cast<std::size_t>(
            std::ranges::count(profile.segments, compiler::ir::ProfileSegmentKind::circular_arc,
                               &compiler::ir::ProfileSegment::kind));
    }
    return count;
}

[[nodiscard]] auto severity_name(compiler::DiagnosticSeverity severity) -> std::string_view {
    switch (severity) {
    case compiler::DiagnosticSeverity::error:
        return "error";
    case compiler::DiagnosticSeverity::warning:
        return "warning";
    case compiler::DiagnosticSeverity::note:
        return "note";
    }
    return "error";
}

[[nodiscard]] auto joint_kind_name(compiler::ir::JointKind kind) -> std::string_view {
    switch (kind) {
    case compiler::ir::JointKind::fixed:
        return "fixed";
    case compiler::ir::JointKind::revolute:
        return "revolute";
    case compiler::ir::JointKind::prismatic:
        return "prismatic";
    }
    return "fixed";
}

[[nodiscard]] auto point_kind_name(compiler::ir::SpatialPointKind kind) -> std::string_view {
    return kind == compiler::ir::SpatialPointKind::offset ? "offset" : "absolute";
}

[[nodiscard]] auto direction_kind_name(compiler::ir::DirectionKind kind) -> std::string_view {
    switch (kind) {
    case compiler::ir::DirectionKind::components:
        return "components";
    case compiler::ir::DirectionKind::between_points:
        return "betweenPoints";
    case compiler::ir::DirectionKind::rotated:
        return "rotated";
    }
    return "components";
}

[[nodiscard]] auto sketch_status_name(compiler::ir::SketchSolveStatus status) -> std::string_view {
    switch (status) {
    case compiler::ir::SketchSolveStatus::fully_constrained:
        return "fullyConstrained";
    case compiler::ir::SketchSolveStatus::under_constrained:
        return "underConstrained";
    case compiler::ir::SketchSolveStatus::inconsistent:
        return "inconsistent";
    }
    return "inconsistent";
}

struct ProjectedPoint {
    double u{};
    double v{};
    double depth{};
};

struct SnapshotView {
    std::string_view name;
    std::string_view horizontal_axis;
    std::string_view vertical_axis;
    std::array<double, 3> horizontal;
    std::array<double, 3> vertical;
    std::array<double, 3> depth;
};

constexpr std::array snapshot_views{
    SnapshotView{"front", "+X", "+Z", {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
                 {0.0, 1.0, 0.0}},
    SnapshotView{"right", "+Y", "+Z", {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0},
                 {1.0, 0.0, 0.0}},
    SnapshotView{"top", "+X", "+Y", {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
                 {0.0, 0.0, 1.0}},
    SnapshotView{"isometric", "+X -Y", "+Z -X -Y", {0.7071067811865476, -0.7071067811865476, 0.0},
                 {-0.408248290463863, -0.408248290463863, 0.816496580927726},
                 {0.577350269189626, 0.577350269189626, 0.577350269189626}},
};

constexpr std::size_t snapshot_width = 64;
constexpr std::size_t snapshot_height = 32;
constexpr std::size_t vision_image_width = 512;
constexpr std::size_t vision_image_height = 512;
constexpr std::string_view snapshot_symbols =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

struct SnapshotRaster {
    std::array<double, 4> bounds{};
    std::vector<char> pixels;
    std::size_t occupied_cells{};
};

struct VisionRaster {
    std::vector<std::uint8_t> pixels;
};

auto append_u32_be(std::string& bytes, std::uint32_t value) -> void {
    bytes.push_back(static_cast<char>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<char>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<char>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<char>(value & 0xffU));
}

[[nodiscard]] auto crc32(std::string_view value) -> std::uint32_t {
    std::uint32_t crc = 0xffffffffU;
    for (const char character : value) {
        crc ^= static_cast<unsigned char>(character);
        for (std::size_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

[[nodiscard]] auto adler32(std::string_view value) -> std::uint32_t {
    constexpr std::uint32_t modulus = 65521U;
    std::uint32_t first = 1U;
    std::uint32_t second = 0U;
    for (const char character : value) {
        first = (first + static_cast<unsigned char>(character)) % modulus;
        second = (second + first) % modulus;
    }
    return (second << 16U) | first;
}

auto append_png_chunk(std::string& png, std::string_view type, std::string_view data) -> void {
    append_u32_be(png, static_cast<std::uint32_t>(data.size()));
    png.append(type);
    png.append(data);
    std::string crc_input{type};
    crc_input.append(data);
    append_u32_be(png, crc32(crc_input));
}

[[nodiscard]] auto stored_zlib(std::string_view raw) -> std::string {
    std::string result;
    result.reserve(raw.size() + raw.size() / 65535U * 5U + 8U);
    result.push_back(static_cast<char>(0x78));
    result.push_back(static_cast<char>(0x01));
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const auto block_size = static_cast<std::uint16_t>(
            std::min<std::size_t>(65535U, raw.size() - offset));
        const bool final = offset + block_size == raw.size();
        result.push_back(static_cast<char>(final ? 0x01 : 0x00));
        result.push_back(static_cast<char>(block_size & 0xffU));
        result.push_back(static_cast<char>((block_size >> 8U) & 0xffU));
        const auto inverse = static_cast<std::uint16_t>(~block_size);
        result.push_back(static_cast<char>(inverse & 0xffU));
        result.push_back(static_cast<char>((inverse >> 8U) & 0xffU));
        result.append(raw.substr(offset, block_size));
        offset += block_size;
    }
    append_u32_be(result, adler32(raw));
    return result;
}

[[nodiscard]] auto base64_encode(std::string_view bytes) -> std::string {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((bytes.size() + 2U) / 3U) * 4U);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3U) {
        const auto first = static_cast<unsigned char>(bytes[offset]);
        const auto second = offset + 1U < bytes.size()
                                ? static_cast<unsigned char>(bytes[offset + 1U])
                                : 0U;
        const auto third = offset + 2U < bytes.size()
                               ? static_cast<unsigned char>(bytes[offset + 2U])
                               : 0U;
        const std::uint32_t value = (static_cast<std::uint32_t>(first) << 16U) |
                                    (static_cast<std::uint32_t>(second) << 8U) | third;
        result.push_back(alphabet[(value >> 18U) & 0x3fU]);
        result.push_back(alphabet[(value >> 12U) & 0x3fU]);
        result.push_back(offset + 1U < bytes.size() ? alphabet[(value >> 6U) & 0x3fU] : '=');
        result.push_back(offset + 2U < bytes.size() ? alphabet[value & 0x3fU] : '=');
    }
    return result;
}

[[nodiscard]] auto project_point(const cad::Point3& point, const SnapshotView& view)
    -> ProjectedPoint {
    const auto dot = [&](const std::array<double, 3>& axis) {
        return point.x * axis[0] + point.y * axis[1] + point.z * axis[2];
    };
    return {dot(view.horizontal), dot(view.vertical), dot(view.depth)};
}

[[nodiscard]] auto body_names(const cad::Model& model) -> std::vector<std::string> {
    std::vector<std::string> bodies;
    std::unordered_set<std::string_view> seen;
    seen.reserve(model.parts.size());
    for (const auto& part : model.parts) {
        if (seen.insert(part.body).second)
            bodies.push_back(part.body);
    }
    return bodies;
}

[[nodiscard]] auto inside_triangle(double x, double y, const ProjectedPoint& first,
                                   const ProjectedPoint& second, const ProjectedPoint& third,
                                   std::array<double, 3>& weights) -> bool {
    const double denominator = (second.v - third.v) * (first.u - third.u) +
                               (third.u - second.u) * (first.v - third.v);
    if (std::abs(denominator) <= 1.0e-12)
        return false;
    weights[0] = ((second.v - third.v) * (x - third.u) +
                  (third.u - second.u) * (y - third.v)) /
                 denominator;
    weights[1] = ((third.v - first.v) * (x - third.u) +
                  (first.u - third.u) * (y - third.v)) /
                 denominator;
    weights[2] = 1.0 - weights[0] - weights[1];
    constexpr double tolerance = -1.0e-9;
    return weights[0] >= tolerance && weights[1] >= tolerance &&
           weights[2] >= tolerance;
}

[[nodiscard]] auto snapshot_bounds(const cad::Model& model, const SnapshotView& view)
    -> std::array<double, 4> {
    double minimum_u = std::numeric_limits<double>::max();
    double minimum_v = std::numeric_limits<double>::max();
    double maximum_u = std::numeric_limits<double>::lowest();
    double maximum_v = std::numeric_limits<double>::lowest();
    for (const auto& part : model.parts) {
        for (const auto& vertex : part.vertices) {
            const auto point = project_point(vertex, view);
            minimum_u = std::min(minimum_u, point.u);
            minimum_v = std::min(minimum_v, point.v);
            maximum_u = std::max(maximum_u, point.u);
            maximum_v = std::max(maximum_v, point.v);
        }
    }
    if (minimum_u > maximum_u) {
        minimum_u = minimum_v = 0.0;
        maximum_u = maximum_v = 1.0;
    }
    const double span_u = std::max(maximum_u - minimum_u, 1.0e-9);
    const double span_v = std::max(maximum_v - minimum_v, 1.0e-9);
    minimum_u -= span_u * 0.03;
    maximum_u += span_u * 0.03;
    minimum_v -= span_v * 0.03;
    maximum_v += span_v * 0.03;
    return {minimum_u, minimum_v, maximum_u, maximum_v};
}

[[nodiscard]] auto square_snapshot_bounds(const cad::Model& model, const SnapshotView& view)
    -> std::array<double, 4> {
    auto bounds = snapshot_bounds(model, view);
    const double span_u = bounds[2] - bounds[0];
    const double span_v = bounds[3] - bounds[1];
    if (span_u < span_v) {
        const double padding = (span_v - span_u) * 0.5;
        bounds[0] -= padding;
        bounds[2] += padding;
    } else {
        const double padding = (span_u - span_v) * 0.5;
        bounds[1] -= padding;
        bounds[3] += padding;
    }
    return bounds;
}

[[nodiscard]] auto rasterize_vision_image(const cad::Model& model,
                                          const std::vector<std::string>& bodies,
                                          const SnapshotView& view) -> VisionRaster {
    const auto bounds = square_snapshot_bounds(model, view);
    const double minimum_u = bounds[0];
    const double minimum_v = bounds[1];
    const double cell_u = (bounds[2] - minimum_u) / static_cast<double>(vision_image_width);
    const double cell_v = (bounds[3] - minimum_v) / static_cast<double>(vision_image_height);
    std::vector<std::uint8_t> body_pixels(vision_image_width * vision_image_height, 0U);
    std::vector<double> depths(vision_image_width * vision_image_height,
                               std::numeric_limits<double>::lowest());
    std::unordered_map<std::string_view, std::size_t> body_indices;
    body_indices.reserve(bodies.size());
    for (std::size_t index = 0; index < bodies.size(); ++index)
        body_indices.emplace(bodies[index], index);

    for (const auto& part : model.parts) {
        const auto body = body_indices.find(part.body);
        if (body == body_indices.end())
            continue;
        const auto palette_index = static_cast<std::uint8_t>(2U + body->second % 14U);
        for (const auto& triangle : part.triangles) {
            const auto first = project_point(part.vertices[triangle[0]], view);
            const auto second = project_point(part.vertices[triangle[1]], view);
            const auto third = project_point(part.vertices[triangle[2]], view);
            const double triangle_min_u = std::min({first.u, second.u, third.u});
            const double triangle_max_u = std::max({first.u, second.u, third.u});
            const double triangle_min_v = std::min({first.v, second.v, third.v});
            const double triangle_max_v = std::max({first.v, second.v, third.v});
            const auto min_column = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_min_u - minimum_u) / cell_u), 0.0,
                static_cast<double>(vision_image_width - 1U)));
            const auto max_column = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_max_u - minimum_u) / cell_u), 0.0,
                static_cast<double>(vision_image_width - 1U)));
            const auto min_row_from_bottom = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_min_v - minimum_v) / cell_v), 0.0,
                static_cast<double>(vision_image_height - 1U)));
            const auto max_row_from_bottom = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_max_v - minimum_v) / cell_v), 0.0,
                static_cast<double>(vision_image_height - 1U)));
            for (std::size_t row_from_bottom = min_row_from_bottom;
                 row_from_bottom <= max_row_from_bottom; ++row_from_bottom) {
                for (std::size_t column = min_column; column <= max_column; ++column) {
                    const double u = minimum_u + (static_cast<double>(column) + 0.5) * cell_u;
                    const double v = minimum_v +
                                     (static_cast<double>(row_from_bottom) + 0.5) * cell_v;
                    std::array<double, 3> weights{};
                    if (!inside_triangle(u, v, first, second, third, weights))
                        continue;
                    const double depth = weights[0] * first.depth + weights[1] * second.depth +
                                         weights[2] * third.depth;
                    const std::size_t row = vision_image_height - row_from_bottom - 1U;
                    const std::size_t pixel = row * vision_image_width + column;
                    if (depth >= depths[pixel]) {
                        depths[pixel] = depth;
                        body_pixels[pixel] = palette_index;
                    }
                }
            }
        }
    }

    auto pixels = body_pixels;
    for (std::size_t row = 0; row < vision_image_height; ++row) {
        for (std::size_t column = 0; column < vision_image_width; ++column) {
            const std::size_t pixel = row * vision_image_width + column;
            if (body_pixels[pixel] == 0U)
                continue;
            const auto differs = [&](std::size_t neighbor) {
                return body_pixels[neighbor] != body_pixels[pixel];
            };
            if ((row == 0U || differs(pixel - vision_image_width)) ||
                (row + 1U == vision_image_height || differs(pixel + vision_image_width)) ||
                (column == 0U || differs(pixel - 1U)) ||
                (column + 1U == vision_image_width || differs(pixel + 1U))) {
                pixels[pixel] = 1U;
            }
        }
    }
    return {std::move(pixels)};
}

[[nodiscard]] auto vision_png(const VisionRaster& raster) -> std::string {
    std::string raw;
    raw.reserve(vision_image_height * (1U + vision_image_width / 2U));
    for (std::size_t row = 0; row < vision_image_height; ++row) {
        raw.push_back('\0');
        for (std::size_t column = 0; column < vision_image_width; column += 2U) {
            const auto high = raster.pixels[row * vision_image_width + column] & 0x0fU;
            const auto low = raster.pixels[row * vision_image_width + column + 1U] & 0x0fU;
            raw.push_back(static_cast<char>((high << 4U) | low));
        }
    }

    std::string header;
    append_u32_be(header, static_cast<std::uint32_t>(vision_image_width));
    append_u32_be(header, static_cast<std::uint32_t>(vision_image_height));
    header.push_back(static_cast<char>(4));
    header.push_back(static_cast<char>(3));
    header.append(3U, '\0');

    constexpr std::array<std::array<std::uint8_t, 3>, 16> palette{{
        {{246, 249, 252}}, {{20, 31, 43}},  {{0, 171, 209}},  {{0, 194, 154}},
        {{245, 158, 11}},  {{239, 68, 68}}, {{139, 92, 246}}, {{59, 130, 246}},
        {{236, 72, 153}},  {{34, 197, 94}}, {{234, 179, 8}},  {{14, 116, 144}},
        {{190, 24, 93}},   {{124, 58, 237}},{{5, 150, 105}},  {{194, 65, 12}},
    }};
    std::string palette_bytes;
    palette_bytes.reserve(palette.size() * 3U);
    for (const auto& color : palette) {
        palette_bytes.push_back(static_cast<char>(color[0]));
        palette_bytes.push_back(static_cast<char>(color[1]));
        palette_bytes.push_back(static_cast<char>(color[2]));
    }

    std::string png{"\x89PNG\r\n\x1a\n", 8U};
    append_png_chunk(png, "IHDR", header);
    append_png_chunk(png, "PLTE", palette_bytes);
    const auto compressed = stored_zlib(raw);
    append_png_chunk(png, "IDAT", compressed);
    append_png_chunk(png, "IEND", {});
    return png;
}

[[nodiscard]] auto vision_data_url(const cad::Model& model,
                                   const std::vector<std::string>& bodies,
                                   const SnapshotView& view) -> std::string {
    return "data:image/png;base64," +
           base64_encode(vision_png(rasterize_vision_image(model, bodies, view)));
}

[[nodiscard]] auto rasterize_snapshot(const cad::Model& model,
                                      const std::vector<std::string>& bodies,
                                      const SnapshotView& view,
                                      const std::array<double, 4>& bounds) -> SnapshotRaster {
    const double minimum_u = bounds[0];
    const double minimum_v = bounds[1];
    const double maximum_u = bounds[2];
    const double maximum_v = bounds[3];
    const double cell_u = (maximum_u - minimum_u) / static_cast<double>(snapshot_width);
    const double cell_v = (maximum_v - minimum_v) / static_cast<double>(snapshot_height);
    std::vector<char> pixels(snapshot_width * snapshot_height, '.');
    std::vector<double> depths(snapshot_width * snapshot_height,
                               std::numeric_limits<double>::lowest());
    std::unordered_map<std::string_view, std::size_t> body_indices;
    body_indices.reserve(bodies.size());
    for (std::size_t index = 0; index < bodies.size(); ++index)
        body_indices.emplace(bodies[index], index);

    for (const auto& part : model.parts) {
        const auto body = body_indices.find(part.body);
        if (body == body_indices.end())
            continue;
        const auto body_index = body->second;
        if (body_index >= snapshot_symbols.size())
            continue;
        for (const auto& triangle : part.triangles) {
            const auto first = project_point(part.vertices[triangle[0]], view);
            const auto second = project_point(part.vertices[triangle[1]], view);
            const auto third = project_point(part.vertices[triangle[2]], view);
            const double triangle_min_u = std::min({first.u, second.u, third.u});
            const double triangle_max_u = std::max({first.u, second.u, third.u});
            const double triangle_min_v = std::min({first.v, second.v, third.v});
            const double triangle_max_v = std::max({first.v, second.v, third.v});
            const auto min_column = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_min_u - minimum_u) / cell_u), 0.0,
                static_cast<double>(snapshot_width - 1)));
            const auto max_column = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_max_u - minimum_u) / cell_u), 0.0,
                static_cast<double>(snapshot_width - 1)));
            const auto min_row_from_bottom = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_min_v - minimum_v) / cell_v), 0.0,
                static_cast<double>(snapshot_height - 1)));
            const auto max_row_from_bottom = static_cast<std::size_t>(std::clamp(
                std::floor((triangle_max_v - minimum_v) / cell_v), 0.0,
                static_cast<double>(snapshot_height - 1)));
            for (std::size_t row_from_bottom = min_row_from_bottom;
                 row_from_bottom <= max_row_from_bottom; ++row_from_bottom) {
                for (std::size_t column = min_column; column <= max_column; ++column) {
                    const double u = minimum_u + (static_cast<double>(column) + 0.5) * cell_u;
                    const double v = minimum_v +
                                     (static_cast<double>(row_from_bottom) + 0.5) * cell_v;
                    std::array<double, 3> weights{};
                    if (!inside_triangle(u, v, first, second, third, weights))
                        continue;
                    const double depth = weights[0] * first.depth + weights[1] * second.depth +
                                         weights[2] * third.depth;
                    const std::size_t row = snapshot_height - row_from_bottom - 1;
                    const std::size_t pixel = row * snapshot_width + column;
                    if (depth >= depths[pixel]) {
                        depths[pixel] = depth;
                        pixels[pixel] = snapshot_symbols[body_index];
                    }
                }
            }
        }
    }

    const auto occupied = static_cast<std::size_t>(
        std::ranges::count_if(pixels, [](char value) { return value != '.'; }));
    return {bounds, std::move(pixels), occupied};
}

auto write_snapshot_raster(std::ostringstream& output, const SnapshotView& view,
                           const SnapshotRaster& raster) -> void {
    output << "{\"name\":" << quoted(view.name) << ",\"horizontalAxis\":"
           << quoted(view.horizontal_axis) << ",\"verticalAxis\":"
           << quoted(view.vertical_axis) << ",\"projectedBounds\":[" << raster.bounds[0]
           << ',' << raster.bounds[1] << ',' << raster.bounds[2] << ',' << raster.bounds[3]
           << "],\"occupiedCells\":" << raster.occupied_cells << ",\"grid\":{\"width\":"
           << snapshot_width << ",\"height\":" << snapshot_height << ",\"rows\":[";
    for (std::size_t row = 0; row < snapshot_height; ++row) {
        if (row != 0)
            output << ',';
        output << quoted(std::string_view{raster.pixels.data() + row * snapshot_width,
                                          snapshot_width});
    }
    output << "]}}";
}

auto write_snapshot_view(std::ostringstream& output, const cad::Model& model,
                         const std::vector<std::string>& bodies, const SnapshotView& view)
    -> void {
    write_snapshot_raster(output, view,
                          rasterize_snapshot(model, bodies, view, snapshot_bounds(model, view)));
}

struct ComparisonBodySummary {
    std::size_t parts{};
    std::size_t vertices{};
    std::size_t triangles{};
    std::string material;
    std::string material_preset;
    std::array<double, 3> minimum{std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::max()};
    std::array<double, 3> maximum{std::numeric_limits<double>::lowest(),
                                  std::numeric_limits<double>::lowest(),
                                  std::numeric_limits<double>::lowest()};
};

using ComparisonBodies = std::map<std::string, ComparisonBodySummary>;

[[nodiscard]] auto comparison_bodies(const compiler::ir::Project& project,
                                     const cad::Model& model) -> ComparisonBodies {
    ComparisonBodies result;
    std::unordered_map<std::string_view, std::string_view> presets;
    presets.reserve(project.materials.size());
    for (const auto& material : project.materials)
        presets.emplace(material.name, material.preset);
    for (const auto& body : project.bodies) {
        auto& summary = result[body.name];
        summary.material = body.material;
        const auto preset = presets.find(body.material);
        if (preset != presets.end())
            summary.material_preset = preset->second;
    }
    for (const auto& part : model.parts) {
        auto& summary = result[part.body];
        ++summary.parts;
        summary.vertices += part.vertices.size();
        summary.triangles += part.triangles.size();
        if (summary.material.empty())
            summary.material = part.material;
        for (const auto& vertex : part.vertices) {
            summary.minimum[0] = std::min(summary.minimum[0], vertex.x);
            summary.minimum[1] = std::min(summary.minimum[1], vertex.y);
            summary.minimum[2] = std::min(summary.minimum[2], vertex.z);
            summary.maximum[0] = std::max(summary.maximum[0], vertex.x);
            summary.maximum[1] = std::max(summary.maximum[1], vertex.y);
            summary.maximum[2] = std::max(summary.maximum[2], vertex.z);
        }
    }
    return result;
}

[[nodiscard]] auto semantic_role_hint(std::string_view name) -> std::string_view {
    std::string lowered{name};
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (lowered.contains("sensor") || lowered.contains("vision") || lowered.contains("camera"))
        return "sensing";
    if (lowered.contains("vacuum") || lowered.contains("suction") || lowered.contains("cup"))
        return "vacuumEndEffector";
    if (lowered.contains("gear") || lowered.contains("drive"))
        return "transmission";
    if (lowered.contains("grip") || lowered.contains("finger") || lowered.contains("claw") ||
        lowered.contains("linkage"))
        return "mechanicalEndEffector";
    if (lowered.contains("arm") || lowered.contains("link") || lowered.contains("wrist"))
        return "kinematicLink";
    if (lowered.contains("base") || lowered.contains("waist") || lowered.contains("mount"))
        return "support";
    return "structure";
}

[[nodiscard]] auto moving_joint_count(const compiler::ir::Project& project) -> std::size_t {
    return static_cast<std::size_t>(
        std::ranges::count_if(project.joints, [](const auto& joint) {
            return joint.kind != compiler::ir::JointKind::fixed;
        }));
}

[[nodiscard]] auto scene_track_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& scene : project.scenes)
        count += scene.tracks.size();
    return count;
}

[[nodiscard]] auto scene_keyframe_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& scene : project.scenes) {
        for (const auto& track : scene.tracks)
            count += track.keyframes.size();
    }
    return count;
}

auto write_mechanism_summary(std::ostringstream& output,
                             const compiler::ir::Project& project) -> void {
    output << "{\"movingDof\":" << moving_joint_count(project)
           << ",\"rootBodies\":[";
    bool first_root = true;
    for (const auto& joint : project.joints) {
        if (joint.parent_body != "WORLD")
            continue;
        if (!first_root)
            output << ',';
        first_root = false;
        output << quoted(joint.child_body);
    }
    output << "],\"edges\":[";
    for (std::size_t index = 0; index < project.joints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& joint = project.joints[index];
        output << "{\"joint\":" << quoted(joint.name) << ",\"type\":"
               << quoted(joint_kind_name(joint.kind)) << ",\"parent\":"
               << quoted(joint.parent_body) << ",\"child\":" << quoted(joint.child_body)
               << ",\"anchor\":" << quoted(joint.point) << ",\"axis\":"
               << quoted(joint.axis) << ",\"value\":" << joint.value << ",\"limits\":["
               << joint.lower_limit << ',' << joint.upper_limit << "]}";
    }
    output << "]}";
}

[[nodiscard]] auto body_for_pixel(char pixel, const std::vector<std::string>& bodies)
    -> std::string_view {
    const auto index = snapshot_symbols.find(pixel);
    return index == std::string_view::npos || index >= bodies.size() ? std::string_view{}
                                                                     : bodies[index];
}

auto write_view_delta(std::ostringstream& output, const cad::Model& first_model,
                      const cad::Model& second_model) -> void {
    const auto first_bodies = body_names(first_model);
    const auto second_bodies = body_names(second_model);
    output << "{\"representation\":\"shared-world-bounds-cell-diff\","
              "\"legend\":{\".\":\"empty\",\"=\":\"same named body\","
              "\"!\":\"both occupied by different bodies\","
              "\"A\":\"first only\",\"B\":\"second only\"},\"views\":[";
    for (std::size_t view_index = 0; view_index < snapshot_views.size(); ++view_index) {
        if (view_index != 0)
            output << ',';
        const auto& view = snapshot_views[view_index];
        const auto first_bounds = snapshot_bounds(first_model, view);
        const auto second_bounds = snapshot_bounds(second_model, view);
        const std::array shared_bounds{
            std::min(first_bounds[0], second_bounds[0]),
            std::min(first_bounds[1], second_bounds[1]),
            std::max(first_bounds[2], second_bounds[2]),
            std::max(first_bounds[3], second_bounds[3]),
        };
        const auto first = rasterize_snapshot(first_model, first_bodies, view, shared_bounds);
        const auto second = rasterize_snapshot(second_model, second_bodies, view, shared_bounds);
        std::vector<char> difference(snapshot_width * snapshot_height, '.');
        std::size_t first_only = 0;
        std::size_t second_only = 0;
        std::size_t same_body = 0;
        std::size_t different_body = 0;
        for (std::size_t pixel = 0; pixel < difference.size(); ++pixel) {
            const bool first_occupied = first.pixels[pixel] != '.';
            const bool second_occupied = second.pixels[pixel] != '.';
            if (first_occupied && !second_occupied) {
                difference[pixel] = 'A';
                ++first_only;
            } else if (!first_occupied && second_occupied) {
                difference[pixel] = 'B';
                ++second_only;
            } else if (first_occupied) {
                const auto first_body = body_for_pixel(first.pixels[pixel], first_bodies);
                const auto second_body = body_for_pixel(second.pixels[pixel], second_bodies);
                if (first_body == second_body) {
                    difference[pixel] = '=';
                    ++same_body;
                } else {
                    difference[pixel] = '!';
                    ++different_body;
                }
            }
        }
        const std::size_t intersection = same_body + different_body;
        const std::size_t union_cells = intersection + first_only + second_only;
        const double silhouette_iou = union_cells == 0
                                          ? 1.0
                                          : static_cast<double>(intersection) /
                                                static_cast<double>(union_cells);
        const double identity_agreement = intersection == 0
                                              ? 1.0
                                              : static_cast<double>(same_body) /
                                                    static_cast<double>(intersection);
        const double changed_fraction = union_cells == 0
                                            ? 0.0
                                            : static_cast<double>(first_only + second_only +
                                                                  different_body) /
                                                  static_cast<double>(union_cells);
        output << "{\"name\":" << quoted(view.name) << ",\"sharedProjectedBounds\":["
               << shared_bounds[0] << ',' << shared_bounds[1] << ',' << shared_bounds[2] << ','
               << shared_bounds[3] << "],\"cells\":{\"firstOccupied\":"
               << first.occupied_cells << ",\"secondOccupied\":" << second.occupied_cells
               << ",\"firstOnly\":" << first_only << ",\"secondOnly\":" << second_only
               << ",\"sameBody\":" << same_body << ",\"differentBody\":"
               << different_body << "},\"silhouetteIntersectionOverUnion\":"
               << silhouette_iou << ",\"bodyIdentityAgreement\":" << identity_agreement
               << ",\"changedFraction\":" << changed_fraction
               << ",\"differenceGrid\":{\"width\":" << snapshot_width
               << ",\"height\":" << snapshot_height << ",\"rows\":[";
        for (std::size_t row = 0; row < snapshot_height; ++row) {
            if (row != 0)
                output << ',';
            output << quoted(std::string_view{difference.data() + row * snapshot_width,
                                              snapshot_width});
        }
        output << "]}}";
    }
    output << "]}";
}

[[nodiscard]] auto subtract(const cad::Point3& first, const cad::Point3& second) -> cad::Point3 {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

[[nodiscard]] auto add(const cad::Point3& first, const cad::Point3& second) -> cad::Point3 {
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

[[nodiscard]] auto scale(const cad::Point3& point, double amount) -> cad::Point3 {
    return {point.x * amount, point.y * amount, point.z * amount};
}

[[nodiscard]] auto dot(const cad::Point3& first, const cad::Point3& second) -> double {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] auto cross(const cad::Point3& first, const cad::Point3& second) -> cad::Point3 {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

[[nodiscard]] auto point_triangle_distance(const cad::Point3& point, const cad::Point3& a,
                                           const cad::Point3& b, const cad::Point3& c) -> double {
    const auto ab = subtract(b, a);
    const auto ac = subtract(c, a);
    const auto ap = subtract(point, a);
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
        return std::sqrt(dot(ap, ap));

    const auto bp = subtract(point, b);
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
        return std::sqrt(dot(bp, bp));

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const auto projection = add(a, scale(ab, d1 / (d1 - d3)));
        const auto delta = subtract(point, projection);
        return std::sqrt(dot(delta, delta));
    }

    const auto cp = subtract(point, c);
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return std::sqrt(dot(cp, cp));

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const auto projection = add(a, scale(ac, d2 / (d2 - d6)));
        const auto delta = subtract(point, projection);
        return std::sqrt(dot(delta, delta));
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
        const auto edge = subtract(c, b);
        const auto projection = add(b, scale(edge, (d4 - d3) / ((d4 - d3) + (d5 - d6))));
        const auto delta = subtract(point, projection);
        return std::sqrt(dot(delta, delta));
    }

    const auto normal = cross(ab, ac);
    const double normal_length = std::sqrt(dot(normal, normal));
    return normal_length <= 1.0e-15 ? std::numeric_limits<double>::max()
                                   : std::abs(dot(ap, normal)) / normal_length;
}

[[nodiscard]] auto ray_triangle_hit(const cad::Point3& origin, const cad::Point3& direction,
                                    const cad::Point3& a, const cad::Point3& b,
                                    const cad::Point3& c) -> std::optional<double> {
    constexpr double epsilon = 1.0e-10;
    const auto first_edge = subtract(b, a);
    const auto second_edge = subtract(c, a);
    const auto h = cross(direction, second_edge);
    const double determinant = dot(first_edge, h);
    if (std::abs(determinant) <= epsilon)
        return std::nullopt;
    const double inverse = 1.0 / determinant;
    const auto s = subtract(origin, a);
    const double u = inverse * dot(s, h);
    if (u < -epsilon || u > 1.0 + epsilon)
        return std::nullopt;
    const auto q = cross(s, first_edge);
    const double v = inverse * dot(direction, q);
    if (v < -epsilon || u + v > 1.0 + epsilon)
        return std::nullopt;
    const double distance = inverse * dot(second_edge, q);
    return distance > epsilon ? std::optional<double>{distance} : std::nullopt;
}

[[nodiscard]] auto point_inside_part(const cad::Part& part, const cad::Point3& point) -> bool {
    const cad::Point3 direction{0.8192319205190405, 0.3047588739149585,
                                0.4853216186274828};
    std::vector<double> hits;
    for (const auto& triangle : part.triangles) {
        const auto hit = ray_triangle_hit(point, direction, part.vertices[triangle[0]],
                                          part.vertices[triangle[1]], part.vertices[triangle[2]]);
        if (hit)
            hits.push_back(*hit);
    }
    std::ranges::sort(hits);
    std::size_t unique_hits = 0;
    double previous = std::numeric_limits<double>::lowest();
    for (const double hit : hits) {
        if (unique_hits == 0 || std::abs(hit - previous) > 1.0e-7) {
            ++unique_hits;
            previous = hit;
        }
    }
    return unique_hits % 2 == 1;
}

[[nodiscard]] auto body_geometry_gap(const cad::Model& model, std::string_view body,
                                     const std::array<double, 3>& position,
                                     double tolerance) -> std::optional<double> {
    const cad::Point3 point{position[0], position[1], position[2]};
    double minimum = std::numeric_limits<double>::max();
    bool found = false;
    for (const auto& part : model.parts) {
        if (part.body != body)
            continue;
        found = true;
        double surface_distance = std::numeric_limits<double>::max();
        for (const auto& triangle : part.triangles) {
            surface_distance = std::min(
                surface_distance,
                point_triangle_distance(point, part.vertices[triangle[0]],
                                        part.vertices[triangle[1]], part.vertices[triangle[2]]));
        }
        if (surface_distance <= tolerance || point_inside_part(part, point))
            return 0.0;
        minimum = std::min(minimum, surface_distance);
    }
    return found ? std::optional<double>{minimum} : std::nullopt;
}

auto write_string_array(std::ostringstream& output, const std::vector<std::string>& values)
    -> void {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            output << ',';
        output << quoted(values[index]);
    }
    output << ']';
}

auto write_comparison_design(std::ostringstream& output, const compiler::ir::Project& project,
                             const cad::Model& model, const cad::TopologyModel& topology,
                             const ComparisonBodies& bodies) -> void {
    const auto analysis = cad::analyze(project);
    std::map<std::string_view, std::size_t> role_histogram;
    for (const auto& [name, body] : bodies) {
        static_cast<void>(body);
        ++role_histogram[semantic_role_hint(name)];
    }
    output << "{\"project\":" << quoted(project.name) << ",\"revision\":"
           << quoted(document::revision_id(document::fingerprint(project)))
           << ",\"counts\":{\"bodies\":" << project.bodies.size()
           << ",\"features\":" << feature_count(project) << ",\"parts\":"
           << model.parts.size() << ",\"vertices\":" << model.vertex_count()
           << ",\"triangles\":" << model.triangle_count() << ",\"solids\":"
           << topology.solids.size() << ",\"topologyVertices\":"
           << topology.vertex_count() << ",\"topologyEdges\":" << topology.edge_count()
           << ",\"topologyFaces\":" << topology.face_count() << ",\"joints\":"
           << project.joints.size() << ",\"movingDof\":" << moving_joint_count(project)
           << ",\"materials\":" << project.materials.size() << ",\"scenes\":"
           << project.scenes.size() << "},\"boundsMm\":{\"minimum\":["
           << analysis.bounds.minimum[0] << ',' << analysis.bounds.minimum[1] << ','
           << analysis.bounds.minimum[2] << "],\"maximum\":[" << analysis.bounds.maximum[0]
           << ',' << analysis.bounds.maximum[1] << ',' << analysis.bounds.maximum[2]
           << "],\"size\":[" << analysis.bounds.maximum[0] - analysis.bounds.minimum[0] << ','
           << analysis.bounds.maximum[1] - analysis.bounds.minimum[1] << ','
           << analysis.bounds.maximum[2] - analysis.bounds.minimum[2]
           << "],\"envelopeVolumeMm3\":"
           << (analysis.bounds.maximum[0] - analysis.bounds.minimum[0]) *
                  (analysis.bounds.maximum[1] - analysis.bounds.minimum[1]) *
                  (analysis.bounds.maximum[2] - analysis.bounds.minimum[2])
           << "},\"materials\":[";
    for (std::size_t index = 0; index < project.materials.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& material = project.materials[index];
        output << "{\"name\":" << quoted(material.name) << ",\"profile\":"
               << quoted(material.profile) << ",\"preset\":"
               << quoted(material.preset) << ",\"texture\":" << quoted(material.texture)
               << '}';
    }
    output << "],\"scenes\":[";
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& scene = project.scenes[index];
        std::size_t keyframes = 0;
        for (const auto& track : scene.tracks)
            keyframes += track.keyframes.size();
        output << "{\"name\":" << quoted(scene.name) << ",\"durationSeconds\":"
               << scene.duration_seconds << ",\"fps\":" << scene.frames_per_second
               << ",\"tracks\":" << scene.tracks.size() << ",\"keyframes\":"
               << keyframes << '}';
    }
    output << "],\"bodies\":[";
    std::size_t index = 0;
    for (const auto& [name, body] : bodies) {
        if (index++ != 0)
            output << ',';
        const std::array center{
            (body.minimum[0] + body.maximum[0]) * 0.5,
            (body.minimum[1] + body.maximum[1]) * 0.5,
            (body.minimum[2] + body.maximum[2]) * 0.5,
        };
        output << "{\"name\":" << quoted(name) << ",\"semanticRoleHint\":"
               << quoted(semantic_role_hint(name)) << ",\"parts\":" << body.parts
               << ",\"vertices\":" << body.vertices << ",\"triangles\":"
               << body.triangles << ",\"material\":" << quoted(body.material)
               << ",\"materialPreset\":" << quoted(body.material_preset)
               << ",\"boundsMm\":{\"minimum\":[" << body.minimum[0] << ','
               << body.minimum[1] << ',' << body.minimum[2] << "],\"maximum\":["
               << body.maximum[0] << ',' << body.maximum[1] << ',' << body.maximum[2]
               << "],\"center\":[" << center[0] << ',' << center[1] << ',' << center[2]
               << "],\"size\":[" << body.maximum[0] - body.minimum[0] << ','
               << body.maximum[1] - body.minimum[1] << ','
               << body.maximum[2] - body.minimum[2] << "]}}";
    }
    output << "],\"architecture\":{\"roleHintsAreHeuristic\":true,\"roleHistogram\":{";
    index = 0;
    for (const auto& [role, count] : role_histogram) {
        if (index++ != 0)
            output << ',';
        output << quoted(role) << ':' << count;
    }
    output << "},\"mechanism\":";
    write_mechanism_summary(output, project);
    output << "},\"visual\":" << visual_snapshot_json(project) << '}';
}

auto write_optimization_matrix(std::ostringstream& output,
                               const compiler::ir::Project& first,
                               const compiler::ir::Project& second,
                               const cad::Model& first_model,
                               const cad::Model& second_model,
                               const cad::TopologyModel& first_topology,
                               const cad::TopologyModel& second_topology) -> void {
    const auto first_analysis = cad::analyze(first);
    const auto second_analysis = cad::analyze(second);
    const auto envelope_volume = [](const cad::ProjectAnalysis& analysis) {
        return (analysis.bounds.maximum[0] - analysis.bounds.minimum[0]) *
               (analysis.bounds.maximum[1] - analysis.bounds.minimum[1]) *
               (analysis.bounds.maximum[2] - analysis.bounds.minimum[2]);
    };
    bool first_metric = true;
    const auto write_metric = [&](std::string_view metric, std::string_view unit,
                                  std::string_view goal, double first_value,
                                  double second_value) {
        if (!first_metric)
            output << ',';
        first_metric = false;
        output << "{\"metric\":" << quoted(metric) << ",\"unit\":" << quoted(unit)
               << ",\"goal\":" << quoted(goal) << ",\"first\":" << first_value
               << ",\"second\":" << second_value << ",\"deltaSecondMinusFirst\":"
               << second_value - first_value << '}';
    };
    output << '[';
    write_metric("triangleCount", "triangles", "minimizeWhenFunctionallyEquivalent",
                 static_cast<double>(first_model.triangle_count()),
                 static_cast<double>(second_model.triangle_count()));
    write_metric("topologyFaceCount", "faces", "minimizeWhenFunctionallyEquivalent",
                 static_cast<double>(first_topology.face_count()),
                 static_cast<double>(second_topology.face_count()));
    write_metric("envelopeVolume", "mm3", "minimizeWhenWorkspaceIsFixed",
                 envelope_volume(first_analysis), envelope_volume(second_analysis));
    write_metric("materialDiversity", "materials", "intentDependent",
                 static_cast<double>(first.materials.size()),
                 static_cast<double>(second.materials.size()));
    write_metric("movingDof", "joints", "intentDependent",
                 static_cast<double>(moving_joint_count(first)),
                 static_cast<double>(moving_joint_count(second)));
    write_metric("sceneTracks", "tracks", "maximizeUsefulCoverage",
                 static_cast<double>(scene_track_count(first)),
                 static_cast<double>(scene_track_count(second)));
    write_metric("sceneKeyframes", "keyframes", "maximizeUsefulCoverage",
                 static_cast<double>(scene_keyframe_count(first)),
                 static_cast<double>(scene_keyframe_count(second)));
    output << ']';
}

} // namespace

auto visual_snapshot_json(const compiler::ir::Project& project) -> std::string {
    const auto model = cad::build_model(project);
    const auto analysis = cad::analyze(project, model);
    const auto constraint_results = constraints::validate(project, analysis);
    const auto intersections =
        cad::analyze_intersections(project, model, project.tolerance.linear_mm);
    const auto manufacturing_report =
        manufacturing::validate(project, analysis, intersections);
    const auto bodies = body_names(model);
    struct BodySummary {
        std::size_t parts{};
        std::size_t triangles{};
        std::array<double, 3> minimum{std::numeric_limits<double>::max(),
                                      std::numeric_limits<double>::max(),
                                      std::numeric_limits<double>::max()};
        std::array<double, 3> maximum{std::numeric_limits<double>::lowest(),
                                      std::numeric_limits<double>::lowest(),
                                      std::numeric_limits<double>::lowest()};
    };
    std::vector<BodySummary> summaries(bodies.size());
    std::unordered_map<std::string_view, std::size_t> body_indices;
    body_indices.reserve(bodies.size());
    for (std::size_t index = 0; index < bodies.size(); ++index)
        body_indices.emplace(bodies[index], index);
    for (const auto& part : model.parts) {
        const auto found = body_indices.find(part.body);
        if (found == body_indices.end())
            continue;
        auto& summary = summaries[found->second];
        ++summary.parts;
        summary.triangles += part.triangles.size();
        for (const auto& vertex : part.vertices) {
            summary.minimum[0] = std::min(summary.minimum[0], vertex.x);
            summary.minimum[1] = std::min(summary.minimum[1], vertex.y);
            summary.minimum[2] = std::min(summary.minimum[2], vertex.z);
            summary.maximum[0] = std::max(summary.maximum[0], vertex.x);
            summary.maximum[1] = std::max(summary.maximum[1], vertex.y);
            summary.maximum[2] = std::max(summary.maximum[2], vertex.z);
        }
    }
    const auto spatial_point = [&](std::string_view name) -> const compiler::ir::SpatialPoint* {
        const auto found =
            std::ranges::find(project.points, name, &compiler::ir::SpatialPoint::name);
        return found == project.points.end() ? nullptr : &*found;
    };
    const double attachment_tolerance = std::max(project.tolerance.linear_mm, 1.0e-6);
    std::size_t checked_attachments = 0;
    std::size_t disconnected_joints = 0;
    for (const auto& joint : project.joints) {
        const auto* anchor = spatial_point(joint.point);
        const auto child_gap = anchor == nullptr
                                   ? std::optional<double>{}
                                   : body_geometry_gap(model, joint.child_body,
                                                       anchor->position_mm, attachment_tolerance);
        const auto parent_gap = joint.parent_body == "WORLD"
                                    ? std::optional<double>{0.0}
                                : anchor == nullptr
                                    ? std::optional<double>{}
                                    : body_geometry_gap(model, joint.parent_body,
                                                        anchor->position_mm,
                                                        attachment_tolerance);
        const double child_gap_mm = child_gap.value_or(0.0);
        const double parent_gap_mm = parent_gap.value_or(0.0);
        if (!child_gap || !parent_gap) {
            ++disconnected_joints;
            continue;
        }
        if (joint.parent_body != "WORLD")
            ++checked_attachments;
        if (child_gap_mm > attachment_tolerance || parent_gap_mm > attachment_tolerance)
            ++disconnected_joints;
    }
    constexpr std::string_view symbols =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"schema\":\"icad.visual.snapshot.v1\",\"project\":"
           << quoted(project.name) << ",\"revision\":"
           << quoted(document::revision_id(document::fingerprint(project)))
           << ",\"representation\":\"lossless-orthographic-png+deterministic-depth-raster\""
              ",\"imageOrder\":[\"front\",\"right\",\"top\"]"
              ",\"imageSize\":{\"width\":512,\"height\":512},\"imageMime\":\"image/png\""
              ",\"images\":[";
    for (std::size_t index = 0; index < 3U; ++index) {
        if (index != 0U)
            output << ',';
        output << "{\"type\":\"image_url\",\"image_url\":{\"url\":"
               << quoted(vision_data_url(model, bodies, snapshot_views[index]))
               << ",\"detail\":\"low\"}}";
    }
    output << "],\"legend\":[";
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& summary = summaries[index];
        output << "{\"symbol\":";
        if (index < symbols.size())
            output << quoted(std::string_view{&symbols[index], 1});
        else
            output << "null";
        output << ",\"body\":" << quoted(bodies[index]) << ",\"parts\":" << summary.parts
               << ",\"triangles\":" << summary.triangles << ",\"boundsMinMm\":["
               << summary.minimum[0] << ',' << summary.minimum[1] << ',' << summary.minimum[2]
               << "],\"boundsMaxMm\":[" << summary.maximum[0] << ',' << summary.maximum[1]
               << ',' << summary.maximum[2] << "]}";
    }
    output << "],\"truncatedBodies\":"
           << (bodies.size() > symbols.size() ? bodies.size() - symbols.size() : 0)
           << ",\"views\":[";
    for (std::size_t index = 0; index < snapshot_views.size(); ++index) {
        if (index != 0)
            output << ',';
        write_snapshot_view(output, model, bodies, snapshot_views[index]);
    }
    output << "],\"parameters\":[";
    for (std::size_t index = 0; index < project.parameters.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& parameter = project.parameters[index];
        output << "{\"name\":" << quoted(parameter.name) << ",\"value\":"
               << parameter.value.value << ",\"unit\":" << quoted(parameter.value.unit)
               << ",\"expression\":" << quoted(parameter.expression)
               << ",\"dependencies\":[";
        for (std::size_t dependency = 0; dependency < parameter.dependencies.size();
             ++dependency) {
            if (dependency != 0)
                output << ',';
            output << quoted(parameter.dependencies[dependency]);
        }
        output << "]}";
    }
    output << "],\"sketches\":[";
    for (std::size_t sketch_index = 0; sketch_index < project.sketches.size(); ++sketch_index) {
        if (sketch_index != 0)
            output << ',';
        const auto& sketch = project.sketches[sketch_index];
        output << "{\"name\":" << quoted(sketch.name) << ",\"status\":"
               << quoted(sketch_status_name(sketch.status)) << ",\"degreesOfFreedom\":"
               << sketch.degrees_of_freedom << ",\"solveRequirement\":"
               << quoted(sketch.solve_requirement) << ",\"shapes\":[";
        for (std::size_t shape_index = 0; shape_index < sketch.shapes.size(); ++shape_index) {
            if (shape_index != 0)
                output << ',';
            const auto& shape = sketch.shapes[shape_index];
            output << "{\"name\":" << quoted(shape.name) << ",\"role\":"
                   << quoted(shape.role) << ",\"closed\":"
                   << (shape.closed ? "true" : "false") << ",\"profile\":"
                   << quoted(shape.profile) << ",\"areaMm2\":" << shape.area_mm2;
            if (!shape.containing_shape.empty())
                output << ",\"containedBy\":" << quoted(shape.containing_shape);
            output << '}';
        }
        output << "],\"regions\":[";
        for (std::size_t region_index = 0; region_index < sketch.regions.size(); ++region_index) {
            if (region_index != 0)
                output << ',';
            const auto& region = sketch.regions[region_index];
            output << "{\"name\":" << quoted(region.name) << ",\"outer\":"
                   << quoted(region.outer_shape) << ",\"holes\":[";
            for (std::size_t hole = 0; hole < region.hole_shapes.size(); ++hole) {
                if (hole != 0)
                    output << ',';
                output << quoted(region.hole_shapes[hole]);
            }
            output << "],\"areaMm2\":" << region.area_mm2 << '}';
        }
        output << "],\"entities\":[";
        for (std::size_t entity_index = 0; entity_index < sketch.entities.size();
             ++entity_index) {
            if (entity_index != 0)
                output << ',';
            const auto& entity = sketch.entities[entity_index];
            output << "{\"name\":" << quoted(entity.name) << ",\"type\":"
                   << quoted(entity.full_circle
                                 ? "CIRCLE"
                             : entity.kind == compiler::ir::ProfileSegmentKind::circular_arc
                                 ? "ARC"
                                 : "LINE")
                   << '}';
        }
        output << "],\"constraints\":[";
        for (std::size_t constraint_index = 0;
             constraint_index < sketch.constraints.size(); ++constraint_index) {
            if (constraint_index != 0)
                output << ',';
            const auto& constraint = sketch.constraints[constraint_index];
            output << "{\"name\":" << quoted(constraint.name)
                   << ",\"type\":" << quoted(constraint.kind)
                   << ",\"references\":[";
            for (std::size_t reference = 0;
                 reference < constraint.references.size(); ++reference) {
                if (reference != 0)
                    output << ',';
                output << quoted(constraint.references[reference]);
            }
            output << "]}";
        }
        output << "]}";
    }
    output << "],\"featureHistory\":[";
    for (std::size_t body_index = 0; body_index < project.bodies.size(); ++body_index) {
        if (body_index != 0)
            output << ',';
        const auto& body = project.bodies[body_index];
        output << "{\"body\":" << quoted(body.name) << ",\"steps\":[";
        for (std::size_t feature_index = 0; feature_index < body.features.size(); ++feature_index) {
            if (feature_index != 0)
                output << ',';
            const auto& feature = body.features[feature_index];
            const auto operation = feature.operation == compiler::ir::FeatureOperation::create
                                       ? "NEW"
                                   : feature.operation == compiler::ir::FeatureOperation::unite
                                       ? "ADD"
                                   : feature.operation == compiler::ir::FeatureOperation::cut
                                       ? "CUT"
                                       : "INTERSECT";
            output << "{\"name\":" << quoted(feature.name) << ",\"command\":"
                   << quoted(feature.source_keyword) << ",\"type\":" << quoted(feature.type)
                   << ",\"operation\":" << quoted(operation);
            if (!feature.profile.empty()) {
                const auto separator = feature.profile.rfind("::");
                const auto sketch_name = separator == std::string::npos
                                             ? std::string_view{feature.profile}
                                             : std::string_view{feature.profile}.substr(separator + 2);
                output << ",\"sketch\":" << quoted(sketch_name)
                       << ",\"sketchId\":" << quoted(feature.profile)
                       << ",\"plane\":" << quoted(feature.sketch_plane);
            }
            if (!feature.region.empty())
                output << ",\"region\":" << quoted(feature.region)
                       << ",\"regionHoleProfiles\":"
                       << feature.region_hole_profiles.size();
            if (!feature.support_feature.empty())
                output << ",\"supportFeature\":" << quoted(feature.support_feature)
                       << ",\"supportFace\":" << quoted(feature.support_face);
            if (!feature.support_reference.empty())
                output << ",\"supportReference\":" << quoted(feature.support_reference);
            if (!feature.support_topology_id.empty())
                output << ",\"supportTopologyId\":" << quoted(feature.support_topology_id);
            if (!feature.selected_edge_location.empty())
                output << ",\"selection\":{\"kind\":\"EDGE_LOOP\",\"location\":"
                       << quoted(feature.selected_edge_location)
                       << ",\"classification\":"
                       << quoted(feature.selected_edge_classification)
                       << ",\"applicableOperations\":[\"FILLET\",\"CHAMFER\"]";
            if (!feature.selected_edge_location.empty()) {
                if (!feature.selected_edge_set.empty())
                    output << ",\"reference\":" << quoted(feature.selected_edge_set);
                if (!feature.selected_topology_id.empty())
                    output << ",\"topologyId\":" << quoted(feature.selected_topology_id);
                output << '}';
            }
            output << '}';
        }
        output << "],\"faceReferences\":[";
        for (std::size_t reference_index = 0;
             reference_index < body.face_references.size(); ++reference_index) {
            if (reference_index != 0)
                output << ',';
            const auto& reference = body.face_references[reference_index];
            output << "{\"name\":" << quoted(reference.name)
                   << ",\"feature\":" << quoted(reference.feature)
                   << ",\"role\":" << quoted(reference.role)
                   << ",\"topologyId\":" << quoted(reference.topology_id) << '}';
        }
        output << "],\"topologySelections\":[";
        for (std::size_t selection_index = 0;
             selection_index < body.topology_selections.size(); ++selection_index) {
            if (selection_index != 0)
                output << ',';
            write_topology_selection(output, body.topology_selections[selection_index]);
        }
        output << "]}";
    }
    output << "],\"attachmentSummary\":{\"checkedJoints\":" << checked_attachments
           << ",\"disconnectedJoints\":" << disconnected_joints
           << ",\"toleranceMm\":" << attachment_tolerance << "},\"joints\":[";
    for (std::size_t index = 0; index < project.joints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& joint = project.joints[index];
        const auto* anchor = spatial_point(joint.point);
        const auto child_gap = anchor == nullptr
                                   ? std::optional<double>{}
                                   : body_geometry_gap(model, joint.child_body,
                                                       anchor->position_mm, attachment_tolerance);
        const auto parent_gap = joint.parent_body == "WORLD"
                                    ? std::optional<double>{0.0}
                                : anchor == nullptr
                                    ? std::optional<double>{}
                                    : body_geometry_gap(model, joint.parent_body,
                                                        anchor->position_mm,
                                                        attachment_tolerance);
        const double child_gap_mm = child_gap.value_or(0.0);
        const double parent_gap_mm = parent_gap.value_or(0.0);
        output << "{\"name\":" << quoted(joint.name) << ",\"parent\":"
               << quoted(joint.parent_body) << ",\"child\":" << quoted(joint.child_body)
               << ",\"type\":" << quoted(joint_kind_name(joint.kind)) << ",\"point\":"
               << quoted(joint.point) << ",\"axis\":" << quoted(joint.axis)
               << ",\"value\":" << joint.value << ",\"limits\":[" << joint.lower_limit
               << ',' << joint.upper_limit << "],\"attachment\":{";
        if (!child_gap || !parent_gap) {
            output << "\"resolved\":false,\"connected\":false";
        } else {
            output << "\"resolved\":true,\"parentGapMm\":";
            if (joint.parent_body == "WORLD")
                output << "null";
            else
                output << parent_gap_mm;
            output << ",\"childGapMm\":" << child_gap_mm
                   << ",\"connected\":"
                   << (parent_gap_mm <= attachment_tolerance &&
                               child_gap_mm <= attachment_tolerance
                           ? "true"
                           : "false");
        }
        output << "}}";
    }
    output << "],\"interfaces\":[";
    for (std::size_t index = 0; index < project.interfaces.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& interface = project.interfaces[index];
        const bool attachment_valid = std::ranges::none_of(
            manufacturing_report.issues, [&](const auto& issue) {
                return issue.subject == interface.name &&
                       issue.severity == manufacturing::Severity::error;
            });
        output << "{\"name\":" << quoted(interface.name)
               << ",\"occurrence\":" << quoted(interface.occurrence)
               << ",\"point\":" << quoted(interface.point)
               << ",\"axis\":" << quoted(interface.axis)
               << ",\"type\":" << quoted(interface.kind)
               << ",\"attachmentValid\":" << (attachment_valid ? "true" : "false");
        if (interface.has_size)
            output << ",\"sizeMm\":" << interface.size_mm;
        output << '}';
    }
    output << "],\"connections\":[";
    for (std::size_t index = 0; index < project.connections.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& connection = project.connections[index];
        const auto first_interface = std::ranges::find(
            project.interfaces, connection.first_interface,
            &compiler::ir::ComponentInterface::name);
        const auto second_interface = std::ranges::find(
            project.interfaces, connection.second_interface,
            &compiler::ir::ComponentInterface::name);
        const bool engineering_valid = std::ranges::none_of(
            manufacturing_report.issues, [&](const auto& issue) {
                if (issue.severity != manufacturing::Severity::error)
                    return false;
                return issue.subject == connection.name ||
                       (first_interface != project.interfaces.end() &&
                        issue.subject == first_interface->name) ||
                       (second_interface != project.interfaces.end() &&
                        issue.subject == second_interface->name);
            });
        const auto snap_state = !connection.aligned
                                    ? (connection.automatic ? "SNAP_REQUIRED" : "MISALIGNED")
                                : engineering_valid ? "SEATED"
                                                    : "INVALID_GEOMETRY";
        output << "{\"name\":" << quoted(connection.name)
               << ",\"interfaces\":[" << quoted(connection.first_interface) << ','
               << quoted(connection.second_interface) << ']'
               << ",\"method\":" << quoted(connection.method)
               << ",\"standard\":" << quoted(connection.standard)
               << ",\"fastener\":" << quoted(connection.fastener)
               << ",\"fit\":" << quoted(connection.fit)
               << ",\"clearanceMm\":" << connection.clearance_mm
               << ",\"gapMm\":" << connection.interface_gap_mm
               << ",\"axisAlignment\":" << connection.axis_alignment
               << ",\"automatic\":" << (connection.automatic ? "true" : "false")
               << ",\"aligned\":" << (connection.aligned ? "true" : "false")
               << ",\"engineeringValid\":" << (engineering_valid ? "true" : "false")
               << ",\"snapState\":" << quoted(snap_state)
               << '}';
    }
    output << "],\"engineering\":{\"constraintsPassed\":"
           << (constraints::all_passed(constraint_results) ? "true" : "false")
           << ",\"manufacturingPassed\":" << (manufacturing_report.passed ? "true" : "false")
           << ",\"penetratingPartPairs\":" << intersections.penetrating_part_pairs
           << ",\"issues\":[";
    for (std::size_t index = 0; index < manufacturing_report.issues.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& issue = manufacturing_report.issues[index];
        output << "{\"code\":" << quoted(issue.code)
               << ",\"subject\":" << quoted(issue.subject)
               << ",\"message\":" << quoted(issue.message) << '}';
    }
    output << "]},\"sceneSamples\":[";
    bool first_sample = true;
    for (const auto& scene_value : project.scenes) {
        const std::array sample_times{0.0, scene_value.duration_seconds * 0.5,
                                      scene_value.duration_seconds};
        for (const double time : sample_times) {
            if (!first_sample)
                output << ',';
            first_sample = false;
            const auto values = scene::joint_values_at(scene_value, time);
            const auto sampled_model = scene::sample_model(project, model, scene_value, time);
            std::size_t disconnected = 0;
            for (const auto& joint : project.joints) {
                const auto* anchor_source = spatial_point(joint.point);
                if (anchor_source == nullptr) {
                    ++disconnected;
                    continue;
                }
                cad::Point3 anchor{anchor_source->position_mm[0], anchor_source->position_mm[1],
                                   anchor_source->position_mm[2]};
                if (joint.parent_body != "WORLD")
                    anchor = scene::transform_joint_point(project, joint.parent_body, anchor,
                                                          values);
                const std::array position{anchor.x, anchor.y, anchor.z};
                const auto child_gap = body_geometry_gap(sampled_model, joint.child_body,
                                                         position, attachment_tolerance);
                const auto parent_gap = joint.parent_body == "WORLD"
                                            ? std::optional<double>{0.0}
                                            : body_geometry_gap(sampled_model, joint.parent_body,
                                                                position,
                                                                attachment_tolerance);
                if (!child_gap || !parent_gap ||
                    child_gap.value_or(0.0) > attachment_tolerance ||
                    parent_gap.value_or(0.0) > attachment_tolerance)
                    ++disconnected;
            }
            double root_displacement = 0.0;
            for (const auto& root_joint : project.joints) {
                if (root_joint.parent_body != "WORLD")
                    continue;
                for (std::size_t part = 0; part < model.parts.size(); ++part) {
                    if (model.parts[part].body != root_joint.child_body ||
                        sampled_model.parts[part].vertices.size() !=
                            model.parts[part].vertices.size())
                        continue;
                    for (std::size_t vertex = 0; vertex < model.parts[part].vertices.size();
                         ++vertex) {
                        const auto delta = subtract(sampled_model.parts[part].vertices[vertex],
                                                    model.parts[part].vertices[vertex]);
                        root_displacement =
                            std::max(root_displacement, std::sqrt(dot(delta, delta)));
                    }
                }
            }
            output << "{\"scene\":" << quoted(scene_value.name) << ",\"timeSeconds\":" << time
                   << ",\"disconnectedJoints\":" << disconnected
                   << ",\"rootMaxDisplacementMm\":" << root_displacement << '}';
        }
    }
    output << "]}";
    return output.str();
}

auto comparison_json(const compiler::ir::Project& first,
                     const compiler::ir::Project& second) -> std::string {
    const auto first_model = cad::build_model(first);
    const auto second_model = cad::build_model(second);
    const auto first_topology = cad::build_topology(first);
    const auto second_topology = cad::build_topology(second);
    const auto first_bodies = comparison_bodies(first, first_model);
    const auto second_bodies = comparison_bodies(second, second_model);
    std::vector<std::string> first_only;
    std::vector<std::string> second_only;
    std::vector<std::string> common;
    for (const auto& [name, summary] : first_bodies) {
        static_cast<void>(summary);
        if (second_bodies.contains(name))
            common.push_back(name);
        else
            first_only.push_back(name);
    }
    for (const auto& [name, summary] : second_bodies) {
        static_cast<void>(summary);
        if (!first_bodies.contains(name))
            second_only.push_back(name);
    }
    std::map<std::string, const compiler::ir::Joint*> first_joints;
    std::map<std::string, const compiler::ir::Joint*> second_joints;
    for (const auto& joint : first.joints)
        first_joints.emplace(joint.name, &joint);
    for (const auto& joint : second.joints)
        second_joints.emplace(joint.name, &joint);
    std::vector<std::string> first_only_joints;
    std::vector<std::string> second_only_joints;
    for (const auto& [name, joint] : first_joints) {
        static_cast<void>(joint);
        if (!second_joints.contains(name))
            first_only_joints.push_back(name);
    }
    for (const auto& [name, joint] : second_joints) {
        static_cast<void>(joint);
        if (!first_joints.contains(name))
            second_only_joints.push_back(name);
    }
    std::vector<std::string> first_scene_names;
    std::vector<std::string> second_scene_names;
    for (const auto& scene : first.scenes)
        first_scene_names.push_back(scene.name);
    for (const auto& scene : second.scenes)
        second_scene_names.push_back(scene.name);

    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"schema\":\"icad.agent.comparison.v2\",\"selectionDimensions\":["
              "\"endEffectorArchitecture\",\"componentStructure\",\"materialSystem\","
              "\"mechanismGraph\",\"sceneProgram\",\"spatialEnvelope\","
              "\"silhouetteAndOcclusion\",\"topologyCost\",\"runtimeCost\"],"
              "\"structuralDelta\":{\"firstOnlyBodies\":";
    write_string_array(output, first_only);
    output << ",\"secondOnlyBodies\":";
    write_string_array(output, second_only);
    output << ",\"commonBodies\":";
    write_string_array(output, common);
    output << ",\"changedBodies\":[";
    std::size_t changed_count = 0;
    for (const auto& name : common) {
        const auto& first_body = first_bodies.at(name);
        const auto& second_body = second_bodies.at(name);
        if (first_body.parts == second_body.parts &&
            first_body.vertices == second_body.vertices &&
            first_body.triangles == second_body.triangles &&
            first_body.material == second_body.material &&
            first_body.material_preset == second_body.material_preset) {
            continue;
        }
        if (changed_count++ != 0)
            output << ',';
        output << "{\"body\":" << quoted(name) << ",\"parts\":[" << first_body.parts
               << ',' << second_body.parts << "],\"vertices\":[" << first_body.vertices << ','
               << second_body.vertices << "],\"triangles\":[" << first_body.triangles << ','
               << second_body.triangles << "],\"triangleDelta\":"
               << static_cast<long long>(second_body.triangles) -
                      static_cast<long long>(first_body.triangles)
               << ",\"materials\":[{\"name\":" << quoted(first_body.material)
               << ",\"preset\":" << quoted(first_body.material_preset)
               << "},{\"name\":" << quoted(second_body.material) << ",\"preset\":"
               << quoted(second_body.material_preset) << "}]}";
    }
    output << "],\"bodyMembershipChanges\":" << first_only.size() + second_only.size()
           << ",\"changedBodyCount\":" << changed_count << ",\"mechanismDelta\":{"
              "\"firstOnlyJoints\":";
    write_string_array(output, first_only_joints);
    output << ",\"secondOnlyJoints\":";
    write_string_array(output, second_only_joints);
    output << ",\"movingDof\":[" << moving_joint_count(first) << ','
           << moving_joint_count(second) << "]},\"sceneDelta\":{\"firstScenes\":";
    write_string_array(output, first_scene_names);
    output << ",\"secondScenes\":";
    write_string_array(output, second_scene_names);
    output << ",\"trackCounts\":[" << scene_track_count(first) << ','
           << scene_track_count(second) << "],\"keyframeCounts\":["
           << scene_keyframe_count(first) << ',' << scene_keyframe_count(second)
           << "]}},\"viewDelta\":";
    write_view_delta(output, first_model, second_model);
    output << ",\"optimizationMatrix\":";
    write_optimization_matrix(output, first, second, first_model, second_model,
                              first_topology, second_topology);
    output << ",\"decisionPolicy\":{\"automaticWinner\":null,"
              "\"requiresDesignPriority\":true,\"rule\":"
              "\"Select by required task architecture first; optimize numeric costs only "
              "after functional equivalence is established\",\"nextOptimizationOrder\":["
              "\"selected end-effector function\",\"kinematic reach and constraints\","
              "\"interference and clearances\",\"topology and triangle cost\","
              "\"materials and scene coverage\"]},\"first\":";
    write_comparison_design(output, first, first_model, first_topology, first_bodies);
    output << ",\"second\":";
    write_comparison_design(output, second, second_model, second_topology, second_bodies);
    output << '}';
    return output.str();
}

auto project_json(const compiler::ir::Project& project) -> std::string {
    const auto delivery_model = cad::build_model(project);
    const auto metrics = cad::analyze(project, delivery_model);
    const auto intersections =
        cad::analyze_intersections(project, delivery_model, project.tolerance.linear_mm);
    const auto topology = cad::build_topology(project);
    const auto topology_validation = cad::validate_topology(topology);
    const auto constraint_results = constraints::validate(project, metrics);
    const auto manufacturing_report =
        manufacturing::validate(project, metrics, intersections);
    const auto dependencies = compiler::build_dependency_graph(project);
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"schema\":\"icad.inspect.v1\",\"project\":" << quoted(project.name)
           << ",\"revision\":" << quoted(document::revision_id(document::fingerprint(project)))
           << ",\"units\":" << quoted(project.canonical_length_unit)
           << ",\"tolerance\":{\"linearMm\":" << project.tolerance.linear_mm
           << ",\"angularDeg\":" << project.tolerance.angular_degrees << "}"
           << ",\"counts\":{\"parameters\":" << project.parameters.size()
           << ",\"angles\":" << project.angles.size() << ",\"points\":" << project.points.size()
           << ",\"vectors\":" << project.vectors.size() << ",\"poses\":" << project.poses.size()
           << ",\"instances\":" << project.instances.size()
           << ",\"joints\":" << project.joints.size()
           << ",\"interfaces\":" << project.interfaces.size()
           << ",\"connections\":" << project.connections.size()
           << ",\"materials\":" << project.materials.size()
           << ",\"profiles\":" << project.profiles.size()
           << ",\"sketches\":" << project.sketches.size()
           << ",\"profileSegments\":" << profile_segment_count(project)
           << ",\"curvedProfileSegments\":" << curved_profile_segment_count(project)
           << ",\"bodies\":" << project.bodies.size() << ",\"features\":" << feature_count(project)
           << ",\"topologySelections\":" << topology_selection_count(project)
           << ",\"booleanOperations\":" << boolean_operation_count(project)
           << ",\"modelingOperations\":" << modeling_operation_count(project)
           << ",\"surfaceOperations\":" << surface_operation_count(project)
           << ",\"dependencyNodes\":" << dependencies.nodes.size()
           << ",\"constraints\":" << project.constraints.size()
           << ",\"mates\":" << project.mates.size()
           << ",\"scenes\":" << project.scenes.size()
           << "},\"metrics\":{\"surfaceAreaMm2\":" << metrics.surface_area_mm2
           << ",\"volumeMm3\":" << metrics.volume_mm3 << ",\"boundsMin\":["
           << metrics.bounds.minimum[0] << ',' << metrics.bounds.minimum[1] << ','
           << metrics.bounds.minimum[2] << "],\"boundsMax\":[" << metrics.bounds.maximum[0] << ','
           << metrics.bounds.maximum[1] << ',' << metrics.bounds.maximum[2]
           << "]},\"topology\":{\"valid\":" << (topology_validation.valid() ? "true" : "false")
           << ",\"solids\":" << topology.solids.size()
           << ",\"vertices\":" << topology.vertex_count() << ",\"edges\":" << topology.edge_count()
           << ",\"wires\":" << topology.wire_count() << ",\"faces\":" << topology.face_count()
           << "},\"dependencies\":{\"edges\":" << dependencies.edge_count
           << ",\"evaluationOrder\":[";
    for (std::size_t index = 0; index < dependencies.evaluation_order.size(); ++index) {
        if (index != 0)
            output << ',';
        output << quoted(dependencies.evaluation_order[index]);
    }
    output << "],\"nodes\":[";
    for (std::size_t index = 0; index < dependencies.nodes.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& node = dependencies.nodes[index];
        output << "{\"id\":" << quoted(node.id) << ",\"kind\":" << quoted(node.kind)
               << ",\"dependsOn\":[";
        for (std::size_t dependency = 0; dependency < node.dependencies.size(); ++dependency) {
            if (dependency != 0)
                output << ',';
            output << quoted(node.dependencies[dependency]);
        }
        output << "]}";
    }
    output << "]},\"modeling\":{\"operations\":[";
    bool first_operation = true;
    for (const auto& body : project.bodies) {
        for (const auto& feature : body.features) {
            if (feature.type != "CHAMFER" && feature.type != "FILLET" &&
                feature.type != "LINEAR_PATTERN" && feature.type != "MIRROR")
                continue;
            if (!first_operation)
                output << ',';
            first_operation = false;
            output << "{\"body\":" << quoted(body.name) << ",\"feature\":"
                   << quoted(feature.name) << ",\"type\":" << quoted(feature.type);
            if (!feature.selected_edge_point.empty())
                output << ",\"edgeNearestPoint\":" << quoted(feature.selected_edge_point);
            if (!feature.selected_edge_location.empty())
                output << ",\"selection\":{\"kind\":\"EDGE_LOOP\",\"location\":"
                       << quoted(feature.selected_edge_location)
                       << ",\"classification\":"
                       << quoted(feature.selected_edge_classification)
                       << ",\"applicableOperations\":[\"FILLET\",\"CHAMFER\"]";
            if (!feature.selected_edge_location.empty()) {
                if (!feature.selected_edge_set.empty())
                    output << ",\"reference\":" << quoted(feature.selected_edge_set);
                if (!feature.selected_topology_id.empty())
                    output << ",\"topologyId\":" << quoted(feature.selected_topology_id);
                output << '}';
            }
            if (!feature.direction.empty())
                output << ",\"direction\":" << quoted(feature.direction)
                       << ",\"count\":" << feature.count;
            if (!feature.plane_point.empty())
                output << ",\"planePoint\":" << quoted(feature.plane_point)
                       << ",\"planeNormal\":" << quoted(feature.plane_normal);
            output << '}';
        }
    }
    output << "],\"topologySelections\":[";
    bool first_selection = true;
    for (const auto& body : project.bodies) {
        for (const auto& selection : body.topology_selections) {
            if (!first_selection)
                output << ',';
            first_selection = false;
            output << "{\"body\":" << quoted(body.name) << ",\"query\":";
            write_topology_selection(output, selection);
            output << '}';
        }
    }
    output << "],\"surfaces\":[";
    bool first_surface = true;
    for (const auto& body : project.bodies) {
        for (const auto& feature : body.features) {
            if (feature.type != "SWEEP" && feature.type != "LOFT" &&
                feature.type != "FREEFORM" && feature.type != "REVOLVE")
                continue;
            if (!first_surface)
                output << ',';
            first_surface = false;
            output << "{\"body\":" << quoted(body.name) << ",\"feature\":"
                   << quoted(feature.name) << ",\"type\":" << quoted(feature.type)
                   << ",\"profile\":" << quoted(feature.profile);
            if (!feature.target_profile.empty())
                output << ",\"targetProfile\":" << quoted(feature.target_profile);
            if (!feature.path_points.empty()) {
                output << ",\"path\":[";
                for (std::size_t point = 0; point < feature.path_points.size(); ++point) {
                    if (point != 0)
                        output << ',';
                    output << quoted(feature.path_points[point]);
                }
                output << ']';
            }
            if (feature.type == "FREEFORM")
                output << ",\"sections\":" << feature.count;
            output << '}';
        }
    }
    output << "],\"repairs\":[";
    bool first_repair = true;
    for (const auto& part : delivery_model.parts) {
        if (!part.boolean_result && !part.faceted_result)
            continue;
        if (!first_repair)
            output << ',';
        first_repair = false;
        output << "{\"body\":" << quoted(part.body) << ",\"result\":" << quoted(part.name)
               << ",\"actions\":[";
        for (std::size_t action = 0; action < part.repairs.size(); ++action) {
            if (action != 0)
                output << ',';
            output << quoted(part.repairs[action]);
        }
        output << "]}";
    }
    output << "]},\"intersections\":{\"partPairCandidates\":"
           << intersections.part_pair_candidates
           << ",\"trianglePairCandidates\":" << intersections.triangle_pair_candidates
           << ",\"intersectingPartPairs\":" << intersections.intersecting_part_pairs
           << ",\"intersectingTrianglePairs\":" << intersections.intersecting_triangle_pairs
           << ",\"penetratingPartPairs\":" << intersections.penetrating_part_pairs
           << ",\"declaredEngagementPartPairs\":"
           << intersections.declared_engagement_part_pairs
           << ",\"unintendedPenetratingPartPairs\":"
           << intersections.unintended_penetrating_part_pairs
           << ",\"containedPartPairs\":" << intersections.contained_part_pairs
           << ",\"surfaceContactOnlyPartPairs\":"
           << intersections.surface_contact_only_part_pairs
           << ",\"bodyContacts\":[";
    for (std::size_t index = 0; index < intersections.body_contacts.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& contact = intersections.body_contacts[index];
        output << "{\"firstBody\":" << quoted(contact.first_body)
               << ",\"secondBody\":" << quoted(contact.second_body)
               << ",\"partPairCandidates\":" << contact.part_pair_candidates
               << ",\"trianglePairCandidates\":" << contact.triangle_pair_candidates
               << ",\"intersectingPartPairs\":" << contact.intersecting_part_pairs
               << ",\"intersectingTrianglePairs\":" << contact.intersecting_triangle_pairs
               << ",\"penetratingPartPairs\":" << contact.penetrating_part_pairs
               << ",\"containedPartPairs\":" << contact.contained_part_pairs
               << ",\"surfaceContactOnlyPartPairs\":"
               << contact.surface_contact_only_part_pairs
               << ",\"declaredConnection\":"
               << (contact.declared_connection ? "true" : "false");
        if (contact.declared_connection) {
            output << ",\"connection\":" << quoted(contact.connection_name)
                   << ",\"method\":" << quoted(contact.connection_method)
                   << ",\"standard\":" << quoted(contact.connection_standard);
        }
        output << '}';
    }
    output << "]},\"spatial\":{\"angles\":[";
    for (std::size_t index = 0; index < project.angles.size(); ++index) {
        if (index != 0)
            output << ',';
        output << "{\"name\":" << quoted(project.angles[index].name)
               << ",\"degrees\":" << project.angles[index].degrees << '}';
    }
    output << "],\"points\":[";
    for (std::size_t index = 0; index < project.points.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& point = project.points[index];
        output << "{\"name\":" << quoted(point.name)
               << ",\"kind\":" << quoted(point_kind_name(point.kind)) << ",\"positionMm\":["
               << point.position_mm[0] << ',' << point.position_mm[1] << ','
               << point.position_mm[2] << ']';
        if (point.kind == compiler::ir::SpatialPointKind::offset) {
            output << ",\"from\":" << quoted(point.base_point)
                   << ",\"along\":" << quoted(point.direction)
                   << ",\"distanceMm\":" << point.distance_mm;
            if (!point.distance_reference.empty())
                output << ",\"distanceReference\":" << quoted(point.distance_reference);
        }
        output << '}';
    }
    output << "],\"vectors\":[";
    for (std::size_t index = 0; index < project.vectors.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& vector = project.vectors[index];
        output << "{\"name\":" << quoted(vector.name)
               << ",\"kind\":" << quoted(direction_kind_name(vector.kind)) << ",\"unit\":["
               << vector.unit[0] << ',' << vector.unit[1] << ',' << vector.unit[2] << ']';
        if (vector.kind == compiler::ir::DirectionKind::between_points) {
            output << ",\"from\":" << quoted(vector.from_point)
                   << ",\"to\":" << quoted(vector.to_point);
        } else if (vector.kind == compiler::ir::DirectionKind::rotated) {
            output << ",\"source\":" << quoted(vector.source_direction)
                   << ",\"around\":" << quoted(vector.around_axis)
                   << ",\"angleDeg\":" << vector.angle_degrees;
            if (!vector.angle_reference.empty())
                output << ",\"angleReference\":" << quoted(vector.angle_reference);
        }
        output << '}';
    }
    output << "]},\"sketches\":[";
    for (std::size_t sketch_index = 0; sketch_index < project.sketches.size(); ++sketch_index) {
        if (sketch_index != 0)
            output << ',';
        const auto& sketch = project.sketches[sketch_index];
        output << "{\"name\":" << quoted(sketch.name);
        if (!sketch.body.empty())
            output << ",\"body\":" << quoted(sketch.body);
        output << ",\"plane\":" << quoted(sketch.plane);
        if (!sketch.support_feature.empty())
            output << ",\"supportFeature\":" << quoted(sketch.support_feature)
                   << ",\"supportFace\":" << quoted(sketch.support_face);
        if (!sketch.support_reference.empty())
            output << ",\"supportReference\":" << quoted(sketch.support_reference);
        if (!sketch.support_topology_id.empty())
            output << ",\"supportTopologyId\":" << quoted(sketch.support_topology_id);
        output
               << ",\"status\":" << quoted(sketch_status_name(sketch.status))
               << ",\"degreesOfFreedom\":" << sketch.degrees_of_freedom
               << ",\"iterations\":" << sketch.iterations
               << ",\"maximumResidual\":" << sketch.maximum_residual
               << ",\"solveRequirement\":" << quoted(sketch.solve_requirement)
               << ",\"shapes\":[";
        for (std::size_t shape_index = 0; shape_index < sketch.shapes.size(); ++shape_index) {
            if (shape_index != 0)
                output << ',';
            const auto& shape = sketch.shapes[shape_index];
            output << "{\"name\":" << quoted(shape.name) << ",\"role\":"
                   << quoted(shape.role) << ",\"closed\":"
                   << (shape.closed ? "true" : "false") << ",\"profile\":"
                   << quoted(shape.profile) << ",\"areaMm2\":" << shape.area_mm2
                   << ",\"points\":[";
            for (std::size_t point = 0; point < shape.points.size(); ++point) {
                if (point != 0)
                    output << ',';
                output << quoted(shape.points[point]);
            }
            output << "],\"entities\":[";
            for (std::size_t entity = 0; entity < shape.entities.size(); ++entity) {
                if (entity != 0)
                    output << ',';
                output << quoted(shape.entities[entity]);
            }
            output << ']';
            if (!shape.containing_shape.empty())
                output << ",\"containedBy\":" << quoted(shape.containing_shape);
            output << '}';
        }
        output << "],\"regions\":[";
        for (std::size_t region_index = 0; region_index < sketch.regions.size(); ++region_index) {
            if (region_index != 0)
                output << ',';
            const auto& region = sketch.regions[region_index];
            output << "{\"name\":" << quoted(region.name) << ",\"outerShape\":"
                   << quoted(region.outer_shape) << ",\"holeShapes\":[";
            for (std::size_t hole = 0; hole < region.hole_shapes.size(); ++hole) {
                if (hole != 0)
                    output << ',';
                output << quoted(region.hole_shapes[hole]);
            }
            output << "],\"outerProfile\":" << quoted(region.outer_profile)
                   << ",\"holeProfiles\":[";
            for (std::size_t hole = 0; hole < region.hole_profiles.size(); ++hole) {
                if (hole != 0)
                    output << ',';
                output << quoted(region.hole_profiles[hole]);
            }
            output << "],\"areaMm2\":" << region.area_mm2 << '}';
        }
        output << "],\"points\":[";
        for (std::size_t point_index = 0; point_index < sketch.points.size(); ++point_index) {
            if (point_index != 0)
                output << ',';
            const auto& point_value = sketch.points[point_index];
            output << "{\"name\":" << quoted(point_value.name)
                   << ",\"fixed\":" << (point_value.fixed ? "true" : "false")
                   << ",\"initialMm\":[" << point_value.initial.x_mm << ','
                   << point_value.initial.y_mm << "],\"solvedMm\":[" << point_value.solved.x_mm
                   << ',' << point_value.solved.y_mm << "]}";
        }
        output << "],\"entities\":[";
        for (std::size_t entity_index = 0; entity_index < sketch.entities.size();
             ++entity_index) {
            if (entity_index != 0)
                output << ',';
            const auto& entity = sketch.entities[entity_index];
            output << "{\"name\":" << quoted(entity.name)
                   << ",\"type\":"
                   << quoted(entity.full_circle
                                 ? "CIRCLE"
                             : entity.kind == compiler::ir::ProfileSegmentKind::circular_arc
                                 ? "ARC"
                                 : "LINE");
            if (!entity.full_circle)
                output << ",\"from\":" << quoted(entity.start)
                       << ",\"to\":" << quoted(entity.end);
            if (entity.kind == compiler::ir::ProfileSegmentKind::circular_arc) {
                output << ",\"center\":" << quoted(entity.center)
                       << ",\"direction\":"
                       << quoted(entity.counterclockwise ? "CCW" : "CW");
                if (entity.full_circle)
                    output << ",\"radiusMm\":" << entity.radius_mm;
            }
            output << '}';
        }
        output << "],\"constraints\":[";
        for (std::size_t constraint_index = 0;
             constraint_index < sketch.constraints.size(); ++constraint_index) {
            if (constraint_index != 0)
                output << ',';
            const auto& constraint = sketch.constraints[constraint_index];
            output << "{\"name\":" << quoted(constraint.name)
                   << ",\"type\":" << quoted(constraint.kind) << ",\"references\":[";
            for (std::size_t reference = 0; reference < constraint.references.size(); ++reference) {
                if (reference != 0)
                    output << ',';
                output << quoted(constraint.references[reference]);
            }
            output << ']';
            if (!constraint.target_unit.empty()) {
                output << ",\"target\":" << constraint.target_value
                       << ",\"unit\":" << quoted(constraint.target_unit);
            }
            if (!constraint.target_reference.empty())
                output << ",\"targetReference\":" << quoted(constraint.target_reference);
            output << '}';
        }
        output << "]}";
    }
    output << "],\"mechanism\":{\"degreesOfFreedom\":"
           << std::ranges::count_if(
                  project.joints,
                  [](const auto& joint) { return joint.kind != compiler::ir::JointKind::fixed; })
           << ",\"poses\":[";
    for (std::size_t index = 0; index < project.poses.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& pose = project.poses[index];
        output << "{\"body\":" << quoted(pose.body) << ",\"at\":" << quoted(pose.point)
               << ",\"positionMm\":[" << pose.transform.position_mm[0] << ','
               << pose.transform.position_mm[1] << ',' << pose.transform.position_mm[2]
               << "],\"rotationDeg\":[" << pose.transform.rotation_deg[0] << ','
               << pose.transform.rotation_deg[1] << ',' << pose.transform.rotation_deg[2] << "]}";
    }
    output << "],\"instances\":[";
    for (std::size_t index = 0; index < project.instances.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& instance = project.instances[index];
        output << "{\"name\":" << quoted(instance.name) << ",\"body\":"
               << quoted(instance.body) << ",\"at\":" << quoted(instance.point)
               << ",\"positionMm\":[" << instance.transform.position_mm[0] << ','
               << instance.transform.position_mm[1] << ',' << instance.transform.position_mm[2]
               << "],\"rotationDeg\":[" << instance.transform.rotation_deg[0] << ','
               << instance.transform.rotation_deg[1] << ',' << instance.transform.rotation_deg[2]
               << ']';
        const bool driven = std::ranges::any_of(
            project.joints,
            [&](const auto& joint) { return joint.child_body == instance.name && joint.driven; });
        output << ",\"jointDriven\":" << (driven ? "true" : "false");
        const auto solved_part =
            std::ranges::find(metrics.parts, instance.name, &cad::PartAnalysis::body);
        if (solved_part != metrics.parts.end()) {
            auto bounds = solved_part->bounds;
            for (auto part = std::next(solved_part); part != metrics.parts.end(); ++part) {
                if (part->body != instance.name)
                    continue;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], part->bounds.minimum[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], part->bounds.maximum[axis]);
                }
            }
            output << ",\"solvedBoundsMin\":[" << bounds.minimum[0] << ',' << bounds.minimum[1]
                   << ',' << bounds.minimum[2] << "],\"solvedBoundsMax\":[" << bounds.maximum[0]
                   << ',' << bounds.maximum[1] << ',' << bounds.maximum[2] << ']';
        }
        output << '}';
    }
    output << "],\"joints\":[";
    for (std::size_t index = 0; index < project.joints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& joint = project.joints[index];
        output << "{\"name\":" << quoted(joint.name)
               << ",\"type\":" << quoted(joint_kind_name(joint.kind))
               << ",\"parent\":" << quoted(joint.parent_body)
               << ",\"child\":" << quoted(joint.child_body) << ",\"at\":" << quoted(joint.point)
               << ",\"axis\":" << quoted(joint.axis) << ",\"value\":" << joint.value
               << ",\"limits\":[" << joint.lower_limit << ',' << joint.upper_limit
               << "],\"unit\":" << quoted(joint.unit);
        if (!joint.value_reference.empty())
            output << ",\"valueReference\":" << quoted(joint.value_reference);
        if (!joint.lower_limit_reference.empty())
            output << ",\"lowerLimitReference\":" << quoted(joint.lower_limit_reference);
        if (!joint.upper_limit_reference.empty())
            output << ",\"upperLimitReference\":" << quoted(joint.upper_limit_reference);
        output << '}';
    }
    output << "],\"interfaces\":[";
    for (std::size_t index = 0; index < project.interfaces.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& interface = project.interfaces[index];
        output << "{\"name\":" << quoted(interface.name)
               << ",\"occurrence\":" << quoted(interface.occurrence)
               << ",\"point\":" << quoted(interface.point)
               << ",\"axis\":" << quoted(interface.axis)
               << ",\"type\":" << quoted(interface.kind);
        if (interface.has_size)
            output << ",\"sizeMm\":" << interface.size_mm;
        const auto point = std::ranges::find(project.points, interface.point,
                                             &compiler::ir::SpatialPoint::name);
        if (point != project.points.end()) {
            output << ",\"positionMm\":[" << point->position_mm[0] << ','
                   << point->position_mm[1] << ',' << point->position_mm[2] << ']';
        }
        const auto axis = std::ranges::find(project.vectors, interface.axis,
                                            &compiler::ir::Direction::name);
        if (axis != project.vectors.end()) {
            output << ",\"axisVector\":[" << axis->unit[0] << ',' << axis->unit[1]
                   << ',' << axis->unit[2] << ']';
        }
        output << '}';
    }
    output << "],\"connections\":[";
    for (std::size_t index = 0; index < project.connections.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& connection = project.connections[index];
        output << "{\"name\":" << quoted(connection.name)
               << ",\"interfaces\":[" << quoted(connection.first_interface) << ','
               << quoted(connection.second_interface) << ']'
               << ",\"method\":" << quoted(connection.method)
               << ",\"standard\":" << quoted(connection.standard)
               << ",\"fastener\":" << quoted(connection.fastener)
               << ",\"fit\":" << quoted(connection.fit)
               << ",\"clearanceMm\":" << connection.clearance_mm
               << ",\"interfaceGapMm\":" << connection.interface_gap_mm
               << ",\"axisAlignment\":" << connection.axis_alignment
               << ",\"automatic\":" << (connection.automatic ? "true" : "false")
               << ",\"aligned\":" << (connection.aligned ? "true" : "false")
               << ",\"snapState\":"
               << quoted(connection.aligned ? "SEATED" :
                         (connection.automatic ? "SNAP_REQUIRED" : "MISALIGNED"))
               << '}';
    }
    output << "]},\"bodies\":[";
    for (std::size_t index = 0; index < project.bodies.size(); ++index) {
        const auto& body = project.bodies[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"name\":" << quoted(body.name) << ",\"material\":" << quoted(body.material)
               << ",\"features\":" << body.features.size();
        const auto first_part =
            std::ranges::find(metrics.parts, body.name, &cad::PartAnalysis::body);
        if (first_part != metrics.parts.end()) {
            auto bounds = first_part->bounds;
            for (auto part = std::next(first_part); part != metrics.parts.end(); ++part) {
                if (part->body != body.name)
                    continue;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], part->bounds.minimum[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], part->bounds.maximum[axis]);
                }
            }
            output << ",\"geometry\":{\"boundsMin\":[" << bounds.minimum[0] << ','
                   << bounds.minimum[1] << ',' << bounds.minimum[2] << "],\"boundsMax\":["
                   << bounds.maximum[0] << ',' << bounds.maximum[1] << ',' << bounds.maximum[2]
                   << "],\"centerMm\":[" << (bounds.minimum[0] + bounds.maximum[0]) * 0.5 << ','
                   << (bounds.minimum[1] + bounds.maximum[1]) * 0.5 << ','
                   << (bounds.minimum[2] + bounds.maximum[2]) * 0.5 << "],\"sizeMm\":["
                   << bounds.maximum[0] - bounds.minimum[0] << ','
                   << bounds.maximum[1] - bounds.minimum[1] << ','
                   << bounds.maximum[2] - bounds.minimum[2] << "]}";
        }
        output << '}';
    }
    output << "],\"constraints\":[";
    for (std::size_t index = 0; index < project.constraints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& constraint = project.constraints[index];
        const auto& validation = constraint_results[index];
        output << "{\"name\":" << quoted(constraint.name) << ",\"type\":" << quoted(constraint.kind)
               << ",\"first\":" << quoted(constraint.first_body)
               << ",\"second\":" << quoted(constraint.second_body)
               << ",\"passed\":" << (validation.passed ? "true" : "false")
               << ",\"required\":" << validation.required_mm
               << ",\"actual\":" << validation.actual_mm
               << ",\"unit\":" << quoted(validation.unit);
        if (!constraint.target_reference.empty())
            output << ",\"targetReference\":" << quoted(constraint.target_reference);
        output << '}';
    }
    output << "],\"mates\":[";
    for (std::size_t index = 0; index < project.mates.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& mate = project.mates[index];
        const auto& validation = constraint_results[project.constraints.size() + index];
        output << "{\"name\":" << quoted(mate.name)
               << ",\"type\":"
               << quoted(mate.kind == compiler::ir::MateKind::face ? "FACE" : "EDGE")
               << ",\"firstOccurrence\":" << quoted(mate.first_occurrence)
               << ",\"firstSelector\":" << quoted(mate.first_selector)
               << ",\"secondOccurrence\":" << quoted(mate.second_occurrence)
               << ",\"secondSelector\":" << quoted(mate.second_selector)
               << ",\"passed\":" << (validation.passed ? "true" : "false")
               << ",\"requiredMm\":" << validation.required_mm
               << ",\"actualMm\":" << validation.actual_mm;
        if (!mate.target_reference.empty())
            output << ",\"targetReference\":" << quoted(mate.target_reference);
        output << '}';
    }
    output << "],\"scenes\":[";
    for (std::size_t scene_index = 0; scene_index < project.scenes.size(); ++scene_index) {
        if (scene_index != 0)
            output << ',';
        const auto& scene = project.scenes[scene_index];
        output << "{\"name\":" << quoted(scene.name)
               << ",\"durationSeconds\":" << scene.duration_seconds
               << ",\"fps\":" << scene.frames_per_second
               << ",\"background\":" << quoted(scene.background)
               << ",\"loopCount\":" << scene.loop_count
               << ",\"lights\":" << scene.lights.size()
               << ",\"events\":" << scene.events.size() << ",\"tracks\":[";
        for (std::size_t track_index = 0; track_index < scene.tracks.size(); ++track_index) {
            if (track_index != 0)
                output << ',';
            const auto& track = scene.tracks[track_index];
            output << "{\"name\":" << quoted(track.name)
                   << ",\"targetKind\":" << quoted(track.target_kind)
                   << ",\"target\":" << quoted(track.target)
                   << ",\"easing\":" << quoted(track.easing)
                   << ",\"keyframes\":" << track.keyframes.size();
            if (track.target_kind == "JOINT" && !track.keyframes.empty()) {
                const auto [minimum, maximum] = std::ranges::minmax_element(
                    track.keyframes, {}, &compiler::ir::Keyframe::joint_value);
                output << ",\"valueRange\":[" << minimum->joint_value << ','
                       << maximum->joint_value << "],\"unit\":"
                       << quoted(track.keyframes.front().joint_unit);
            }
            output << '}';
        }
        output << "]}";
    }
    output << "],\"validation\":{\"constraintsPassed\":"
           << (constraints::all_passed(constraint_results) ? "true" : "false")
           << ",\"manufacturingPassed\":" << (manufacturing_report.passed ? "true" : "false")
           << ",\"manufacturingIssues\":" << manufacturing_report.issues.size() << "}}";
    return output.str();
}

auto topology_json(const compiler::ir::Project& project) -> std::string {
    const auto topology = cad::build_topology(project);
    return topology_json(project, topology);
}

auto topology_json(const compiler::ir::Project& project,
                   const cad::TopologyModel& topology) -> std::string {
    static_cast<void>(project);
    const auto validation = cad::validate_topology(topology);
    std::ostringstream output;
    output << std::setprecision(17) << "{\"schema\":\"icad.topology.v1\",\"valid\":"
           << (validation.valid() ? "true" : "false")
           << ",\"counts\":{\"solids\":" << topology.solids.size()
           << ",\"vertices\":" << topology.vertex_count() << ",\"edges\":" << topology.edge_count()
           << ",\"wires\":" << topology.wire_count() << ",\"faces\":" << topology.face_count()
           << "},\"solids\":[";
    for (std::size_t solid_index = 0; solid_index < topology.solids.size(); ++solid_index) {
        const auto& solid = topology.solids[solid_index];
        if (solid_index != 0)
            output << ',';
        const auto characteristic = solid.euler_characteristic();
        output << "{\"id\":" << quoted(solid.id) << ",\"body\":" << quoted(solid.body)
               << ",\"feature\":" << quoted(solid.feature)
               << ",\"featureType\":" << quoted(solid.feature_type)
               << ",\"shell\":" << quoted(solid.shell.id)
               << ",\"eulerCharacteristic\":" << characteristic
               << ",\"genus\":" << (2 - characteristic) / 2 << ",\"vertices\":[";
        for (std::size_t index = 0; index < solid.vertices.size(); ++index) {
            const auto& vertex = solid.vertices[index];
            if (index != 0)
                output << ',';
            output << "{\"id\":" << quoted(vertex.id) << ",\"pointMm\":[" << vertex.point.x << ','
                   << vertex.point.y << ',' << vertex.point.z << "]}";
        }
        output << "],\"edges\":[";
        for (std::size_t index = 0; index < solid.edges.size(); ++index) {
            const auto& edge = solid.edges[index];
            if (index != 0)
                output << ',';
            output << "{\"id\":" << quoted(edge.id)
                   << ",\"curve\":" << quoted(cad::curve_kind_name(edge.curve.kind))
                   << ",\"startVertex\":" << quoted(edge.start_vertex)
                   << ",\"endVertex\":" << quoted(edge.end_vertex)
                   << ",\"closed\":" << (edge.closed ? "true" : "false") << ",\"originMm\":["
                   << edge.curve.origin.x << ',' << edge.curve.origin.y << ','
                   << edge.curve.origin.z << "],\"direction\":[" << edge.curve.direction.x << ','
                   << edge.curve.direction.y << ',' << edge.curve.direction.z << "],\"axis\":["
                   << edge.curve.axis.x << ',' << edge.curve.axis.y << ',' << edge.curve.axis.z
                   << "],\"radiusMm\":" << edge.curve.radius << ",\"parameterRange\":["
                   << edge.curve.parameter_start << ',' << edge.curve.parameter_end << "]}";
        }
        output << "],\"faces\":[";
        for (std::size_t index = 0; index < solid.faces.size(); ++index) {
            const auto& face = solid.faces[index];
            if (index != 0)
                output << ',';
            output << "{\"id\":" << quoted(face.id)
                   << ",\"semanticPath\":" << quoted(face.id)
                   << ",\"generatedByFeature\":" << quoted(solid.feature)
                   << ",\"surface\":" << quoted(cad::surface_kind_name(face.surface.kind))
                   << ",\"originMm\":[" << face.surface.origin.x << ',' << face.surface.origin.y
                   << ',' << face.surface.origin.z << "],\"axis\":[" << face.surface.axis.x << ','
                   << face.surface.axis.y << ',' << face.surface.axis.z
                   << "],\"radiusMm\":" << face.surface.radius
                   << ",\"semiAngleRadians\":" << face.surface.semi_angle_radians
                   << ",\"boundaryWires\":[";
            for (std::size_t wire_index = 0; wire_index < face.boundaries.size(); ++wire_index) {
                const auto& wire = face.boundaries[wire_index];
                if (wire_index != 0)
                    output << ',';
                output << "{\"id\":" << quoted(wire.id) << ",\"edges\":[";
                for (std::size_t use_index = 0; use_index < wire.edges.size(); ++use_index) {
                    const auto& use = wire.edges[use_index];
                    if (use_index != 0)
                        output << ',';
                    output << "{\"edge\":" << quoted(use.edge)
                           << ",\"reversed\":" << (use.reversed ? "true" : "false") << '}';
                }
                output << "]}";
            }
            output << "]}";
        }
        output << "]}";
    }
    output << "],\"issues\":[";
    for (std::size_t index = 0; index < validation.issues.size(); ++index) {
        const auto& issue = validation.issues[index];
        if (index != 0)
            output << ',';
        output << "{\"code\":" << quoted(issue.code) << ",\"entity\":" << quoted(issue.entity)
               << ",\"message\":" << quoted(issue.message) << '}';
    }
    output << "]}";
    return output.str();
}

auto diagnostics_json(std::string_view source) -> std::string {
    const auto result = compiler::compile(source);
    std::ostringstream output;
    output << "{\"schema\":\"icad.diagnostics.v1\",\"ok\":" << (result.ok() ? "true" : "false")
           << ",\"diagnostics\":[";
    for (std::size_t index = 0; index < result.diagnostics.size(); ++index) {
        const auto& diagnostic = result.diagnostics[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"severity\":" << quoted(severity_name(diagnostic.severity))
               << ",\"code\":" << quoted(diagnostic.code)
               << ",\"message\":" << quoted(diagnostic.message)
               << ",\"line\":" << diagnostic.location.line
               << ",\"column\":" << diagnostic.location.column << '}';
    }
    output << "]}";
    return output.str();
}

} // namespace icad::ai
