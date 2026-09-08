#include "icad/document/revision.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <string_view>
#include <utility>

namespace icad::document {
namespace {

class Hash {
  public:
    auto add(std::string_view value) -> void {
        for (const char character : value) {
            const auto byte = static_cast<unsigned char>(character);
            value_ ^= byte;
            value_ *= 1099511628211ULL;
        }
        value_ ^= 0xffU;
        value_ *= 1099511628211ULL;
    }

    auto add(double value) -> void {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (std::size_t shift = 0; shift < 64; shift += 8) {
            value_ ^= (bits >> shift) & 0xffU;
            value_ *= 1099511628211ULL;
        }
    }

    [[nodiscard]] auto value() const -> std::uint64_t { return value_; }

  private:
    std::uint64_t value_{14695981039346656037ULL};
};

[[nodiscard]] auto feature_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += body.features.size();
    }
    return count;
}

} // namespace

auto fingerprint(const compiler::ir::Project& project) -> std::uint64_t {
    Hash hash;
    hash.add(project.name);
    hash.add(project.canonical_length_unit);
    hash.add(project.tolerance.linear_mm);
    hash.add(project.tolerance.angular_degrees);
    for (const auto& parameter : project.parameters) {
        hash.add(parameter.name);
        hash.add(parameter.value.value);
        hash.add(parameter.value.unit);
        hash.add(parameter.expression);
        for (const auto& dependency : parameter.dependencies)
            hash.add(dependency);
    }
    for (const auto& angle : project.angles) {
        hash.add(angle.name);
        hash.add(angle.degrees);
    }
    for (const auto& point : project.points) {
        hash.add(point.name);
        for (const auto coordinate : point.position_mm)
            hash.add(coordinate);
        hash.add(static_cast<double>(point.kind == compiler::ir::SpatialPointKind::offset));
        hash.add(point.base_point);
        hash.add(point.direction);
        hash.add(point.distance_mm);
        hash.add(point.distance_reference);
    }
    for (const auto& vector : project.vectors) {
        hash.add(vector.name);
        for (const auto component : vector.unit)
            hash.add(component);
        hash.add(static_cast<double>(vector.kind == compiler::ir::DirectionKind::between_points
                                         ? 1
                                     : vector.kind == compiler::ir::DirectionKind::rotated ? 2
                                                                                            : 0));
        hash.add(vector.from_point);
        hash.add(vector.to_point);
        hash.add(vector.source_direction);
        hash.add(vector.around_axis);
        hash.add(vector.angle_degrees);
        hash.add(vector.angle_reference);
    }
    for (const auto& pose : project.poses) {
        hash.add(pose.body);
        hash.add(pose.point);
        for (const auto coordinate : pose.transform.position_mm)
            hash.add(coordinate);
        for (const auto angle : pose.transform.rotation_deg)
            hash.add(angle);
    }
    for (const auto& instance : project.instances) {
        hash.add(instance.name);
        hash.add(instance.body);
        hash.add(instance.point);
        for (const auto coordinate : instance.transform.position_mm)
            hash.add(coordinate);
        for (const auto angle : instance.transform.rotation_deg)
            hash.add(angle);
    }
    for (const auto& joint : project.joints) {
        hash.add(joint.name);
        hash.add(static_cast<double>(joint.kind == compiler::ir::JointKind::revolute    ? 1
                                     : joint.kind == compiler::ir::JointKind::prismatic ? 2
                                                                                        : 0));
        hash.add(joint.parent_body);
        hash.add(joint.child_body);
        hash.add(joint.point);
        hash.add(joint.axis);
        hash.add(joint.value);
        hash.add(joint.lower_limit);
        hash.add(joint.upper_limit);
        hash.add(joint.unit);
        hash.add(joint.value_reference);
        hash.add(joint.lower_limit_reference);
        hash.add(joint.upper_limit_reference);
    }
    for (const auto& interface : project.interfaces) {
        hash.add(interface.name);
        hash.add(interface.occurrence);
        hash.add(interface.point);
        hash.add(interface.axis);
        hash.add(interface.kind);
        hash.add(interface.size_mm);
        hash.add(static_cast<double>(interface.has_size));
    }
    for (const auto& connection : project.connections) {
        hash.add(connection.name);
        hash.add(connection.first_interface);
        hash.add(connection.second_interface);
        hash.add(connection.method);
        hash.add(connection.standard);
        hash.add(connection.fastener);
        hash.add(connection.fit);
        hash.add(connection.filler);
        hash.add(connection.weld_process);
        hash.add(connection.clearance_mm);
        hash.add(connection.weld_size_mm);
        hash.add(connection.weld_length_mm);
        hash.add(connection.filler_diameter_mm);
        hash.add(connection.stock_length_mm);
        hash.add(connection.deposition_efficiency);
        hash.add(connection.interface_gap_mm);
        hash.add(connection.axis_alignment);
        hash.add(static_cast<double>(connection.quantity));
        hash.add(static_cast<double>(connection.quantity_explicit));
        hash.add(static_cast<double>(connection.automatic));
        hash.add(static_cast<double>(connection.aligned));
    }
    for (const auto& material : project.materials) {
        hash.add(material.name);
        hash.add(material.profile);
        hash.add(material.preset);
        for (const auto channel : material.base_color)
            hash.add(channel);
        hash.add(material.metallic);
        hash.add(material.roughness);
        hash.add(material.texture);
        hash.add(static_cast<double>(material.texture_seed));
        hash.add(material.texture_scale_mm);
        hash.add(material.uv_mode);
    }
    for (const auto& profile : project.profiles) {
        hash.add(profile.name);
        for (const auto& segment : profile.segments) {
            hash.add(static_cast<double>(segment.kind ==
                                         compiler::ir::ProfileSegmentKind::circular_arc));
            hash.add(segment.start.x_mm);
            hash.add(segment.start.y_mm);
            hash.add(segment.end.x_mm);
            hash.add(segment.end.y_mm);
            hash.add(segment.center.x_mm);
            hash.add(segment.center.y_mm);
            hash.add(segment.radius_mm);
            hash.add(segment.sweep_radians);
        }
        for (const auto& point : profile.points) {
            hash.add(point.x_mm);
            hash.add(point.y_mm);
        }
    }
    for (const auto& sketch : project.sketches) {
        hash.add(sketch.name);
        hash.add(sketch.body);
        hash.add(sketch.plane);
        hash.add(sketch.support_feature);
        hash.add(sketch.support_face);
        hash.add(sketch.solve_requirement);
        for (const auto& shape : sketch.shapes) {
            hash.add(shape.name);
            hash.add(shape.role);
            hash.add(static_cast<double>(shape.closed));
            for (const auto& point : shape.points)
                hash.add(point);
            for (const auto& entity : shape.entities)
                hash.add(entity);
            hash.add(shape.profile);
            hash.add(shape.area_mm2);
            hash.add(shape.containing_shape);
        }
        for (const auto& region : sketch.regions) {
            hash.add(region.name);
            hash.add(region.outer_shape);
            for (const auto& hole : region.hole_shapes)
                hash.add(hole);
            hash.add(region.outer_profile);
            for (const auto& hole_profile : region.hole_profiles)
                hash.add(hole_profile);
            hash.add(region.area_mm2);
        }
        for (const auto& point : sketch.points) {
            hash.add(point.name);
            hash.add(point.initial.x_mm);
            hash.add(point.initial.y_mm);
            hash.add(point.solved.x_mm);
            hash.add(point.solved.y_mm);
            hash.add(static_cast<double>(point.fixed));
        }
        for (const auto& entity : sketch.entities) {
            hash.add(entity.name);
            hash.add(static_cast<double>(entity.kind ==
                                         compiler::ir::ProfileSegmentKind::circular_arc));
            hash.add(entity.start);
            hash.add(entity.end);
            hash.add(entity.center);
            hash.add(static_cast<double>(entity.counterclockwise));
            hash.add(static_cast<double>(entity.full_circle));
            hash.add(entity.radius_mm);
            hash.add(entity.radius_reference);
        }
        for (const auto& constraint : sketch.constraints) {
            hash.add(constraint.name);
            hash.add(constraint.kind);
            for (const auto& reference : constraint.references)
                hash.add(reference);
            hash.add(constraint.target_value);
            hash.add(constraint.target_unit);
            hash.add(constraint.target_reference);
        }
        hash.add(static_cast<double>(sketch.status ==
                                     compiler::ir::SketchSolveStatus::fully_constrained
                                         ? 0
                                     : sketch.status ==
                                               compiler::ir::SketchSolveStatus::under_constrained
                                         ? 1
                                         : 2));
        hash.add(static_cast<double>(sketch.degrees_of_freedom));
    }
    for (const auto& body : project.bodies) {
        hash.add(body.name);
        hash.add(body.material);
        for (const auto& selection : body.topology_selections) {
            hash.add(selection.name);
            hash.add(selection.source_feature);
            hash.add(selection.entity_kind);
            hash.add(selection.geometry);
            hash.add(selection.convexity);
            hash.add(selection.adjacent_face);
            hash.add(selection.topology_id);
        }
        for (const auto& feature : body.features) {
            hash.add(feature.name);
            hash.add(feature.source_keyword);
            hash.add(feature.type);
            hash.add(feature.profile);
            hash.add(feature.region);
            for (const auto& hole_profile : feature.region_hole_profiles)
                hash.add(hole_profile);
            hash.add(feature.target_profile);
            hash.add(static_cast<double>(feature.operation == compiler::ir::FeatureOperation::unite
                                             ? 1
                                         : feature.operation == compiler::ir::FeatureOperation::cut
                                             ? 2
                                         : feature.operation ==
                                                   compiler::ir::FeatureOperation::intersect
                                             ? 3
                                             : 0));
            hash.add(feature.selected_edge_point);
            hash.add(feature.selected_edge_location);
            hash.add(feature.selected_edge_classification);
            hash.add(feature.selected_edge_set);
            hash.add(feature.selected_topology_id);
            hash.add(feature.direction);
            hash.add(feature.plane_point);
            hash.add(feature.plane_normal);
            hash.add(feature.sketch_plane);
            hash.add(feature.support_feature);
            hash.add(feature.support_face);
            for (const auto& point : feature.path_points)
                hash.add(point);
            hash.add(static_cast<double>(feature.count));
            for (const auto& property : feature.properties) {
                hash.add(property.name);
                hash.add(property.value.value);
                hash.add(property.value.unit);
                hash.add(property.expression);
            }
        }
    }
    for (const auto& constraint : project.constraints) {
        hash.add(constraint.name);
        hash.add(constraint.kind);
        hash.add(constraint.first_body);
        hash.add(constraint.second_body);
        hash.add(constraint.target_value);
        hash.add(constraint.target_unit);
        hash.add(constraint.target_reference);
    }
    for (const auto& mate : project.mates) {
        hash.add(mate.name);
        hash.add(static_cast<double>(mate.kind == compiler::ir::MateKind::edge));
        hash.add(mate.first_occurrence);
        hash.add(mate.first_selector);
        hash.add(mate.second_occurrence);
        hash.add(mate.second_selector);
        hash.add(mate.target_mm);
        hash.add(mate.target_reference);
    }
    for (const auto& scene : project.scenes) {
        hash.add(scene.name);
        hash.add(scene.duration_seconds);
        hash.add(scene.frames_per_second);
        hash.add(scene.background);
        hash.add(static_cast<double>(scene.loop_count));
        for (const auto& light : scene.lights) {
            hash.add(light.name);
            hash.add(light.kind);
            for (const auto channel : light.color)
                hash.add(channel);
            hash.add(light.intensity);
            for (const auto coordinate : light.position_mm)
                hash.add(coordinate);
        }
        for (const auto& event : scene.events) {
            hash.add(event.time_seconds);
            hash.add(event.name);
        }
        for (const auto& track : scene.tracks) {
            hash.add(track.name);
            hash.add(track.target_kind);
            hash.add(track.target);
            hash.add(track.easing);
            for (const auto& keyframe : track.keyframes) {
                hash.add(keyframe.time_seconds);
                for (const auto coordinate : keyframe.transform.position_mm)
                    hash.add(coordinate);
                for (const auto angle : keyframe.transform.rotation_deg)
                    hash.add(angle);
                hash.add(keyframe.joint_value);
                hash.add(keyframe.joint_unit);
                hash.add(static_cast<double>(keyframe.visible));
            }
        }
    }
    return hash.value();
}

