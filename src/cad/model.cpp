#include "model.hpp"

#include "boolean.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <string_view>
#include <thread>
#include <utility>

namespace icad::cad {
namespace {

constexpr std::size_t radial_segments = 32;
constexpr std::size_t sphere_latitude_segments = 16;
constexpr std::size_t sphere_longitude_segments = 32;

[[nodiscard]] auto radial_segments_for(double radius) -> std::size_t {
    static_cast<void>(radius);
    return radial_segments;
}

[[nodiscard]] auto property(const compiler::ir::Feature& feature, std::string_view name,
                            double fallback = 0.0) -> double {
    const auto found = std::ranges::find(feature.properties, name, &compiler::ir::Property::name);
    return found == feature.properties.end() ? fallback : found->value.value;
}

[[nodiscard]] auto direction(const compiler::ir::Project& project, std::string_view name)
    -> Vector3 {
    const auto found = std::ranges::find(project.vectors, name, &compiler::ir::Direction::name);
    return found == project.vectors.end()
               ? Vector3{}
               : Vector3{found->unit[0], found->unit[1], found->unit[2]};
}

[[nodiscard]] auto spatial_point(const compiler::ir::Project& project, std::string_view name)
    -> Point3 {
    const auto found = std::ranges::find(project.points, name, &compiler::ir::SpatialPoint::name);
    return found == project.points.end()
               ? Point3{}
               : Point3{found->position_mm[0], found->position_mm[1], found->position_mm[2]};
}

auto add_triangle(Part& part, std::size_t first, std::size_t second, std::size_t third) -> void {
    part.triangles.push_back({first, second, third});
}

[[nodiscard]] auto make_box(const compiler::ir::Feature& feature) -> Part {
    const double width = property(feature, "WIDTH");
    const double depth = property(feature, "DEPTH");
    const double height = property(feature, "HEIGHT");
    Part part;
    part.vertices = {
        {0, 0, 0},      {width, 0, 0},      {width, depth, 0},      {0, depth, 0},
        {0, 0, height}, {width, 0, height}, {width, depth, height}, {0, depth, height}};
    part.triangles = {{{0, 2, 1}}, {{0, 3, 2}}, {{4, 5, 6}}, {{4, 6, 7}}, {{0, 1, 5}}, {{0, 5, 4}},
                      {{1, 2, 6}}, {{1, 6, 5}}, {{2, 3, 7}}, {{2, 7, 6}}, {{3, 0, 4}}, {{3, 4, 7}}};
    return part;
}

[[nodiscard]] auto make_frustum(const compiler::ir::Feature& feature, double bottom_radius,
                                double top_radius) -> Part {
    const double height = property(feature, "HEIGHT");
    Part part;
    const bool bottom_apex = bottom_radius <= 1e-9;
    const bool top_apex = top_radius <= 1e-9;
    const std::size_t segments = radial_segments_for(std::max(bottom_radius, top_radius));
    part.vertices.reserve(segments * 2 + 2);
    if (bottom_apex) {
        part.vertices.push_back({0.0, 0.0, 0.0});
    } else {
        for (std::size_t index = 0; index < segments; ++index) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                                 static_cast<double>(segments);
            part.vertices.push_back(
                {bottom_radius * std::cos(angle), bottom_radius * std::sin(angle), 0.0});
        }
    }
    const std::size_t top_start = part.vertices.size();
    if (top_apex) {
        part.vertices.push_back({0.0, 0.0, height});
    } else {
        for (std::size_t index = 0; index < segments; ++index) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                                 static_cast<double>(segments);
            part.vertices.push_back(
                {top_radius * std::cos(angle), top_radius * std::sin(angle), height});
        }
    }
    const std::size_t bottom_center = part.vertices.size();
    if (!bottom_apex)
        part.vertices.push_back({0, 0, 0});
    const std::size_t top_center = part.vertices.size();
    if (!top_apex)
        part.vertices.push_back({0, 0, height});
    for (std::size_t index = 0; index < segments; ++index) {
        const std::size_t next = (index + 1) % segments;
        if (bottom_apex) {
            add_triangle(part, 0, top_start + next, top_start + index);
        } else if (top_apex) {
            add_triangle(part, index, next, top_start);
        } else {
            add_triangle(part, index, top_start + next, top_start + index);
            add_triangle(part, index, next, top_start + next);
        }
        if (!bottom_apex)
            add_triangle(part, bottom_center, next, index);
        if (!top_apex)
            add_triangle(part, top_center, top_start + index, top_start + next);
    }
    return part;
}

[[nodiscard]] auto make_sphere(const compiler::ir::Feature& feature) -> Part {
    const double radius = property(feature, "RADIUS");
    Part part;
    part.vertices.push_back({0, 0, radius});
    for (std::size_t latitude = 1; latitude < sphere_latitude_segments; ++latitude) {
        const double phi = std::numbers::pi * static_cast<double>(latitude) /
                           static_cast<double>(sphere_latitude_segments);
        for (std::size_t longitude = 0; longitude < sphere_longitude_segments; ++longitude) {
            const double theta = 2.0 * std::numbers::pi * static_cast<double>(longitude) /
                                 static_cast<double>(sphere_longitude_segments);
            part.vertices.push_back({radius * std::sin(phi) * std::cos(theta),
                                     radius * std::sin(phi) * std::sin(theta),
                                     radius * std::cos(phi)});
        }
    }
    const std::size_t south = part.vertices.size();
    part.vertices.push_back({0, 0, -radius});
    for (std::size_t longitude = 0; longitude < sphere_longitude_segments; ++longitude) {
        const std::size_t next = (longitude + 1) % sphere_longitude_segments;
        add_triangle(part, 0, 1 + longitude, 1 + next);
        for (std::size_t latitude = 1; latitude + 1 < sphere_latitude_segments; ++latitude) {
            const std::size_t first = 1 + (latitude - 1) * sphere_longitude_segments + longitude;
            const std::size_t first_next =
                1 + (latitude - 1) * sphere_longitude_segments + next;
            const std::size_t second = 1 + latitude * sphere_longitude_segments + longitude;
            const std::size_t second_next = 1 + latitude * sphere_longitude_segments + next;
            add_triangle(part, first, second, second_next);
            add_triangle(part, first, second_next, first_next);
        }
        const std::size_t last =
            1 + (sphere_latitude_segments - 2) * sphere_longitude_segments + longitude;
        const std::size_t last_next =
            1 + (sphere_latitude_segments - 2) * sphere_longitude_segments + next;
        add_triangle(part, south, last_next, last);
    }
    return part;
}

[[nodiscard]] auto point_in_triangle(const compiler::ir::Point2& point,
                                     const compiler::ir::Point2& a, const compiler::ir::Point2& b,
                                     const compiler::ir::Point2& c) -> bool {
    const auto cross = [](const compiler::ir::Point2& first, const compiler::ir::Point2& second,
                          const compiler::ir::Point2& candidate) {
        return (second.x_mm - first.x_mm) * (candidate.y_mm - first.y_mm) -
               (second.y_mm - first.y_mm) * (candidate.x_mm - first.x_mm);
    };
    constexpr double tolerance = 1e-9;
    return cross(a, b, point) >= -tolerance && cross(b, c, point) >= -tolerance &&
           cross(c, a, point) >= -tolerance;
}

