#include "icad/scene/exporter.hpp"

#include "icad/cad/model.hpp"
#include "icad/compiler/dependency_graph.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace icad::scene {
namespace {

[[nodiscard]] auto json_string(std::string_view value) -> std::string;

auto write_dependency_graph(std::ostream& output, const compiler::ir::Project& project) -> void {
    const auto graph = compiler::build_dependency_graph(project);
    output << "\"dependencyGraph\":{\"nodes\":[";
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        if (index != 0)
            output << ',';
        output << "{\"id\":" << json_string(graph.nodes[index].id)
               << ",\"kind\":" << json_string(graph.nodes[index].kind) << '}';
    }
    output << "],\"edges\":[";
    for (std::size_t index = 0; index < graph.edges.size(); ++index) {
        if (index != 0)
            output << ',';
        output << '[' << graph.edges[index].dependency << ',' << graph.edges[index].consumer
               << ']';
    }
    output << "]}";
}

auto rotate_axis(cad::Point3& point, const cad::Point3& pivot, const cad::Vector3& axis,
                 double degrees) -> void {
    const double radians = degrees * std::numbers::pi / 180.0;
    const cad::Point3 relative{point.x - pivot.x, point.y - pivot.y, point.z - pivot.z};
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const cad::Point3 cross{axis.y * relative.z - axis.z * relative.y,
                            axis.z * relative.x - axis.x * relative.z,
                            axis.x * relative.y - axis.y * relative.x};
    const double projection =
        axis.x * relative.x + axis.y * relative.y + axis.z * relative.z;
    point = {pivot.x + relative.x * cosine + cross.x * sine +
                 axis.x * projection * (1.0 - cosine),
             pivot.y + relative.y * cosine + cross.y * sine +
                 axis.y * projection * (1.0 - cosine),
             pivot.z + relative.z * cosine + cross.z * sine +
                 axis.z * projection * (1.0 - cosine)};
}

struct JointFrame {
    cad::Point3 pivot;
    cad::Vector3 axis;
};

[[nodiscard]] auto joint_frame(const compiler::ir::Project& project,
                               const compiler::ir::Joint& target) -> JointFrame {
    const auto target_point = std::ranges::find(project.points, target.point,
                                                &compiler::ir::SpatialPoint::name);
    const auto target_axis =
        std::ranges::find(project.vectors, target.axis, &compiler::ir::Direction::name);
    JointFrame result;
    if (target_point == project.points.end() || target_axis == project.vectors.end())
        return result;
    result.pivot = {target_point->position_mm[0], target_point->position_mm[1],
                    target_point->position_mm[2]};
    result.axis = {target_axis->unit[0], target_axis->unit[1], target_axis->unit[2]};
    std::vector<const compiler::ir::Joint*> ancestors;
    std::string child = target.parent_body;
    while (child != "WORLD") {
        const auto joint =
            std::ranges::find(project.joints, child, &compiler::ir::Joint::child_body);
        if (joint == project.joints.end())
            break;
        ancestors.push_back(&*joint);
        child = joint->parent_body;
    }
    std::ranges::reverse(ancestors);
    struct Operation {
        compiler::ir::JointKind kind;
        cad::Point3 pivot;
        cad::Vector3 axis;
        double value{};
    };
    std::vector<Operation> applied;
    for (const auto* ancestor : ancestors) {
        const auto point = std::ranges::find(project.points, ancestor->point,
                                             &compiler::ir::SpatialPoint::name);
        const auto axis = std::ranges::find(project.vectors, ancestor->axis,
                                            &compiler::ir::Direction::name);
        if (point == project.points.end() || axis == project.vectors.end())
            continue;
        cad::Point3 pivot{point->position_mm[0], point->position_mm[1], point->position_mm[2]};
        cad::Vector3 direction{axis->unit[0], axis->unit[1], axis->unit[2]};
        for (const auto& operation : applied) {
            if (operation.kind == compiler::ir::JointKind::revolute) {
                rotate_axis(pivot, operation.pivot, operation.axis, operation.value);
                cad::Point3 vector_point{direction.x, direction.y, direction.z};
                rotate_axis(vector_point, {}, operation.axis, operation.value);
                direction = {vector_point.x, vector_point.y, vector_point.z};
            } else if (operation.kind == compiler::ir::JointKind::prismatic) {
                pivot.x += operation.axis.x * operation.value;
                pivot.y += operation.axis.y * operation.value;
                pivot.z += operation.axis.z * operation.value;
            }
        }
        if (ancestor->kind == compiler::ir::JointKind::revolute) {
            rotate_axis(result.pivot, pivot, direction, ancestor->value);
            cad::Point3 vector_point{result.axis.x, result.axis.y, result.axis.z};
            rotate_axis(vector_point, {}, direction, ancestor->value);
            result.axis = {vector_point.x, vector_point.y, vector_point.z};
        } else if (ancestor->kind == compiler::ir::JointKind::prismatic) {
            result.pivot.x += direction.x * ancestor->value;
            result.pivot.y += direction.y * ancestor->value;
            result.pivot.z += direction.z * ancestor->value;
        }
        applied.push_back({ancestor->kind, pivot, direction, ancestor->value});
    }
    return result;
}