auto fingerprint(std::string_view source) -> std::uint64_t {
    Hash hash;
    hash.add(source);
    return hash.value();
}

auto revision_id(std::uint64_t fingerprint_value) -> std::string {
    std::array<char, 16> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), fingerprint_value, 16);
    const auto digits = static_cast<std::size_t>(converted.ptr - buffer.data());
    return std::string(16 - digits, '0') + std::string(buffer.data(), converted.ptr);
}

auto revision_id(std::string_view source) -> std::string {
    return revision_id(fingerprint(source));
}

auto diff(const compiler::ir::Project& before, const compiler::ir::Project& after) -> Diff {
    const auto delta = [](std::size_t old_value, std::size_t new_value) {
        return static_cast<std::ptrdiff_t>(new_value) - static_cast<std::ptrdiff_t>(old_value);
    };
    return {delta(before.parameters.size(), after.parameters.size()),
            delta(before.profiles.size(), after.profiles.size()),
            delta(before.bodies.size(), after.bodies.size()),
            delta(feature_count(before), feature_count(after)),
            delta(before.scenes.size(), after.scenes.size())};
}

RevisionStore::RevisionStore(compiler::ir::Project initial)
    : history_{std::move(initial)}, cursor_{0} {}

auto RevisionStore::revision() const -> std::uint64_t { return fingerprint(current()); }

auto RevisionStore::current() const -> const compiler::ir::Project& { return history_[cursor_]; }

auto RevisionStore::commit(compiler::ir::Project next, std::uint64_t expected_revision) -> bool {
    if (revision() != expected_revision) {
        return false;
    }
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1), history_.end());
    history_.push_back(std::move(next));
    cursor_ = history_.size() - 1;
    return true;
}

auto RevisionStore::undo() -> bool {
    if (cursor_ == 0) {
        return false;
    }
    --cursor_;
    return true;
}

auto RevisionStore::redo() -> bool {
    if (cursor_ + 1 >= history_.size()) {
        return false;
    }
    ++cursor_;
    return true;
}

} // namespace icad::document