[[nodiscard]] auto triangulate_profile(const compiler::ir::Profile& profile)
    -> std::vector<Triangle> {
    std::vector<std::size_t> remaining(profile.points.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    std::vector<Triangle> triangles;
    while (remaining.size() > 3) {
        bool clipped = false;
        for (std::size_t cursor = 0; cursor < remaining.size(); ++cursor) {
            const std::size_t previous =
                remaining[(cursor + remaining.size() - 1) % remaining.size()];
            const std::size_t current = remaining[cursor];
            const std::size_t next = remaining[(cursor + 1) % remaining.size()];
            const auto& a = profile.points[previous];
            const auto& b = profile.points[current];
            const auto& c = profile.points[next];
            const double turn =
                (b.x_mm - a.x_mm) * (c.y_mm - a.y_mm) - (b.y_mm - a.y_mm) * (c.x_mm - a.x_mm);
            if (turn <= 1e-9) {
                continue;
            }
            bool contains_point = false;
            for (const auto candidate : remaining) {
                if (candidate != previous && candidate != current && candidate != next &&
                    point_in_triangle(profile.points[candidate], a, b, c)) {
                    contains_point = true;
                    break;
                }
            }
            if (contains_point) {
                continue;
            }
            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(cursor));
            clipped = true;
            break;
        }
        if (!clipped) {
            return {};
        }
    }
    if (remaining.size() == 3) {
        triangles.push_back({remaining[0], remaining[1], remaining[2]});
    }
    return triangles;
}

[[nodiscard]] auto make_extrude(const compiler::ir::Feature& feature,
                                const compiler::ir::Profile& profile) -> Part {
    const double height = property(feature, "HEIGHT");
    const std::size_t count = profile.points.size();
    Part part;
    part.vertices.reserve(count * 2);
    for (const auto& point : profile.points) {
        part.vertices.push_back({point.x_mm, point.y_mm, 0.0});
    }
    for (const auto& point : profile.points) {
        part.vertices.push_back({point.x_mm, point.y_mm, height});
    }
    for (const auto& triangle : triangulate_profile(profile)) {
        add_triangle(part, triangle[0], triangle[2], triangle[1]);
        add_triangle(part, count + triangle[0], count + triangle[1], count + triangle[2]);
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t next = (index + 1) % count;
        add_triangle(part, index, next, count + next);
        add_triangle(part, index, count + next, count + index);
    }
    return part;
}

[[nodiscard]] auto make_revolve(const compiler::ir::Profile& profile,
                                std::size_t segments = radial_segments) -> Part {
    const std::size_t points = profile.points.size();
    Part part;
    part.vertices.reserve(segments * points);
    for (std::size_t segment = 0; segment < segments; ++segment) {
        const double angle =
            2.0 * std::numbers::pi * static_cast<double>(segment) / static_cast<double>(segments);
        for (const auto& point : profile.points) {
            part.vertices.push_back(
                {point.x_mm * std::cos(angle), point.x_mm * std::sin(angle), point.y_mm});
        }
    }
    for (std::size_t segment = 0; segment < segments; ++segment) {
        const std::size_t next_segment = (segment + 1) % segments;
        for (std::size_t point = 0; point < points; ++point) {
            const std::size_t next_point = (point + 1) % points;
            const std::size_t a = segment * points + point;
            const std::size_t b = next_segment * points + point;
            const std::size_t c = next_segment * points + next_point;
            const std::size_t d = segment * points + next_point;
            add_triangle(part, a, b, c);
            add_triangle(part, a, c, d);
        }
    }
    return part;
}

[[nodiscard]] auto resample_profile(const std::vector<compiler::ir::Point2>& source,
                                    std::size_t count)
    -> std::vector<compiler::ir::Point2> {
    std::vector<double> lengths(source.size());
    double perimeter = 0.0;
    for (std::size_t edge = 0; edge < source.size(); ++edge) {
        const auto& first = source[edge];
        const auto& second = source[(edge + 1) % source.size()];
        lengths[edge] = std::hypot(second.x_mm - first.x_mm, second.y_mm - first.y_mm);
        perimeter += lengths[edge];
    }
    std::vector<compiler::ir::Point2> result;
    result.reserve(count);
    std::size_t edge = 0;
    double edge_start = 0.0;
    for (std::size_t sample = 0; sample < count; ++sample) {
        const double target = perimeter * static_cast<double>(sample) / static_cast<double>(count);
        while (edge + 1 < source.size() && target > edge_start + lengths[edge]) {
            edge_start += lengths[edge];
            ++edge;
        }
        const double amount = lengths[edge] <= 1e-12 ? 0.0 : (target - edge_start) / lengths[edge];
        const auto& first = source[edge];
        const auto& second = source[(edge + 1) % source.size()];
        result.push_back({first.x_mm + (second.x_mm - first.x_mm) * amount,
                          first.y_mm + (second.y_mm - first.y_mm) * amount});
    }
    return result;
}

[[nodiscard]] auto sectioned_solid(const std::vector<std::vector<Point3>>& sections,
                                   const std::vector<compiler::ir::Point2>& cap_profile) -> Part {
    Part part;
    if (sections.size() < 2 || cap_profile.size() < 3)
        return part;
    const std::size_t count = cap_profile.size();
    for (const auto& section : sections)
        part.vertices.insert(part.vertices.end(), section.begin(), section.end());

    compiler::ir::Profile triangulation_profile;
    triangulation_profile.points = cap_profile;
    for (const auto& triangle : triangulate_profile(triangulation_profile)) {
        add_triangle(part, triangle[0], triangle[2], triangle[1]);
        const auto top = (sections.size() - 1) * count;
        add_triangle(part, top + triangle[0], top + triangle[1], top + triangle[2]);
    }
    for (std::size_t section = 0; section + 1 < sections.size(); ++section) {
        const auto first = section * count;
        const auto second = (section + 1) * count;
        for (std::size_t point = 0; point < count; ++point) {
            const auto next = (point + 1) % count;
            add_triangle(part, first + point, first + next, second + next);
            add_triangle(part, first + point, second + next, second + point);
        }
    }
    return part;
}