[[nodiscard]] auto json_string(std::string_view value) -> std::string {
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
        default:
            result.push_back(character);
            break;
        }
    }
    result.push_back('"');
    return result;
}

auto append_le(std::string& bytes, std::uint32_t value, std::size_t count) -> void {
    for (std::size_t index = 0; index < count; ++index) {
        bytes.push_back(static_cast<char>((value >> (8 * index)) & 0xffU));
    }
}

[[nodiscard]] auto sample_texture(std::string_view kind, unsigned int seed, std::size_t x,
                                  std::size_t y) -> double {
    std::uint32_t hash = seed ^ (static_cast<std::uint32_t>(x) * 0x9e3779b9U) ^
                         (static_cast<std::uint32_t>(y) * 0x85ebca6bU);
    hash ^= hash >> 16U;
    hash *= 0x7feb352dU;
    hash ^= hash >> 15U;
    const double noise = static_cast<double>(hash & 0xffffU) / 65535.0;
    double pattern = noise;
    if (kind.find("brushed") != std::string_view::npos ||
        kind.find("brush") != std::string_view::npos) {
        pattern = 0.55 * noise + 0.45 * std::sin(static_cast<double>(x) * 1.8);
    } else if (kind.find("wood") != std::string_view::npos ||
               kind.find("vein") != std::string_view::npos) {
        pattern = 0.5 + 0.5 * std::sin(static_cast<double>(x) * 0.55 + noise * 3.0);
    } else if (kind.find("brick") != std::string_view::npos) {
        const bool mortar = y % 8 == 0 || (x + ((y / 8) % 2) * 8) % 16 == 0;
        pattern = mortar ? 1.0 : noise * 0.55;
    } else if (kind.find("twill") != std::string_view::npos ||
               kind.find("woven") != std::string_view::npos) {
        pattern = ((x + y) % 8 < 3 || (x + 32 - y) % 8 < 3) ? 0.75 : 0.2;
    } else if (kind.find("ripple") != std::string_view::npos) {
        pattern = 0.5 + 0.5 * std::sin(std::hypot(static_cast<double>(x - 16),
                                                  static_cast<double>(y - 16)) *
                                       1.4);
    }
    return std::clamp(0.72 + 0.32 * pattern, 0.45, 1.15);
}

[[nodiscard]] auto texture_bmp(const compiler::ir::Material& material) -> std::string {
    constexpr std::size_t width = 32;
    constexpr std::size_t height = 32;
    constexpr std::size_t row_size = (width * 3 + 3) & ~std::size_t{3};
    constexpr std::size_t pixel_bytes = row_size * height;
    std::string bytes;
    bytes.reserve(54 + pixel_bytes);
    bytes += "BM";
    append_le(bytes, static_cast<std::uint32_t>(54 + pixel_bytes), 4);
    append_le(bytes, 0, 4);
    append_le(bytes, 54, 4);
    append_le(bytes, 40, 4);
    append_le(bytes, width, 4);
    append_le(bytes, height, 4);
    append_le(bytes, 1, 2);
    append_le(bytes, 24, 2);
    append_le(bytes, 0, 4);
    append_le(bytes, pixel_bytes, 4);
    append_le(bytes, 2835, 4);
    append_le(bytes, 2835, 4);
    append_le(bytes, 0, 4);
    append_le(bytes, 0, 4);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const double factor = sample_texture(material.texture, material.texture_seed, x, y);
            for (const std::size_t channel : {2U, 1U, 0U}) {
                const auto value = static_cast<unsigned char>(std::clamp(
                    std::round(material.base_color[channel] * factor * 255.0), 0.0, 255.0));
                bytes.push_back(static_cast<char>(value));
            }
        }
        for (std::size_t padding = width * 3; padding < row_size; ++padding) {
            bytes.push_back('\0');
        }
    }
    return bytes;
}