[[nodiscard]] auto make_sweep(const compiler::ir::Project& project,
                              const compiler::ir::Feature& feature,
                              const compiler::ir::Profile& profile) -> Part {
    const auto subtract_points = [](const Point3& first, const Point3& second) {
        return Vector3{first.x - second.x, first.y - second.y, first.z - second.z};
    };
    const auto vector_dot = [](const Vector3& first, const Vector3& second) {
        return first.x * second.x + first.y * second.y + first.z * second.z;
    };
    const auto vector_cross = [](const Vector3& first, const Vector3& second) {
        return Vector3{first.y * second.z - first.z * second.y,
                       first.z * second.x - first.x * second.z,
                       first.x * second.y - first.y * second.x};
    };
    const auto normalize = [&vector_dot](const Vector3& vector) {
        const double length = std::sqrt(vector_dot(vector, vector));
        return Vector3{vector.x / length, vector.y / length, vector.z / length};
    };
    std::vector<std::vector<Point3>> sections;
    sections.reserve(feature.path_points.size());
    std::vector<Point3> path;
    path.reserve(feature.path_points.size());
    for (const auto& name : feature.path_points)
        path.push_back(spatial_point(project, name));

    Vector3 previous_u{};
    for (std::size_t path_index = 0; path_index < path.size(); ++path_index) {
        const Point3 origin = path[path_index];
        const Point3 tangent_start = path_index == 0 ? path[path_index] : path[path_index - 1];
        const Point3 tangent_end = path_index + 1 == path.size() ? path[path_index]
                                                                  : path[path_index + 1];
        const Vector3 tangent = normalize(subtract_points(tangent_end, tangent_start));

        // A sweep profile is defined in its local (u, v) section plane.  Construct
        // that plane perpendicular to the path instead of translating an XY
        // profile unchanged: the latter creates zero-area faces for horizontal
        // and inclined structural members.  The vertical-path special case keeps
        // the historical XY orientation, while the sign check prevents an
        // avoidable 180-degree section flip along a polyline.
        const Vector3 reference = std::abs(tangent.z) > 0.9 ? Vector3{0.0, 1.0, 0.0}
                                                            : Vector3{0.0, 0.0, 1.0};
        Vector3 u = normalize(vector_cross(reference, tangent));
        if (path_index != 0 && vector_dot(u, previous_u) < 0.0)
            u = {-u.x, -u.y, -u.z};
        const Vector3 v = normalize(vector_cross(tangent, u));
        previous_u = u;

        std::vector<Point3> section;
        section.reserve(profile.points.size());
        for (const auto& point : profile.points) {
            section.push_back({origin.x + point.x_mm * u.x + point.y_mm * v.x,
                               origin.y + point.x_mm * u.y + point.y_mm * v.y,
                               origin.z + point.x_mm * u.z + point.y_mm * v.z});
        }
        sections.push_back(std::move(section));
    }
    return sectioned_solid(sections, profile.points);
}

[[nodiscard]] auto make_loft(const compiler::ir::Profile& first,
                             const compiler::ir::Profile& second, double height,
                             std::size_t section_count, double twist_degrees) -> Part {
    const std::size_t point_count = std::max(first.points.size(), second.points.size());
    const auto first_points = resample_profile(first.points, point_count);
    const auto second_points = resample_profile(second.points, point_count);
    compiler::ir::Point2 first_center{};
    compiler::ir::Point2 second_center{};
    for (std::size_t point = 0; point < point_count; ++point) {
        first_center.x_mm += first_points[point].x_mm / static_cast<double>(point_count);
        first_center.y_mm += first_points[point].y_mm / static_cast<double>(point_count);
        second_center.x_mm += second_points[point].x_mm / static_cast<double>(point_count);
        second_center.y_mm += second_points[point].y_mm / static_cast<double>(point_count);
    }
    std::vector<std::vector<Point3>> sections;
    sections.reserve(section_count);
    for (std::size_t section = 0; section < section_count; ++section) {
        const double amount = static_cast<double>(section) /
                              static_cast<double>(section_count - 1);
        const double angle = twist_degrees * amount * std::numbers::pi / 180.0;
        const double center_x = first_center.x_mm +
                                (second_center.x_mm - first_center.x_mm) * amount;
        const double center_y = first_center.y_mm +
                                (second_center.y_mm - first_center.y_mm) * amount;
        std::vector<Point3> points;
        points.reserve(point_count);
        for (std::size_t point = 0; point < point_count; ++point) {
            const double x = first_points[point].x_mm +
                             (second_points[point].x_mm - first_points[point].x_mm) * amount;
            const double y = first_points[point].y_mm +
                             (second_points[point].y_mm - first_points[point].y_mm) * amount;
            const double relative_x = x - center_x;
            const double relative_y = y - center_y;
            points.push_back({center_x + relative_x * std::cos(angle) -
                                             relative_y * std::sin(angle),
                              center_y + relative_x * std::sin(angle) +
                                             relative_y * std::cos(angle),
                              height * amount});
        }
        sections.push_back(std::move(points));
    }
    return sectioned_solid(sections, first_points);
}

auto rotate_x(Point3& point, double radians) -> void {
    const double y = point.y * std::cos(radians) - point.z * std::sin(radians);
    const double z = point.y * std::sin(radians) + point.z * std::cos(radians);
    point.y = y;
    point.z = z;
}

auto rotate_y(Point3& point, double radians) -> void {
    const double x = point.x * std::cos(radians) + point.z * std::sin(radians);
    const double z = -point.x * std::sin(radians) + point.z * std::cos(radians);
    point.x = x;
    point.z = z;
}

auto rotate_z(Point3& point, double radians) -> void {
    const double x = point.x * std::cos(radians) - point.y * std::sin(radians);
    const double y = point.x * std::sin(radians) + point.y * std::cos(radians);
    point.x = x;
    point.y = y;
}

auto apply_transform(Part& part, const compiler::ir::Feature& feature) -> void {
    const double radians = std::numbers::pi / 180.0;
    const double rotation_x = property(feature, "ROTATION_X") * radians;
    const double rotation_y = property(feature, "ROTATION_Y") * radians;
    const double rotation_z = property(feature, "ROTATION_Z") * radians;
    const Point3 origin{property(feature, "ORIGIN_X"), property(feature, "ORIGIN_Y"),
                        property(feature, "ORIGIN_Z")};
    for (auto& point : part.vertices) {
        rotate_x(point, rotation_x);
        rotate_y(point, rotation_y);
        rotate_z(point, rotation_z);
        point.x += origin.x;
        point.y += origin.y;
        point.z += origin.z;
    }
}

auto apply_transform(Part& part, const compiler::ir::Transform& transform) -> void {
    const double radians = std::numbers::pi / 180.0;
    for (auto& point : part.vertices) {
        rotate_x(point, transform.rotation_deg[0] * radians);
        rotate_y(point, transform.rotation_deg[1] * radians);
        rotate_z(point, transform.rotation_deg[2] * radians);
        point.x += transform.position_mm[0];
        point.y += transform.position_mm[1];
        point.z += transform.position_mm[2];
    }
}

auto rotate_axis(Point3& point, const Point3& pivot, const Vector3& axis, double radians) -> void {
    const Point3 relative{point.x - pivot.x, point.y - pivot.y, point.z - pivot.z};
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const Point3 cross{axis.y * relative.z - axis.z * relative.y,
                       axis.z * relative.x - axis.x * relative.z,
                       axis.x * relative.y - axis.y * relative.x};
    const double projection =
        axis.x * relative.x + axis.y * relative.y + axis.z * relative.z;
    point = {pivot.x + relative.x * cosine + cross.x * sine + axis.x * projection * (1.0 - cosine),
             pivot.y + relative.y * cosine + cross.y * sine + axis.y * projection * (1.0 - cosine),
             pivot.z + relative.z * cosine + cross.z * sine + axis.z * projection * (1.0 - cosine)};
}