[[nodiscard]] auto base64(std::string_view bytes) -> std::string {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t first = static_cast<unsigned char>(bytes[index]);
        const std::uint32_t second =
            index + 1 < bytes.size() ? static_cast<unsigned char>(bytes[index + 1]) : 0;
        const std::uint32_t third =
            index + 2 < bytes.size() ? static_cast<unsigned char>(bytes[index + 2]) : 0;
        const std::uint32_t value = (first << 16U) | (second << 8U) | third;
        result.push_back(alphabet[(value >> 18U) & 63U]);
        result.push_back(alphabet[(value >> 12U) & 63U]);
        result.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6U) & 63U] : '=');
        result.push_back(index + 2 < bytes.size() ? alphabet[value & 63U] : '=');
    }
    return result;
}

auto write_materials(std::ostream& output, const compiler::ir::Project& project) -> void {
    output << "\"materials\":[";
    for (std::size_t index = 0; index < project.materials.size(); ++index) {
        const auto& material = project.materials[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"name\":" << json_string(material.name)
               << ",\"profile\":" << json_string(material.profile)
               << ",\"preset\":" << json_string(material.preset) << ",\"baseColor\":["
               << material.base_color[0] << ',' << material.base_color[1] << ','
               << material.base_color[2] << ',' << material.base_color[3]
               << "],\"metallic\":" << material.metallic << ",\"roughness\":" << material.roughness
               << ",\"textureScaleMm\":" << material.texture_scale_mm
               << ",\"uvMode\":" << json_string(material.uv_mode)
               << ",\"texture\":{\"name\":" << json_string(material.texture)
               << ",\"mime\":\"image/bmp\",\"encoding\":\"base64\",\"dataUri\":\"data:image/"
                  "bmp;base64,"
               << base64(texture_bmp(material)) << "\"}}";
    }
    output << ']';
}

auto write_scenes(std::ostream& output, const compiler::ir::Project& project) -> void {
    output << "\"scenes\":[";
    for (std::size_t scene_index = 0; scene_index < project.scenes.size(); ++scene_index) {
        const auto& scene = project.scenes[scene_index];
        if (scene_index != 0) {
            output << ',';
        }
        output << "{\"name\":" << json_string(scene.name)
               << ",\"duration\":" << scene.duration_seconds
               << ",\"fps\":" << scene.frames_per_second
               << ",\"background\":" << json_string(scene.background)
               << ",\"loopCount\":" << scene.loop_count << ",\"lights\":[";
        for (std::size_t light_index = 0; light_index < scene.lights.size(); ++light_index) {
            const auto& light = scene.lights[light_index];
            if (light_index != 0) {
                output << ',';
            }
            output << "{\"name\":" << json_string(light.name)
                   << ",\"kind\":" << json_string(light.kind) << ",\"color\":["
                   << light.color[0] << ',' << light.color[1] << ',' << light.color[2]
                   << "],\"intensity\":" << light.intensity << ",\"positionMm\":["
                   << light.position_mm[0] << ',' << light.position_mm[1] << ','
                   << light.position_mm[2] << "]}";
        }
        output << "],\"events\":[";
        for (std::size_t event_index = 0; event_index < scene.events.size(); ++event_index) {
            const auto& event = scene.events[event_index];
            if (event_index != 0) {
                output << ',';
            }
            output << "{\"time\":" << event.time_seconds
                   << ",\"name\":" << json_string(event.name) << '}';
        }
        output << "],\"tracks\":[";
        for (std::size_t track_index = 0; track_index < scene.tracks.size(); ++track_index) {
            const auto& track = scene.tracks[track_index];
            if (track_index != 0) {
                output << ',';
            }
            output << "{\"name\":" << json_string(track.name)
                   << ",\"targetKind\":" << json_string(track.target_kind)
                   << ",\"target\":" << json_string(track.target)
                   << ",\"easing\":" << json_string(track.easing) << ",\"keyframes\":[";
            for (std::size_t frame_index = 0; frame_index < track.keyframes.size(); ++frame_index) {
                const auto& frame = track.keyframes[frame_index];
                if (frame_index != 0) {
                    output << ',';
                }
                output << "{\"time\":" << frame.time_seconds;
                if (track.target_kind == "VISIBILITY") {
                    output << ",\"visible\":" << (frame.visible ? "true" : "false");
                } else if (track.target_kind == "JOINT") {
                    output << ",\"value\":" << frame.joint_value
                           << ",\"unit\":" << json_string(frame.joint_unit);
                } else {
                    output << ",\"position\":[" << frame.transform.position_mm[0] << ','
                           << frame.transform.position_mm[1] << ','
                           << frame.transform.position_mm[2] << "],\"rotation\":["
                           << frame.transform.rotation_deg[0] << ','
                           << frame.transform.rotation_deg[1] << ','
                           << frame.transform.rotation_deg[2] << ']';
                }
                output << '}';
            }
            output << "]}";
        }
        output << "]}";
    }
    output << ']';
}

auto write_bodies(std::ostream& output, const compiler::ir::Project& project) -> void {
    output << "\"bodies\":[";
    for (std::size_t index = 0; index < project.bodies.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        const auto& body = project.bodies[index];
        output << "{\"name\":" << json_string(body.name)
               << ",\"material\":" << json_string(body.material);
        const auto pose =
            std::ranges::find(project.poses, body.name, &compiler::ir::BodyPose::body);
        if (pose != project.poses.end()) {
            output << ",\"pose\":{\"at\":" << json_string(pose->point) << ",\"position\":["
                   << pose->transform.position_mm[0] << ',' << pose->transform.position_mm[1] << ','
                   << pose->transform.position_mm[2] << "],\"rotation\":["
                   << pose->transform.rotation_deg[0] << ',' << pose->transform.rotation_deg[1]
                   << ',' << pose->transform.rotation_deg[2] << "]}";
        }
        output << '}';
    }
    output << ']';
}

[[nodiscard]] auto joint_kind(compiler::ir::JointKind kind) -> std::string_view {
    if (kind == compiler::ir::JointKind::revolute)
        return "REVOLUTE";
    if (kind == compiler::ir::JointKind::prismatic)
        return "PRISMATIC";
    return "FIXED";
}

auto write_mechanism(std::ostream& output, const compiler::ir::Project& project) -> void {
    output << "\"spatial\":{\"points\":[";
    for (std::size_t index = 0; index < project.points.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& point = project.points[index];
        output << "{\"name\":" << json_string(point.name) << ",\"position\":["
               << point.position_mm[0] << ',' << point.position_mm[1] << ',' << point.position_mm[2]
               << "]}";
    }
    output << "],\"vectors\":[";
    for (std::size_t index = 0; index < project.vectors.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& vector = project.vectors[index];
        output << "{\"name\":" << json_string(vector.name) << ",\"unit\":[" << vector.unit[0] << ','
               << vector.unit[1] << ',' << vector.unit[2] << "]}";
    }
    output << "]},\"joints\":[";
    for (std::size_t index = 0; index < project.joints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& joint = project.joints[index];
        const auto frame = joint_frame(project, joint);
        output << "{\"name\":" << json_string(joint.name)
               << ",\"type\":" << json_string(joint_kind(joint.kind))
               << ",\"parent\":" << json_string(joint.parent_body)
               << ",\"child\":" << json_string(joint.child_body)
               << ",\"point\":" << json_string(joint.point)
               << ",\"axis\":" << json_string(joint.axis) << ",\"value\":" << joint.value
               << ",\"limits\":[" << joint.lower_limit << ',' << joint.upper_limit
               << "],\"unit\":" << json_string(joint.unit) << ",\"pivotMm\":["
               << frame.pivot.x << ',' << frame.pivot.y << ',' << frame.pivot.z
               << "],\"axisUnit\":[" << frame.axis.x << ',' << frame.axis.y << ','
               << frame.axis.z << "]}";
    }
    output << "],\"connections\":[";
    for (std::size_t index = 0; index < project.connections.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& connection = project.connections[index];
        const auto first = std::ranges::find(project.interfaces, connection.first_interface,
                                             &compiler::ir::ComponentInterface::name);
        const auto second = std::ranges::find(project.interfaces, connection.second_interface,
                                              &compiler::ir::ComponentInterface::name);
        const auto point = first == project.interfaces.end()
                               ? project.points.end()
                               : std::ranges::find(project.points, first->point,
                                                   &compiler::ir::SpatialPoint::name);
        output << "{\"name\":" << json_string(connection.name)
               << ",\"method\":" << json_string(connection.method)
               << ",\"standard\":" << json_string(connection.standard)
               << ",\"firstBody\":"
               << json_string(first == project.interfaces.end() ? "" : first->occurrence)
               << ",\"secondBody\":"
               << json_string(second == project.interfaces.end() ? "" : second->occurrence)
               << ",\"pointMm\":[";
        if (point == project.points.end())
            output << "0,0,0";
        else
            output << point->position_mm[0] << ',' << point->position_mm[1] << ','
                   << point->position_mm[2];
        output << "],\"clearanceMm\":" << connection.clearance_mm
               << ",\"gapMm\":" << connection.interface_gap_mm
               << ",\"aligned\":" << (connection.aligned ? "true" : "false") << '}';
    }
    output << ']';
}

auto write_model(std::ostream& output, const compiler::ir::Project& project,
                 const cad::Model* geometry, std::string_view basename) -> void {
    output << std::setprecision(17)
           << "{\"format\":\"ICAD_SCENE\",\"version\":1,\"project\":" << json_string(project.name)
           << ",\"units\":\"mm\",\"geometryFiles\":{\"step\":"
           << json_string(std::string{basename} + ".step")
           << ",\"assemblyStep\":" << json_string(std::string{basename} + ".assembly.step")
           << ",\"obj\":" << json_string(std::string{basename} + ".obj")
           << ",\"stl\":" << json_string(std::string{basename} + ".stl")
           << ",\"gltf\":" << json_string(std::string{basename} + ".gltf")
           << ",\"glb\":" << json_string(std::string{basename} + ".glb")
           << ",\"threeMf\":" << json_string(std::string{basename} + ".3mf")
           << ",\"drawingDxf\":" << json_string(std::string{basename} + ".drawing.dxf")
           << "},";
    write_materials(output, project);
    output << ',';
    write_bodies(output, project);
    output << ',';
    write_mechanism(output, project);
    output << ',';
    write_scenes(output, project);
    output << ',';
    write_dependency_graph(output, project);
    if (geometry != nullptr) {
        output << ",\"parts\":[";
        for (std::size_t part_index = 0; part_index < geometry->parts.size(); ++part_index) {
            const auto& part = geometry->parts[part_index];
            if (part_index != 0) {
                output << ',';
            }
            output << "{\"name\":" << json_string(part.name)
                   << ",\"body\":" << json_string(part.body)
                   << ",\"material\":" << json_string(part.material) << ",\"vertices\":[";
            for (std::size_t vertex = 0; vertex < part.vertices.size(); ++vertex) {
                if (vertex != 0) {
                    output << ',';
                }
                const auto& point = part.vertices[vertex];
                output << '[' << point.x << ',' << point.y << ',' << point.z << ']';
            }
            output << "],\"triangles\":[";
            for (std::size_t face = 0; face < part.triangles.size(); ++face) {
                if (face != 0) {
                    output << ',';
                }
                const auto& triangle = part.triangles[face];
                output << '[' << triangle[0] << ',' << triangle[1] << ',' << triangle[2] << ']';
            }
            output << "]}";
        }
        output << ']';
    }
    output << '}';
}

} // namespace

auto export_scene(const compiler::ir::Project& project,
                  const std::filesystem::path& output_base) -> ExportResult {
    const auto model = cad::build_model(project);
    return export_scene(project, model, output_base);
}

auto export_scene(const compiler::ir::Project& project, const cad::Model& model,
                  const std::filesystem::path& output_base) -> ExportResult {
    if (!cad::is_valid(model)) {
        return {false, "ICAD geometry validation failed before native-scene export"};
    }
    if (!output_base.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(output_base.parent_path(), error);
        if (error) {
            return {false, "cannot create scene output directory: " + error.message()};
        }
    }
    const std::string basename = output_base.filename().string();
    std::error_code ignored;
    std::filesystem::remove(output_base.string() + ".html", ignored);
    ignored.clear();
    std::filesystem::remove(output_base.string() + ".viewer.js", ignored);
    ignored.clear();
    std::filesystem::remove(output_base.parent_path() / "icad-viewer.js", ignored);
    const auto scene_path = output_base.string() + ".scene.json";
    std::ofstream scene{scene_path, std::ios::binary};
    if (!scene) {
        return {false, "cannot open native scene output file"};
    }
    write_model(scene, project, &model, basename);
    if (!scene) {
        return {false, "failed while writing native scene output"};
    }

    std::size_t tracks = 0;
    std::size_t keyframes = 0;
    for (const auto& animation_scene : project.scenes) {
        tracks += animation_scene.tracks.size();
        for (const auto& track : animation_scene.tracks) {
            keyframes += track.keyframes.size();
        }
    }
    return {true,
            "native ICAD render scene complete",
            project.materials.size(),
            project.scenes.size(),
            tracks,
            keyframes};
}

auto render_model_json(const compiler::ir::Project& project, const cad::Model& model,
                       std::string_view basename) -> std::string {
    if (!cad::is_valid(model))
        return {};
    std::ostringstream output;
    write_model(output, project, &model, basename);
    return output.str();
}

} // namespace icad::scene