auto apply_joint_chain(Part& part, const compiler::ir::Project& project,
                       const std::string& occurrence) -> void {
    std::vector<const compiler::ir::Joint*> chain;
    std::string child = occurrence;
    for (std::size_t guard = 0; guard <= project.joints.size(); ++guard) {
        const auto joint =
            std::ranges::find(project.joints, child, &compiler::ir::Joint::child_body);
        if (joint == project.joints.end())
            break;
        chain.push_back(&*joint);
        if (joint->parent_body == "WORLD")
            break;
        child = joint->parent_body;
    }
    std::ranges::reverse(chain);
    struct AppliedOperation {
        compiler::ir::JointKind kind;
        Point3 point;
        Vector3 axis;
        double value;
    };
    std::vector<AppliedOperation> applied;
    for (const auto* joint : chain) {
        if (joint->kind == compiler::ir::JointKind::fixed)
            continue;
        const auto point_value =
            std::ranges::find(project.points, joint->point, &compiler::ir::SpatialPoint::name);
        const auto axis_value =
            std::ranges::find(project.vectors, joint->axis, &compiler::ir::Direction::name);
        if (point_value == project.points.end() || axis_value == project.vectors.end())
            continue;
        Point3 pivot{point_value->position_mm[0], point_value->position_mm[1],
                     point_value->position_mm[2]};
        Vector3 axis{axis_value->unit[0], axis_value->unit[1], axis_value->unit[2]};
        for (const auto& operation : applied) {
            if (operation.kind == compiler::ir::JointKind::revolute) {
                rotate_axis(pivot, operation.point, operation.axis,
                            operation.value * std::numbers::pi / 180.0);
                Point3 rotated_axis = axis;
                rotate_axis(rotated_axis, {}, operation.axis,
                            operation.value * std::numbers::pi / 180.0);
                axis = rotated_axis;
            } else {
                pivot.x += operation.axis.x * operation.value;
                pivot.y += operation.axis.y * operation.value;
                pivot.z += operation.axis.z * operation.value;
            }
        }
        AppliedOperation operation{joint->kind, pivot, axis, joint->value};
        for (auto& vertex : part.vertices) {
            if (joint->kind == compiler::ir::JointKind::revolute) {
                rotate_axis(vertex, pivot, axis, joint->value * std::numbers::pi / 180.0);
            } else {
                vertex.x += axis.x * joint->value;
                vertex.y += axis.y * joint->value;
                vertex.z += axis.z * joint->value;
            }
        }
        applied.push_back(operation);
    }
}

auto orient_history_part(Part& part, const compiler::ir::Feature& feature,
                         const Part* support) -> void {
    if (feature.support_face.empty()) {
        if (feature.sketch_plane == "XZ") {
            for (auto& point : part.vertices)
                point = {point.x, point.z, point.y};
        } else if (feature.sketch_plane == "YZ") {
            for (auto& point : part.vertices)
                point = {point.z, point.x, point.y};
        }
        return;
    }
    if (support == nullptr || support->vertices.empty())
        return;
    Point3 minimum{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max()};
    Point3 maximum{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                   std::numeric_limits<double>::lowest()};
    for (const auto& point : support->vertices) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    const bool cut = feature.operation == compiler::ir::FeatureOperation::cut;
    double local_minimum = std::numeric_limits<double>::max();
    double local_maximum = std::numeric_limits<double>::lowest();
    for (const auto& point : part.vertices) {
        local_minimum = std::min(local_minimum, point.z);
        local_maximum = std::max(local_maximum, point.z);
    }
    const double local_depth = local_maximum - local_minimum;
    constexpr double face_overlap = 1.0e-2;
    const auto progress = [&](double local_z) {
        return local_depth <= 0.0 ? 0.0 : (local_z - local_minimum) / local_depth;
    };
    const auto from_maximum_face = [&](double face, double local_z) {
        const double distance = progress(local_z) * (local_depth + face_overlap);
        return cut ? face + face_overlap - distance : face - face_overlap + distance;
    };
    const auto from_minimum_face = [&](double face, double local_z) {
        const double distance = progress(local_z) * (local_depth + face_overlap);
        return cut ? face - face_overlap + distance : face + face_overlap - distance;
    };
    for (auto& point : part.vertices) {
        const Point3 local = point;
        if (feature.support_face == "Z_MAX")
            point = {local.x, local.y, from_maximum_face(maximum.z, local.z)};
        else if (feature.support_face == "Z_MIN")
            point = {local.x, local.y, from_minimum_face(minimum.z, local.z)};
        else if (feature.support_face == "X_MAX")
            point = {from_maximum_face(maximum.x, local.z), local.x, local.y};
        else if (feature.support_face == "X_MIN")
            point = {from_minimum_face(minimum.x, local.z), local.x, local.y};
        else if (feature.support_face == "Y_MAX")
            point = {local.x, from_maximum_face(maximum.y, local.z), local.y};
        else if (feature.support_face == "Y_MIN")
            point = {local.x, from_minimum_face(minimum.y, local.z), local.y};
    }
    const bool reverse_winding =
        ((feature.support_face == "Z_MAX" || feature.support_face == "X_MAX") && cut) ||
        ((feature.support_face == "Z_MIN" || feature.support_face == "X_MIN") && !cut) ||
        (feature.support_face == "Y_MAX" && !cut) ||
        (feature.support_face == "Y_MIN" && cut);
    if (reverse_winding) {
        for (auto& triangle : part.triangles)
            std::swap(triangle[1], triangle[2]);
    }
}

[[nodiscard]] auto make_part(const compiler::ir::Project& project,
                             const compiler::ir::Feature& feature,
                             const Part* support) -> Part {
    Part part;
    if (feature.type == "BOX") {
        part = make_box(feature);
    } else if (feature.type == "CYLINDER") {
        part = make_frustum(feature, property(feature, "RADIUS"), property(feature, "RADIUS"));
    } else if (feature.type == "CONE") {
        part = make_frustum(feature, property(feature, "RADIUS1"), property(feature, "RADIUS2"));
    } else if (feature.type == "SPHERE") {
        part = make_sphere(feature);
    } else if (feature.type == "EXTRUDE" || feature.type == "REVOLVE" ||
               feature.type == "SWEEP" || feature.type == "LOFT" ||
               feature.type == "FREEFORM") {
        const auto profile =
            std::ranges::find(project.profiles, feature.profile, &compiler::ir::Profile::name);
        if (profile != project.profiles.end()) {
            if (feature.type == "EXTRUDE")
                part = make_extrude(feature, *profile);
            else if (feature.type == "REVOLVE")
                part = make_revolve(*profile);
            else if (feature.type == "SWEEP")
                part = make_sweep(project, feature, *profile);
            else {
                const auto target = std::ranges::find(
                    project.profiles, feature.target_profile, &compiler::ir::Profile::name);
                if (target != project.profiles.end())
                    part = make_loft(*profile, *target, property(feature, "HEIGHT"),
                                     feature.type == "FREEFORM" ? feature.count : 2,
                                     feature.type == "FREEFORM" ? property(feature, "TWIST")
                                                                : 0.0);
            }
        }
    }
    orient_history_part(part, feature, support);
    apply_transform(part, feature);
    return part;
}

auto append_geometry(Part& destination, const Part& source) -> void {
    const std::size_t offset = destination.vertices.size();
    destination.vertices.insert(destination.vertices.end(), source.vertices.begin(),
                                source.vertices.end());
    for (const auto& triangle : source.triangles) {
        destination.triangles.push_back(
            {triangle[0] + offset, triangle[1] + offset, triangle[2] + offset});
    }
}

[[nodiscard]] auto make_region_part(const compiler::ir::Project& project,
                                    const compiler::ir::Feature& feature,
                                    const Part* support) -> Part {
    Part material = make_part(project, feature, support);
    if (feature.region_hole_profiles.empty())
        return material;

    Part holes;
    for (const auto& hole_profile : feature.region_hole_profiles) {
        auto hole_feature = feature;
        hole_feature.profile = hole_profile;
        hole_feature.region.clear();
        hole_feature.region_hole_profiles.clear();
        append_geometry(holes, make_part(project, hole_feature, support));
    }
    auto result = apply_boolean(material, holes, compiler::ir::FeatureOperation::cut,
                                feature.name + "_region");
    result.part.boolean_result = true;
    result.part.repairs = std::move(result.repairs);
    result.part.repairs.push_back(
        "subtracted " + std::to_string(feature.region_hole_profiles.size()) +
        " REGION hole profile(s) in one boolean transaction");
    return result.part;
}

[[nodiscard]] auto compatible_cut_batch(const compiler::ir::Feature& first,
                                        const compiler::ir::Feature& second) -> bool {
    // A BSP subtraction can consume a disconnected cutter set in one pass.
    // Keep face-attached history in one support frame, but batch primitive,
    // profile, and transformed radial cutters instead of rebuilding the
    // increasingly complex result after every hole.
    if (first.operation != compiler::ir::FeatureOperation::cut ||
        second.operation != compiler::ir::FeatureOperation::cut || first.type != second.type ||
        first.sketch_plane != second.sketch_plane ||
        first.support_feature != second.support_feature ||
        first.support_face != second.support_face) {
        return false;
    }
    constexpr double tolerance = 1e-9;
    const auto equivalent = [&](std::string_view name) {
        return std::abs(property(first, name) - property(second, name)) <= tolerance;
    };
    if (first.type == "CYLINDER") {
        return equivalent("RADIUS") && equivalent("HEIGHT");
    }
    if (first.type == "CONE")
        return equivalent("RADIUS1") && equivalent("RADIUS2") && equivalent("HEIGHT");
    if (first.type == "BOX")
        return equivalent("WIDTH") && equivalent("DEPTH") && equivalent("HEIGHT");
    return first.type == "EXTRUDE" || first.type == "LOFT" || first.type == "FREEFORM";
}

[[nodiscard]] auto triangle_area_twice(const Part& part, const Triangle& triangle) -> double {
    const Point3& a = part.vertices[triangle[0]];
    const Point3& b = part.vertices[triangle[1]];
    const Point3& c = part.vertices[triangle[2]];
    const Point3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Point3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Point3 cross{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z,
                       ab.x * ac.y - ab.y * ac.x};
    return std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
}

[[nodiscard]] auto valid_part(const Part& part) -> bool {
    if (part.vertices.empty() || part.triangles.empty()) {
        return false;
    }
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> edges;
    for (const auto& triangle : part.triangles) {
        if (triangle[0] >= part.vertices.size() || triangle[1] >= part.vertices.size() ||
            triangle[2] >= part.vertices.size() || triangle_area_twice(part, triangle) <= 1e-9) {
            return false;
        }
        for (std::size_t edge = 0; edge < 3; ++edge) {
            auto first = triangle[edge];
            auto second = triangle[(edge + 1) % 3];
            if (first > second) {
                std::swap(first, second);
            }
            ++edges[{first, second}];
        }
    }
    return std::ranges::all_of(edges, [](const auto& edge) { return edge.second == 2; });
}

[[nodiscard]] auto linear_pattern(const Part& source, const compiler::ir::Project& project,
                                  const compiler::ir::Feature& feature) -> Part {
    Part result = source;
    const Vector3 axis = direction(project, feature.direction);
    const double spacing = property(feature, "SPACING");
    const auto source_vertex_count = source.vertices.size();
    for (std::size_t instance = 1; instance < feature.count; ++instance) {
        const auto vertex_offset = result.vertices.size();
        const double distance = spacing * static_cast<double>(instance);
        for (const auto& vertex : source.vertices) {
            result.vertices.push_back({vertex.x + axis.x * distance,
                                       vertex.y + axis.y * distance,
                                       vertex.z + axis.z * distance});
        }
        for (const auto& triangle : source.triangles) {
            result.triangles.push_back({triangle[0] + vertex_offset,
                                        triangle[1] + vertex_offset,
                                        triangle[2] + vertex_offset});
        }
    }
    result.faceted_result = true;
    result.repairs.push_back("generated " + std::to_string(feature.count) +
                             " deterministic linear-pattern instances from " +
                             std::to_string(source_vertex_count) + " source vertices");
    return result;
}

[[nodiscard]] auto mirrored(const Part& source, const compiler::ir::Project& project,
                            const compiler::ir::Feature& feature) -> Part {
    Part result = source;
    const Point3 origin = spatial_point(project, feature.plane_point);
    const Vector3 normal = direction(project, feature.plane_normal);
    const auto vertex_offset = result.vertices.size();
    for (const auto& vertex : source.vertices) {
        const double projection = (vertex.x - origin.x) * normal.x +
                                  (vertex.y - origin.y) * normal.y +
                                  (vertex.z - origin.z) * normal.z;
        result.vertices.push_back({vertex.x - 2.0 * projection * normal.x,
                                   vertex.y - 2.0 * projection * normal.y,
                                   vertex.z - 2.0 * projection * normal.z});
    }
    for (const auto& triangle : source.triangles) {
        result.triangles.push_back({triangle[0] + vertex_offset,
                                    triangle[2] + vertex_offset,
                                    triangle[1] + vertex_offset});
    }
    result.faceted_result = true;
    result.repairs.push_back("generated mirrored solid across plane " + feature.plane_point +
                             " normal " + feature.plane_normal);
    return result;
}

struct PartBounds {
    Point3 minimum;
    Point3 maximum;
};

[[nodiscard]] auto part_bounds(const Part& part) -> PartBounds {
    PartBounds bounds{{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::max()},
                      {std::numeric_limits<double>::lowest(),
                       std::numeric_limits<double>::lowest(),
                       std::numeric_limits<double>::lowest()}};
    for (const auto& vertex : part.vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.z);
    }
    return bounds;
}

[[nodiscard]] auto axis_aligned_box(const Part& part, const PartBounds& bounds) -> bool {
    constexpr double tolerance = 1e-8;
    if (part.vertices.size() != 8 || part.triangles.size() != 12)
        return false;
    return std::ranges::all_of(part.vertices, [&](const auto& vertex) {
        return (std::abs(vertex.x - bounds.minimum.x) <= tolerance ||
                std::abs(vertex.x - bounds.maximum.x) <= tolerance) &&
               (std::abs(vertex.y - bounds.minimum.y) <= tolerance ||
                std::abs(vertex.y - bounds.maximum.y) <= tolerance) &&
               (std::abs(vertex.z - bounds.minimum.z) <= tolerance ||
                std::abs(vertex.z - bounds.maximum.z) <= tolerance);
    });
}

enum class PrincipalAxis { x, y, z };

[[nodiscard]] auto from_local(const compiler::ir::Point2& point, double along,
                              PrincipalAxis axis) -> Point3 {
    if (axis == PrincipalAxis::x)
        return {along, point.x_mm, point.y_mm};
    if (axis == PrincipalAxis::y)
        return {point.y_mm, along, point.x_mm};
    return {point.x_mm, point.y_mm, along};
}

[[nodiscard]] auto to_local(const Point3& point, PrincipalAxis axis) -> Point3 {
    if (axis == PrincipalAxis::x)
        return {point.y, point.z, point.x};
    if (axis == PrincipalAxis::y)
        return {point.z, point.x, point.y};
    return point;
}

[[nodiscard]] auto extruded_polygon(const std::vector<compiler::ir::Point2>& points,
                                    double minimum_along, double maximum_along,
                                    PrincipalAxis axis) -> Part {
    compiler::ir::Profile profile;
    profile.points = points;
    const std::size_t count = points.size();
    Part result;
    for (const auto& point : points)
        result.vertices.push_back(from_local(point, minimum_along, axis));
    for (const auto& point : points)
        result.vertices.push_back(from_local(point, maximum_along, axis));
    for (const auto& triangle : triangulate_profile(profile)) {
        add_triangle(result, triangle[0], triangle[2], triangle[1]);
        add_triangle(result, count + triangle[0], count + triangle[1], count + triangle[2]);
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto next = (index + 1) % count;
        add_triangle(result, index, next, count + next);
        add_triangle(result, index, count + next, count + index);
    }
    return result;
}

[[nodiscard]] auto semantic_loop_modified(const Part& source,
                                          const compiler::ir::Feature& feature) -> Part {
    const auto bounds = part_bounds(source);
    const double center_x = (bounds.minimum.x + bounds.maximum.x) * 0.5;
    const double center_y = (bounds.minimum.y + bounds.maximum.y) * 0.5;
    double inner_radius = std::numeric_limits<double>::infinity();
    double outer_radius = 0.0;
    for (const auto& vertex : source.vertices) {
        const double radius = std::hypot(vertex.x - center_x, vertex.y - center_y);
        if (radius > 1e-6)
            inner_radius = std::min(inner_radius, radius);
        outer_radius = std::max(outer_radius, radius);
    }
    const double amount = property(feature, feature.type == "FILLET" ? "RADIUS" : "DISTANCE");
    const double bottom = bounds.minimum.z;
    const double top = bounds.maximum.z;
    if (!std::isfinite(inner_radius) || inner_radius <= outer_radius * 0.1 ||
        outer_radius - inner_radius <= amount * 2.0 || top - bottom <= amount * 2.0 ||
        amount <= 0.0) {
        return {};
    }

    using compiler::ir::Point2;
    std::vector<Point2> section;
    const bool top_edge = feature.selected_edge_location == "TOP";
    const bool inner_edge = feature.selected_edge_classification == "INNER";
    const auto append_arc = [&](Point2 center, double start, double end) {
        constexpr std::size_t segments = 8;
        for (std::size_t index = 1; index < segments; ++index) {
            const double angle = start + (end - start) * static_cast<double>(index) /
                                             static_cast<double>(segments);
            section.push_back({center.x_mm + amount * std::cos(angle),
                               center.y_mm + amount * std::sin(angle)});
        }
    };
    if (top_edge && !inner_edge) {
        section = {{inner_radius, bottom}, {outer_radius, bottom},
                   {outer_radius, top - amount}};
        if (feature.type == "FILLET")
            append_arc({outer_radius - amount, top - amount}, 0.0,
                       std::numbers::pi * 0.5);
        section.push_back({outer_radius - amount, top});
        section.push_back({inner_radius, top});
    } else if (top_edge && inner_edge) {
        section = {{inner_radius, bottom}, {outer_radius, bottom}, {outer_radius, top},
                   {inner_radius + amount, top}};
        if (feature.type == "FILLET")
            append_arc({inner_radius + amount, top - amount}, std::numbers::pi * 0.5,
                       std::numbers::pi);
        section.push_back({inner_radius, top - amount});
    } else if (!top_edge && !inner_edge) {
        section = {{inner_radius, bottom}, {outer_radius - amount, bottom}};
        if (feature.type == "FILLET")
            append_arc({outer_radius - amount, bottom + amount}, -std::numbers::pi * 0.5,
                       0.0);
        section.push_back({outer_radius, bottom + amount});
        section.push_back({outer_radius, top});
        section.push_back({inner_radius, top});
    } else {
        section = {{inner_radius, bottom + amount}};
        if (feature.type == "FILLET")
            append_arc({inner_radius + amount, bottom + amount}, std::numbers::pi,
                       std::numbers::pi * 1.5);
        section.push_back({inner_radius + amount, bottom});
        section.push_back({outer_radius, bottom});
        section.push_back({outer_radius, top});
        section.push_back({inner_radius, top});
    }

    compiler::ir::Profile profile;
    profile.points = std::move(section);
    // Edge finishing is part of the delivered surface, not a coarse preview.
    // Use a denser angular tessellation than the legacy generic revolution so
    // the selected circular loop stays visually and dimensionally smooth.
    constexpr std::size_t finish_segments = 96;
    auto result = make_revolve(profile, finish_segments);
    for (auto& vertex : result.vertices) {
        vertex.x += center_x;
        vertex.y += center_y;
    }
    result.body = source.body;
    result.material = source.material;
    result.boolean_result = source.boolean_result;
    result.faceted_result = true;
    result.repairs = source.repairs;
    result.repairs.push_back("semantic " + feature.selected_edge_location + " " +
                             feature.selected_edge_classification +
                             " circular edge loop for native " + feature.type);
    return result;
}

[[nodiscard]] auto edge_modified(const Part& source, const compiler::ir::Project& project,
                                 const compiler::ir::Feature& feature) -> Part {
    if (!feature.selected_edge_location.empty())
        return semantic_loop_modified(source, feature);
    const auto bounds = part_bounds(source);
    if (!axis_aligned_box(source, bounds))
        return {};
    const Point3 selector = spatial_point(project, feature.selected_edge_point);
    struct Candidate {
        PrincipalAxis axis{PrincipalAxis::z};
        compiler::ir::Point2 corner;
        double minimum_along{};
        double maximum_along{};
        double distance{};
    };
    std::vector<Candidate> candidates;
    for (const auto axis : {PrincipalAxis::x, PrincipalAxis::y, PrincipalAxis::z}) {
        const Point3 local_minimum = to_local(bounds.minimum, axis);
        const Point3 local_maximum = to_local(bounds.maximum, axis);
        const Point3 local_selector = to_local(selector, axis);
        const std::array<compiler::ir::Point2, 4> local_corners{
            compiler::ir::Point2{local_minimum.x, local_minimum.y},
            compiler::ir::Point2{local_maximum.x, local_minimum.y},
            compiler::ir::Point2{local_maximum.x, local_maximum.y},
            compiler::ir::Point2{local_minimum.x, local_maximum.y}};
        for (const auto& corner : local_corners) {
            const double along = std::clamp(local_selector.z, local_minimum.z, local_maximum.z);
            const double perpendicular =
                std::hypot(local_selector.x - corner.x_mm, local_selector.y - corner.y_mm);
            candidates.push_back({axis, corner, local_minimum.z, local_maximum.z,
                                  std::hypot(perpendicular, local_selector.z - along)});
        }
    }
    const auto selected_candidate =
        std::ranges::min_element(candidates, {}, &Candidate::distance);
    const Point3 local_minimum = to_local(bounds.minimum, selected_candidate->axis);
    const Point3 local_maximum = to_local(bounds.maximum, selected_candidate->axis);
    const std::array<compiler::ir::Point2, 4> corners{
        compiler::ir::Point2{local_minimum.x, local_minimum.y},
        compiler::ir::Point2{local_maximum.x, local_minimum.y},
        compiler::ir::Point2{local_maximum.x, local_maximum.y},
        compiler::ir::Point2{local_minimum.x, local_maximum.y}};
    const auto selected = static_cast<std::size_t>(
                              std::distance(candidates.begin(), selected_candidate)) %
                          corners.size();
    const double amount = property(feature, feature.type == "FILLET" ? "RADIUS" : "DISTANCE");
    const double width = local_maximum.x - local_minimum.x;
    const double depth = local_maximum.y - local_minimum.y;
    if (amount <= 0.0 || amount >= std::min(width, depth) * 0.5)
        return {};

    std::vector<compiler::ir::Point2> profile;
    for (std::size_t corner_index = 0; corner_index < corners.size(); ++corner_index) {
        if (corner_index != selected) {
            profile.push_back(corners[corner_index]);
            continue;
        }
        const auto& corner = corners[corner_index];
        const auto& previous = corners[(corner_index + corners.size() - 1) % corners.size()];
        const auto& next = corners[(corner_index + 1) % corners.size()];
        const double previous_length = std::hypot(previous.x_mm - corner.x_mm,
                                                  previous.y_mm - corner.y_mm);
        const double next_length =
            std::hypot(next.x_mm - corner.x_mm, next.y_mm - corner.y_mm);
        const compiler::ir::Point2 tangent_start{
            corner.x_mm + (previous.x_mm - corner.x_mm) * amount / previous_length,
            corner.y_mm + (previous.y_mm - corner.y_mm) * amount / previous_length};
        const compiler::ir::Point2 tangent_end{
            corner.x_mm + (next.x_mm - corner.x_mm) * amount / next_length,
            corner.y_mm + (next.y_mm - corner.y_mm) * amount / next_length};
        profile.push_back(tangent_start);
        if (feature.type == "FILLET") {
            constexpr std::size_t fillet_segments = 8;
            const compiler::ir::Point2 center{
                tangent_start.x_mm + tangent_end.x_mm - corner.x_mm,
                tangent_start.y_mm + tangent_end.y_mm - corner.y_mm};
            double start_angle =
                std::atan2(tangent_start.y_mm - center.y_mm,
                           tangent_start.x_mm - center.x_mm);
            double end_angle =
                std::atan2(tangent_end.y_mm - center.y_mm, tangent_end.x_mm - center.x_mm);
            while (end_angle <= start_angle)
                end_angle += 2.0 * std::numbers::pi;
            for (std::size_t segment = 1; segment < fillet_segments; ++segment) {
                const double angle = start_angle +
                                     (end_angle - start_angle) * static_cast<double>(segment) /
                                         static_cast<double>(fillet_segments);
                profile.push_back(
                    {center.x_mm + amount * std::cos(angle),
                     center.y_mm + amount * std::sin(angle)});
            }
        }
        profile.push_back(tangent_end);
    }
    auto result = extruded_polygon(profile, selected_candidate->minimum_along,
                                   selected_candidate->maximum_along,
                                   selected_candidate->axis);
    result.body = source.body;
    result.material = source.material;
    result.boolean_result = source.boolean_result;
    result.faceted_result = true;
    result.repairs = source.repairs;
    result.repairs.push_back("selected sharp edge nearest POINT3 " +
                             feature.selected_edge_point + " for native " + feature.type);
    return result;
}

[[nodiscard]] auto connected_components(const Part& part) -> std::vector<Part> {
    if ((!part.boolean_result && !part.faceted_result) || part.triangles.empty())
        return {part};

    std::vector<std::vector<std::size_t>> incident(part.vertices.size());
    for (std::size_t triangle = 0; triangle < part.triangles.size(); ++triangle) {
        for (const auto vertex : part.triangles[triangle])
            incident[vertex].push_back(triangle);
    }
    std::vector<bool> visited(part.triangles.size());
    std::vector<std::vector<std::size_t>> component_triangles;
    for (std::size_t seed = 0; seed < part.triangles.size(); ++seed) {
        if (visited[seed])
            continue;
        std::vector<std::size_t> pending{seed};
        visited[seed] = true;
        component_triangles.emplace_back();
        auto& component = component_triangles.back();
        while (!pending.empty()) {
            const auto triangle = pending.back();
            pending.pop_back();
            component.push_back(triangle);
            for (const auto vertex : part.triangles[triangle]) {
                for (const auto neighbour : incident[vertex]) {
                    if (!visited[neighbour]) {
                        visited[neighbour] = true;
                        pending.push_back(neighbour);
                    }
                }
            }
        }
        std::ranges::sort(component);
    }
    const auto orient_consistently = [](Part& component) {
        struct EdgeUse {
            std::size_t triangle{};
            bool forward{};
        };
        std::map<std::pair<std::size_t, std::size_t>, std::vector<EdgeUse>> uses;
        for (std::size_t triangle_index = 0; triangle_index < component.triangles.size();
             ++triangle_index) {
            const auto& triangle = component.triangles[triangle_index];
            for (std::size_t edge_index = 0; edge_index < triangle.size(); ++edge_index) {
                const auto first = triangle[edge_index];
                const auto second = triangle[(edge_index + 1) % triangle.size()];
                uses[{std::min(first, second), std::max(first, second)}].push_back(
                    {triangle_index, first < second});
            }
        }
        std::vector<int> flipped(component.triangles.size(), -1);
        std::vector<std::size_t> pending;
        for (std::size_t seed = 0; seed < component.triangles.size(); ++seed) {
            if (flipped[seed] >= 0)
                continue;
            flipped[seed] = 0;
            pending.push_back(seed);
            while (!pending.empty()) {
                const auto current = pending.back();
                pending.pop_back();
                const auto& triangle = component.triangles[current];
                for (std::size_t edge_index = 0; edge_index < triangle.size(); ++edge_index) {
                    const auto first = triangle[edge_index];
                    const auto second = triangle[(edge_index + 1) % triangle.size()];
                    const auto found = uses.find(
                        {std::min(first, second), std::max(first, second)});
                    if (found == uses.end() || found->second.size() != 2)
                        continue;
                    const auto current_use = std::ranges::find(
                        found->second, current, &EdgeUse::triangle);
                    const auto neighbour_use =
                        current_use == found->second.begin() ? std::next(current_use)
                                                             : found->second.begin();
                    const int required =
                        flipped[current] ^ (current_use->forward == neighbour_use->forward ? 1 : 0);
                    if (flipped[neighbour_use->triangle] < 0) {
                        flipped[neighbour_use->triangle] = required;
                        pending.push_back(neighbour_use->triangle);
                    }
                }
            }
        }
        for (std::size_t index = 0; index < component.triangles.size(); ++index) {
            if (flipped[index] == 1)
                std::swap(component.triangles[index][1], component.triangles[index][2]);
        }
    };
    if (component_triangles.size() == 1) {
        Part result = part;
        orient_consistently(result);
        return {std::move(result)};
    }

    std::vector<Part> result;
    result.reserve(component_triangles.size());
    for (std::size_t component_index = 0; component_index < component_triangles.size();
         ++component_index) {
        Part component;
        component.name = part.name + ".component." + std::to_string(component_index + 1);
        component.body = part.body;
        component.material = part.material;
        component.feature_type = part.feature_type;
        component.boolean_result = part.boolean_result;
        component.faceted_result = part.faceted_result;
        if (component_index == 0) {
            component.repairs = part.repairs;
            component.repairs.push_back("separated " +
                                        std::to_string(component_triangles.size()) +
                                        " disconnected result solids");
        }
        std::vector<std::size_t> remap(part.vertices.size(),
                                       std::numeric_limits<std::size_t>::max());
        for (const auto triangle_index : component_triangles[component_index]) {
            Triangle triangle{};
            for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                const auto source_vertex = part.triangles[triangle_index][corner];
                if (remap[source_vertex] == std::numeric_limits<std::size_t>::max()) {
                    remap[source_vertex] = component.vertices.size();
                    component.vertices.push_back(part.vertices[source_vertex]);
                }
                triangle[corner] = remap[source_vertex];
            }
            component.triangles.push_back(triangle);
        }
        orient_consistently(component);
        result.push_back(std::move(component));
    }
    return result;
}

[[nodiscard]] auto build_body_parts(const compiler::ir::Project& project,
                                    const compiler::ir::Body& body) -> std::vector<Part> {
    std::vector<Part> body_parts;
    for (std::size_t feature_index = 0; feature_index < body.features.size(); ++feature_index) {
        const auto& feature = body.features[feature_index];
        const bool modifier = feature.type == "CHAMFER" || feature.type == "FILLET" ||
                              feature.type == "LINEAR_PATTERN" || feature.type == "MIRROR";
        if (modifier) {
            if (body_parts.empty()) {
                Part invalid;
                invalid.name = body.name + "_" + feature.name;
                invalid.body = body.name;
                body_parts.push_back(std::move(invalid));
                continue;
            }
            Part modified = feature.type == "LINEAR_PATTERN"
                                ? linear_pattern(body_parts.back(), project, feature)
                                : feature.type == "MIRROR"
                                      ? mirrored(body_parts.back(), project, feature)
                                      : edge_modified(body_parts.back(), project, feature);
            modified.name = body.name + "_" + feature.name;
            modified.body = body.name;
            modified.material = body.material;
            modified.feature_type = feature.type;
            modified.faceted_result = true;
            body_parts.back() = std::move(modified);
            continue;
        }
        Part part =
            make_region_part(project, feature, body_parts.empty() ? nullptr : &body_parts.back());
        part.name = body.name + "_" + feature.name;
        part.body = body.name;
        part.material = body.material;
        part.feature_type = feature.type;
        if (feature.operation == compiler::ir::FeatureOperation::create) {
            body_parts.push_back(std::move(part));
            continue;
        }
        if (body_parts.empty()) {
            part.vertices.clear();
            part.triangles.clear();
            body_parts.push_back(std::move(part));
            continue;
        }
        std::size_t batch_end = feature_index + 1;
        std::string result_name = body.name + "_" + feature.name;
        if (feature.operation == compiler::ir::FeatureOperation::cut) {
            while (batch_end < body.features.size() &&
                   compatible_cut_batch(feature, body.features[batch_end])) {
                const auto& batched_feature = body.features[batch_end];
                auto cutter = make_region_part(project, batched_feature, &body_parts.back());
                append_geometry(part, cutter);
                result_name = body.name + "_" + batched_feature.name;
                ++batch_end;
            }
        }
        auto boolean =
            apply_boolean(body_parts.back(), part, feature.operation, std::move(result_name));
        boolean.part.boolean_result = true;
        boolean.part.feature_type = "BOOLEAN";
        boolean.part.repairs = std::move(boolean.repairs);
        if (batch_end > feature_index + 1) {
            boolean.part.repairs.push_back(
                "batched " + std::to_string(batch_end - feature_index) +
                " compatible cut features into one boolean transaction");
            feature_index = batch_end - 1;
        }
        body_parts.back() = std::move(boolean.part);
    }
    const auto pose = std::ranges::find(project.poses, body.name, &compiler::ir::BodyPose::body);
    std::vector<Part> result;
    for (auto& part : body_parts) {
        if (pose != project.poses.end())
            apply_transform(part, pose->transform);
        auto components = connected_components(part);
        result.insert(result.end(), std::make_move_iterator(components.begin()),
                      std::make_move_iterator(components.end()));
    }
    return result;
}

} // namespace

auto Model::vertex_count() const -> std::size_t {
    std::size_t count = 0;
    for (const auto& part : parts) {
        count += part.vertices.size();
    }
    return count;
}

auto Model::triangle_count() const -> std::size_t {
    std::size_t count = 0;
    for (const auto& part : parts) {
        count += part.triangles.size();
    }
    return count;
}

auto build_model(const compiler::ir::Project& project) -> Model {
    Model model;
    std::vector<std::vector<Part>> body_results(project.bodies.size());
    if (!project.bodies.empty()) {
        constexpr std::size_t max_workers = 8;
        const auto hardware = std::max(1U, std::thread::hardware_concurrency());
        const auto worker_count =
            std::min({project.bodies.size(), static_cast<std::size_t>(hardware), max_workers});
        std::atomic_size_t next_body{};
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&] {
                while (true) {
                    const auto index = next_body.fetch_add(1, std::memory_order_relaxed);
                    if (index >= project.bodies.size())
                        return;
                    body_results[index] = build_body_parts(project, project.bodies[index]);
                }
            });
        }
        for (auto& worker : workers)
            worker.join();
    }
    for (auto& body_parts : body_results) {
        model.parts.insert(model.parts.end(), std::make_move_iterator(body_parts.begin()),
                           std::make_move_iterator(body_parts.end()));
    }
    const auto definition_parts = model.parts;
    for (const auto& instance : project.instances) {
        for (const auto& definition : definition_parts) {
            if (definition.body != instance.body)
                continue;
            Part occurrence = definition;
            occurrence.body = instance.name;
            occurrence.name = instance.name + definition.name.substr(instance.body.size());
            apply_transform(occurrence, instance.transform);
            apply_joint_chain(occurrence, project, instance.name);
            model.parts.push_back(std::move(occurrence));
        }
    }
    return model;
}

auto is_valid(const Model& model) -> bool {
    return !model.parts.empty() && std::ranges::all_of(model.parts, valid_part);
}

} // namespace icad::cad
