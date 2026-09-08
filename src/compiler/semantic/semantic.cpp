#include "icad/compiler/semantic/semantic.hpp"

#include "icad/compiler/expression.hpp"
#include "icad/compiler/types/types.hpp"
#include "icad/compiler/units/units.hpp"
#include "icad/constraints/sketch_solver.hpp"
#include "icad/materials/library.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace icad::compiler {
namespace {

auto add_error(SemanticResult& result, std::string code, std::string message,
               SourceLocation location) -> void {
    result.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::error,
        std::move(code),
        std::move(message),
        location,
    });
}

[[nodiscard]] auto lower_quantity(const ast::QuantityLiteral& literal, units::Dimension expected,
                                  bool positive, SemanticResult& result) -> ir::Quantity {
    const auto source_unit = units::find(literal.unit);
    if (!source_unit) {
        add_error(result, "ICAD-S0001", "unknown unit '" + literal.unit + "'", literal.location);
        return {};
    }
    if (expected != units::Dimension::unknown && source_unit->dimension != expected) {
        add_error(result, "ICAD-S0002",
                  "unit '" + literal.unit + "' has the wrong physical dimension", literal.location);
        return {};
    }
    if (positive && literal.value <= 0.0) {
        add_error(result, "ICAD-S0003", "quantity must be greater than zero", literal.location);
    }
    const std::string_view canonical = units::canonical_symbol(source_unit->dimension);
    const auto target_unit = units::find(canonical);
    const auto converted = target_unit ? units::convert(literal.value, *source_unit, *target_unit)
                                       : std::optional<double>{};
    if (!converted) {
        add_error(result, "ICAD-S0004", "quantity cannot be converted to canonical units",
                  literal.location);
        return {};
    }
    return ir::Quantity{*converted, std::string{canonical}, source_unit->dimension};
}

auto append_diagnostics(SemanticResult& result, std::vector<Diagnostic> diagnostics) -> void {
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(diagnostics.begin()),
                              std::make_move_iterator(diagnostics.end()));
}

[[nodiscard]] auto quantity_from_evaluated(EvaluatedScalar value) -> ir::Quantity {
    return ir::Quantity{value.value, std::string{units::canonical_symbol(value.dimension)},
                        value.dimension};
}

[[nodiscard]] auto lower_expression(
    const ast::ScalarExpression& expression, units::Dimension expected, bool positive,
    const std::unordered_map<std::string, ir::Quantity>& named_values, SemanticResult& result)
    -> ir::Quantity {
    auto evaluated = evaluate_scalar_expression(
        expression, [&named_values](std::string_view reference) -> std::optional<EvaluatedScalar> {
            const auto found = named_values.find(std::string{reference});
            if (found == named_values.end()) {
                return std::nullopt;
            }
            return EvaluatedScalar{found->second.value, found->second.dimension};
        });
    append_diagnostics(result, std::move(evaluated.diagnostics));
    if (!evaluated.value) {
        return {};
    }
    if (expected != units::Dimension::unknown && evaluated.value->dimension != expected) {
        add_error(result, "ICAD-S0002", "scalar expression has the wrong physical dimension",
                  expression.location);
    }
    if (positive && evaluated.value->value <= 0.0) {
        add_error(result, "ICAD-S0003", "quantity must be greater than zero",
                  expression.location);
    }
    return quantity_from_evaluated(*evaluated.value);
}

[[nodiscard]] auto lower_value(const ast::ValueDecl& value, units::Dimension expected,
                               const std::unordered_map<std::string, ir::Quantity>& named_values,
                               SemanticResult& result) -> ir::Quantity {
    if (!value.expression.empty()) {
        return lower_expression(value.expression, expected, false, named_values, result);
    }
    if (value.parameter_reference.empty()) {
        return lower_quantity(value.literal, expected, false, result);
    }
    const auto found = named_values.find(value.parameter_reference);
    if (found == named_values.end()) {
        add_error(result, "ICAD-S0031",
                  "unknown scalar or angle '" + value.parameter_reference + "'", value.location);
        return {};
    }
    if (found->second.dimension != expected) {
        add_error(result, "ICAD-S0031",
                  "'" + value.parameter_reference + "' has the wrong physical dimension",
                  value.location);
    }
    return found->second;
}

[[nodiscard]] auto signed_area(const std::vector<ir::Point2>& points) -> double {
    double area = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& first = points[index];
        const auto& second = points[(index + 1) % points.size()];
        area += first.x_mm * second.y_mm - second.x_mm * first.y_mm;
    }
    return area * 0.5;
}

[[nodiscard]] auto orientation(const ir::Point2& a, const ir::Point2& b, const ir::Point2& c)
    -> double {
    return (b.x_mm - a.x_mm) * (c.y_mm - a.y_mm) - (b.y_mm - a.y_mm) * (c.x_mm - a.x_mm);
}

[[nodiscard]] auto segments_cross(const ir::Point2& a, const ir::Point2& b, const ir::Point2& c,
                                  const ir::Point2& d) -> bool {
    constexpr double tolerance = 1e-9;
    const double first = orientation(a, b, c);
    const double second = orientation(a, b, d);
    const double third = orientation(c, d, a);
    const double fourth = orientation(c, d, b);
    const bool proper =
        ((first > tolerance && second < -tolerance) ||
         (first < -tolerance && second > tolerance)) &&
        ((third > tolerance && fourth < -tolerance) || (third < -tolerance && fourth > tolerance));
    if (proper)
        return true;
    const auto on_segment = [](const ir::Point2& first_point, const ir::Point2& second_point,
                               const ir::Point2& candidate) {
        constexpr double bounds_tolerance = 1e-9;
        return candidate.x_mm >= std::min(first_point.x_mm, second_point.x_mm) - bounds_tolerance &&
               candidate.x_mm <= std::max(first_point.x_mm, second_point.x_mm) + bounds_tolerance &&
               candidate.y_mm >= std::min(first_point.y_mm, second_point.y_mm) - bounds_tolerance &&
               candidate.y_mm <= std::max(first_point.y_mm, second_point.y_mm) + bounds_tolerance;
    };
    return (std::abs(first) <= tolerance && on_segment(a, b, c)) ||
           (std::abs(second) <= tolerance && on_segment(a, b, d)) ||
           (std::abs(third) <= tolerance && on_segment(c, d, a)) ||
           (std::abs(fourth) <= tolerance && on_segment(c, d, b));
}

[[nodiscard]] auto self_intersects(const std::vector<ir::Point2>& points) -> bool {
    for (std::size_t first = 0; first < points.size(); ++first) {
        const std::size_t first_next = (first + 1) % points.size();
        for (std::size_t second = first + 1; second < points.size(); ++second) {
            const std::size_t second_next = (second + 1) % points.size();
            if (first == second || first_next == second || second_next == first) {
                continue;
            }
            if (segments_cross(points[first], points[first_next], points[second],
                               points[second_next])) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] auto boundaries_intersect(const std::vector<ir::Point2>& first,
                                         const std::vector<ir::Point2>& second) -> bool {
    for (std::size_t first_edge = 0; first_edge < first.size(); ++first_edge) {
        for (std::size_t second_edge = 0; second_edge < second.size(); ++second_edge) {
            if (segments_cross(first[first_edge], first[(first_edge + 1) % first.size()],
                               second[second_edge],
                               second[(second_edge + 1) % second.size()])) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] auto point_inside(const ir::Point2& point,
                                const std::vector<ir::Point2>& polygon) -> bool {
    bool inside = false;
    for (std::size_t index = 0, previous = polygon.size() - 1; index < polygon.size();
         previous = index++) {
        const auto& first = polygon[index];
        const auto& second = polygon[previous];
        const bool crosses = (first.y_mm > point.y_mm) != (second.y_mm > point.y_mm);
        if (!crosses)
            continue;
        const double x = (second.x_mm - first.x_mm) * (point.y_mm - first.y_mm) /
                             (second.y_mm - first.y_mm) +
                         first.x_mm;
        if (point.x_mm < x)
            inside = !inside;
    }
    return inside;
}

[[nodiscard]] auto same_point(const ir::Point2& first, const ir::Point2& second) -> bool {
    return std::hypot(first.x_mm - second.x_mm, first.y_mm - second.y_mm) <= 1e-9;
}

[[nodiscard]] auto has_duplicate_consecutive_point(const std::vector<ir::Point2>& points) -> bool {
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (same_point(points[index], points[(index + 1) % points.size()]))
            return true;
    }
    return false;
}

[[nodiscard]] auto has_adjacent_overlap(const std::vector<ir::Point2>& points) -> bool {
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& previous = points[(index + points.size() - 1) % points.size()];
        const auto& current = points[index];
        const auto& next = points[(index + 1) % points.size()];
        if (std::abs(orientation(previous, current, next)) > 1e-9)
            continue;
        const double first_x = previous.x_mm - current.x_mm;
        const double first_y = previous.y_mm - current.y_mm;
        const double second_x = next.x_mm - current.x_mm;
        const double second_y = next.y_mm - current.y_mm;
        if (first_x * second_x + first_y * second_y > 1e-9)
            return true;
    }
    return false;
}

[[nodiscard]] auto lower_point(const ast::Point2Decl& point, SemanticResult& result) -> ir::Point2 {
    const auto x = lower_quantity(point.x, units::Dimension::length, false, result);
    const auto y = lower_quantity(point.y, units::Dimension::length, false, result);
    return {x.value, y.value};
}

[[nodiscard]] auto line_segment(const ir::Point2& start, const ir::Point2& end)
    -> ir::ProfileSegment {
    return {ir::ProfileSegmentKind::line, start, end, {}, 0.0, 0.0};
}

[[nodiscard]] auto tessellate(const std::vector<ir::ProfileSegment>& segments)
    -> std::vector<ir::Point2> {
    constexpr double full_circle = 2.0 * std::numbers::pi;
    constexpr double angular_step = full_circle / 32.0;
    std::vector<ir::Point2> points;
    for (const auto& segment : segments) {
        if (points.empty()) {
            points.push_back(segment.start);
        }
        if (segment.kind == ir::ProfileSegmentKind::line) {
            points.push_back(segment.end);
            continue;
        }
        const double start_angle = std::atan2(segment.start.y_mm - segment.center.y_mm,
                                              segment.start.x_mm - segment.center.x_mm);
        const auto steps = static_cast<std::size_t>(
            std::max(1.0, std::ceil(std::abs(segment.sweep_radians) / angular_step)));
        for (std::size_t step = 1; step <= steps; ++step) {
            const double fraction = static_cast<double>(step) / static_cast<double>(steps);
            const double angle = start_angle + segment.sweep_radians * fraction;
            points.push_back({segment.center.x_mm + segment.radius_mm * std::cos(angle),
                              segment.center.y_mm + segment.radius_mm * std::sin(angle)});
        }
    }
    if (points.size() > 1 && same_point(points.front(), points.back())) {
        points.pop_back();
    }
    return points;
}

auto reverse_segments(std::vector<ir::ProfileSegment>& segments) -> void {
    std::vector<ir::ProfileSegment> reversed;
    reversed.reserve(segments.size());
    for (auto iterator = segments.rbegin(); iterator != segments.rend(); ++iterator) {
        auto segment = *iterator;
        std::swap(segment.start, segment.end);
        segment.sweep_radians = -segment.sweep_radians;
        reversed.push_back(std::move(segment));
    }
    segments = std::move(reversed);
}

auto lower_profile(const ast::ProfileDecl& profile, ir::Profile& lowered, SemanticResult& result)
    -> void {
    constexpr double full_circle = 2.0 * std::numbers::pi;
    if (profile.mode == ast::ProfileMode::points) {
        for (const auto& point : profile.points) {
            lowered.points.push_back(lower_point(point, result));
        }
        if (signed_area(lowered.points) < 0.0) {
            std::ranges::reverse(lowered.points);
        }
        for (std::size_t index = 0; index < lowered.points.size(); ++index) {
            lowered.segments.push_back(line_segment(
                lowered.points[index], lowered.points[(index + 1) % lowered.points.size()]));
        }
        return;
    }
    if (profile.mode == ast::ProfileMode::circle) {
        const auto center = lower_point(profile.circle_center, result);
        const auto radius =
            lower_quantity(profile.circle_radius, units::Dimension::length, true, result);
        const ir::Point2 start{center.x_mm + radius.value, center.y_mm};
        lowered.segments.push_back({ir::ProfileSegmentKind::circular_arc, start, start, center,
                                    radius.value, full_circle});
        lowered.points = tessellate(lowered.segments);
        return;
    }
    if (profile.mode != ast::ProfileMode::path) {
        return;
    }
    const auto start = lower_point(profile.path_start, result);
    auto current = start;
    for (const auto& source_segment : profile.path_segments) {
        const auto end = lower_point(source_segment.end, result);
        if (same_point(current, end)) {
            add_error(result, "ICAD-S0029", "profile segment endpoints must be different",
                      source_segment.location);
            continue;
        }
        if (source_segment.kind == ast::PathSegmentKind::line) {
            lowered.segments.push_back(line_segment(current, end));
            current = end;
            continue;
        }
        const auto center = lower_point(source_segment.center, result);
        const double start_radius =
            std::hypot(current.x_mm - center.x_mm, current.y_mm - center.y_mm);
        const double end_radius = std::hypot(end.x_mm - center.x_mm, end.y_mm - center.y_mm);
        const double scale = std::max({1.0, start_radius, end_radius});
        if (start_radius <= 1e-9 || std::abs(start_radius - end_radius) > 1e-7 * scale) {
            add_error(result, "ICAD-S0029",
                      "ARC start and end must have the same non-zero distance from CENTER",
                      source_segment.location);
        }
        const double start_angle =
            std::atan2(current.y_mm - center.y_mm, current.x_mm - center.x_mm);
        const double end_angle = std::atan2(end.y_mm - center.y_mm, end.x_mm - center.x_mm);
        double sweep = end_angle - start_angle;
        if (source_segment.counterclockwise) {
            while (sweep <= 0.0)
                sweep += full_circle;
        } else {
            while (sweep >= 0.0)
                sweep -= full_circle;
        }
        lowered.segments.push_back(
            {ir::ProfileSegmentKind::circular_arc, current, end, center, start_radius, sweep});
        current = end;
    }
    if (!same_point(current, start)) {
        lowered.segments.push_back(line_segment(current, start));
    }
    lowered.points = tessellate(lowered.segments);
    if (signed_area(lowered.points) < 0.0) {
        reverse_segments(lowered.segments);
        lowered.points = tessellate(lowered.segments);
    }
}

[[nodiscard]] auto lower_sketch_constraint(
    const ast::SketchConstraintDecl& constraint, ir::Sketch& sketch,
    const std::unordered_map<std::string, ir::Quantity>& named_values, SemanticResult& result,
    std::string_view diagnostic_code) -> bool {
    const auto point_exists = [&](const std::string& name) {
        return std::ranges::any_of(sketch.points,
                                   [&](const auto& point) { return point.name == name; });
    };
    const auto entity = [&](const std::string& name) {
        return std::ranges::find(sketch.entities, name, &ir::SketchEntity::name);
    };
    const auto line_entity = [&](const std::string& name) {
        const auto found = entity(name);
        return found != sketch.entities.end() &&
               found->kind == ir::ProfileSegmentKind::line && !found->full_circle;
    };
    const auto circular_entity = [&](const std::string& name) {
        const auto found = entity(name);
        return found != sketch.entities.end() &&
               found->kind == ir::ProfileSegmentKind::circular_arc;
    };
    const auto points_only = [&] {
        return std::ranges::all_of(constraint.references, point_exists);
    };

    const bool point_pair = constraint.kind == "HORIZONTAL" ||
                            constraint.kind == "VERTICAL" ||
                            constraint.kind == "COINCIDENT" ||
                            constraint.kind == "DISTANCE" ||
                            constraint.kind == "H_DISTANCE" ||
                            constraint.kind == "V_DISTANCE";
    const bool line_pair = constraint.kind == "PARALLEL" ||
                           constraint.kind == "PERPENDICULAR" ||
                           constraint.kind == "EQUAL_LENGTH";
    const bool circle_pair = constraint.kind == "CONCENTRIC" ||
                             constraint.kind == "EQUAL_RADIUS";
    const bool angle = constraint.kind == "ANGLE";
    const bool midpoint = constraint.kind == "MIDPOINT";
    const bool symmetric = constraint.kind == "SYMMETRIC";
    const bool tangent = constraint.kind == "TANGENT";
    bool valid = point_pair || line_pair || circle_pair || angle || midpoint || symmetric ||
                 tangent;
    if (!valid) {
        add_error(result, std::string{diagnostic_code},
                  "unsupported sketch constraint kind '" + constraint.kind + "'",
                  constraint.location);
        return false;
    }

    if ((point_pair && (constraint.references.size() != 2 || !points_only())) ||
        (angle && (constraint.references.size() != 3 || !points_only())) ||
        (line_pair &&
         (constraint.references.size() != 2 || !line_entity(constraint.references[0]) ||
          !line_entity(constraint.references[1]))) ||
        (circle_pair &&
         (constraint.references.size() != 2 || !circular_entity(constraint.references[0]) ||
          !circular_entity(constraint.references[1]))) ||
        (midpoint &&
         (constraint.references.size() != 2 || !point_exists(constraint.references[0]) ||
          !line_entity(constraint.references[1]))) ||
        (symmetric &&
         (constraint.references.size() != 3 || !point_exists(constraint.references[0]) ||
          !point_exists(constraint.references[1]) || !line_entity(constraint.references[2])))) {
        add_error(result, std::string{diagnostic_code},
                  "sketch constraint references do not match the required point/entity types",
                  constraint.location);
        return false;
    }

    if (tangent) {
        const bool first_line = constraint.references.size() == 3 &&
                                line_entity(constraint.references[0]);
        const bool second_line = constraint.references.size() == 3 &&
                                 line_entity(constraint.references[1]);
        const bool first_arc = constraint.references.size() == 3 &&
                               circular_entity(constraint.references[0]) &&
                               !entity(constraint.references[0])->full_circle;
        const bool second_arc = constraint.references.size() == 3 &&
                                circular_entity(constraint.references[1]) &&
                                !entity(constraint.references[1])->full_circle;
        const bool typed = constraint.references.size() == 3 &&
                           ((first_line && second_arc) || (first_arc && second_line)) &&
                           point_exists(constraint.references[2]);
        if (!typed) {
            add_error(result, std::string{diagnostic_code},
                      "TANGENT requires one LINE, one ARC, and one shared endpoint",
                      constraint.location);
            return false;
        }
        const auto line = entity(first_line ? constraint.references[0]
                                            : constraint.references[1]);
        const auto arc = entity(first_arc ? constraint.references[0]
                                          : constraint.references[1]);
        const auto& contact = constraint.references[2];
        const bool on_line = contact == line->start || contact == line->end;
        const bool on_arc = contact == arc->start || contact == arc->end;
        if (!on_line || !on_arc) {
            add_error(result, std::string{diagnostic_code},
                      "TANGENT contact point must be an endpoint shared by its LINE and ARC",
                      constraint.location);
            return false;
        }
    }

    const bool duplicate_pair =
        constraint.references.size() >= 2 &&
        constraint.references[0] == constraint.references[1];
    const bool duplicate_angle_leg =
        angle && (constraint.references[0] == constraint.references[1] ||
                  constraint.references[1] == constraint.references[2]);
    if (duplicate_pair || duplicate_angle_leg) {
        add_error(result, std::string{diagnostic_code},
                  "sketch constraint references must identify distinct geometry",
                  constraint.location);
        return false;
    }

    const bool length_target = constraint.kind == "DISTANCE" ||
                               constraint.kind == "H_DISTANCE" ||
                               constraint.kind == "V_DISTANCE";
    ir::Quantity target;
    if (length_target || angle) {
        target = lower_value(constraint.target,
                             angle ? units::Dimension::angle : units::Dimension::length,
                             named_values, result);
        if (target.value < 0.0 || (angle && target.value > 180.0)) {
            add_error(result, std::string{diagnostic_code},
                      "sketch distance or angle target is outside its valid range",
                      constraint.location);
            valid = false;
        }
    }
    sketch.constraints.push_back(ir::SketchConstraint{
        constraint.name, constraint.kind, constraint.references, target.value, target.unit,
        constraint.target.parameter_reference});
    return valid;
}

} // namespace

auto analyze(const ast::Program& program) -> SemanticResult {
    SemanticResult result;
    ir::Project lowered;
    lowered.name = program.project_name;

    const auto default_unit = units::find(program.default_length_unit);
    if (!default_unit || default_unit->dimension != units::Dimension::length) {
        add_error(result, "ICAD-S0005", "UNITS must name a supported length unit",
                  program.location);
    } else {
        lowered.canonical_length_unit =
            std::string{units::canonical_symbol(units::Dimension::length)};
    }

    std::unordered_map<std::string, ir::Quantity> parameter_values;
    std::unordered_map<std::string, const ast::ParameterDecl*> parameter_declarations;
    for (const auto& parameter : program.parameters) {
        parameter_declarations.emplace(parameter.name, &parameter);
    }
    const std::string project_prefix = program.project_name + ".";
    const auto local_parameter_name = [&](std::string_view reference) -> std::string {
        if (reference.starts_with(project_prefix)) {
            return std::string{reference.substr(project_prefix.size())};
        }
        return std::string{reference};
    };
    std::set<std::string> unresolved_parameters;
    for (const auto& parameter : program.parameters) {
        unresolved_parameters.insert(parameter.name);
    }
    bool parameter_progress = true;
    while (parameter_progress && !unresolved_parameters.empty()) {
        parameter_progress = false;
        for (auto iterator = unresolved_parameters.begin();
             iterator != unresolved_parameters.end();) {
            const auto* parameter = parameter_declarations.at(*iterator);
            bool unknown_reference = false;
            bool dependency_pending = false;
            for (const auto& reference : parameter->expression.references) {
                const std::string local = local_parameter_name(reference);
                if (!parameter_declarations.contains(local)) {
                    add_error(result, "ICAD-E0002",
                              "unknown scalar reference '" + reference + "'",
                              parameter->expression.location);
                    unknown_reference = true;
                    break;
                }
                if (!parameter_values.contains(local)) {
                    dependency_pending = true;
                }
            }
            if (unknown_reference) {
                iterator = unresolved_parameters.erase(iterator);
                parameter_progress = true;
                continue;
            }
            if (dependency_pending) {
                ++iterator;
                continue;
            }

            ir::Quantity quantity;
            if (parameter->expression.empty()) {
                const auto unit = units::find(parameter->value.unit);
                const auto dimension = unit ? unit->dimension : units::Dimension::unknown;
                quantity = lower_quantity(parameter->value, dimension, false, result);
            } else {
                auto evaluated = evaluate_scalar_expression(
                    parameter->expression,
                    [&](std::string_view reference) -> std::optional<EvaluatedScalar> {
                        const std::string local = local_parameter_name(reference);
                        const auto found = parameter_values.find(local);
                        if (found == parameter_values.end()) {
                            return std::nullopt;
                        }
                        return EvaluatedScalar{found->second.value, found->second.dimension};
                    });
                append_diagnostics(result, std::move(evaluated.diagnostics));
                if (evaluated.value) {
                    quantity = quantity_from_evaluated(*evaluated.value);
                }
            }
            parameter_values.emplace(parameter->name, quantity);
            parameter_values.emplace(project_prefix + parameter->name, quantity);
            iterator = unresolved_parameters.erase(iterator);
            parameter_progress = true;
        }
    }
    for (const auto& name : unresolved_parameters) {
        add_error(result, "ICAD-S0041",
                  "parameter expression has a cyclic dependency involving '" + name + "'",
                  parameter_declarations.at(name)->location);
    }
    for (const auto& parameter : program.parameters) {
        const auto found = parameter_values.find(parameter.name);
        if (found == parameter_values.end()) {
            continue;
        }
        lowered.parameters.push_back(ir::Parameter{parameter.name, found->second,
                                                    parameter.expression.source,
                                                    parameter.expression.references});
    }
    if (program.tolerance.declared) {
        const auto linear = lower_value(program.tolerance.linear, units::Dimension::length,
                                        parameter_values, result);
        const auto angular = lower_value(program.tolerance.angular, units::Dimension::angle,
                                         parameter_values, result);
        if (linear.value <= 0.0 || linear.value > 1.0 || angular.value <= 0.0 ||
            angular.value > 1.0) {
            add_error(result, "ICAD-S0040",
                      "TOLERANCE values must be greater than zero and at most 1 mm/deg",
                      program.tolerance.location);
        }
        lowered.tolerance = {linear.value, angular.value};
    }

    std::unordered_set<std::string> spatial_names;
    for (const auto& angle : program.angles) {
        if (!spatial_names.insert(angle.name).second || parameter_values.contains(angle.name)) {
            add_error(result, "ICAD-S0031", "duplicate named value '" + angle.name + "'",
                      angle.location);
        }
        auto quantity = lower_value(angle.value, units::Dimension::angle, parameter_values, result);
        parameter_values[angle.name] = quantity;
        parameter_values[project_prefix + angle.name] = quantity;
        lowered.angles.push_back({angle.name, quantity.value});
    }

    std::unordered_map<std::string, std::array<double, 3>> point_values;
    for (const auto& point : program.points) {
        if (!spatial_names.insert(point.name).second || parameter_values.contains(point.name)) {
            add_error(result, "ICAD-S0031", "duplicate spatial name '" + point.name + "'",
                      point.location);
        }
    }
    for (const auto& vector : program.vectors) {
        if (!spatial_names.insert(vector.name).second || parameter_values.contains(vector.name)) {
            add_error(result, "ICAD-S0031", "duplicate spatial name '" + vector.name + "'",
                      vector.location);
        }
    }
    std::unordered_map<std::string, std::array<double, 3>> vector_values;
    std::unordered_map<std::string, double> point_distances;
    std::unordered_map<std::string, double> vector_angles;
    for (const auto& point : program.points) {
        if (point.derived)
            continue;
        std::array<double, 3> position{};
        for (std::size_t axis = 0; axis < position.size(); ++axis) {
            position[axis] = lower_value(point.coordinates[axis], units::Dimension::length,
                                         parameter_values, result)
                                 .value;
        }
        point_values.emplace(point.name, position);
    }
    for (const auto& vector : program.vectors) {
        if (vector.derived || vector.rotated)
            continue;
        const double magnitude =
            std::hypot(vector.components[0], vector.components[1], vector.components[2]);
        if (magnitude <= 1e-12) {
            add_error(result, "ICAD-S0031", "VECTOR magnitude must be greater than zero",
                      vector.location);
        }
        std::array<double, 3> direction{};
        if (magnitude > 1e-12) {
            for (std::size_t axis = 0; axis < direction.size(); ++axis) {
                direction[axis] = vector.components[axis] / magnitude;
            }
        }
        vector_values.emplace(vector.name, direction);
    }

    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& point : program.points) {
            if (!point.derived || point_values.contains(point.name))
                continue;
            const auto base = point_values.find(point.base_point);
            const auto direction = vector_values.find(point.direction);
            if (base == point_values.end() || direction == vector_values.end())
                continue;
            const double distance =
                lower_value(point.distance, units::Dimension::length, parameter_values, result)
                    .value;
            std::array<double, 3> position{};
            for (std::size_t axis = 0; axis < position.size(); ++axis)
                position[axis] = base->second[axis] + direction->second[axis] * distance;
            point_values.emplace(point.name, position);
            point_distances.emplace(point.name, distance);
            progress = true;
        }
        for (const auto& vector : program.vectors) {
            if ((!vector.derived && !vector.rotated) || vector_values.contains(vector.name))
                continue;
            std::array<double, 3> direction{};
            if (vector.rotated) {
                const auto source = vector_values.find(vector.source_vector);
                const auto axis = vector_values.find(vector.around_axis);
                if (source == vector_values.end() || axis == vector_values.end())
                    continue;
                const double degrees =
                    lower_value(vector.angle, units::Dimension::angle, parameter_values, result)
                        .value;
                const double radians = degrees * std::numbers::pi / 180.0;
                const double cosine = std::cos(radians);
                const double sine = std::sin(radians);
                const auto& value = source->second;
                const auto& rotation_axis = axis->second;
                const std::array<double, 3> cross{
                    rotation_axis[1] * value[2] - rotation_axis[2] * value[1],
                    rotation_axis[2] * value[0] - rotation_axis[0] * value[2],
                    rotation_axis[0] * value[1] - rotation_axis[1] * value[0]};
                const double projection = rotation_axis[0] * value[0] +
                                          rotation_axis[1] * value[1] +
                                          rotation_axis[2] * value[2];
                for (std::size_t component = 0; component < direction.size(); ++component) {
                    direction[component] = value[component] * cosine + cross[component] * sine +
                                           rotation_axis[component] * projection * (1.0 - cosine);
                }
                vector_angles.emplace(vector.name, degrees);
            } else {
                const auto from = point_values.find(vector.from_point);
                const auto to = point_values.find(vector.to_point);
                if (from == point_values.end() || to == point_values.end())
                    continue;
                for (std::size_t axis = 0; axis < direction.size(); ++axis)
                    direction[axis] = to->second[axis] - from->second[axis];
            }
            const double magnitude = std::hypot(direction[0], direction[1], direction[2]);
            if (magnitude <= 1e-12) {
                add_error(result, "ICAD-S0031",
                          vector.rotated ? "rotated VECTOR magnitude must be greater than zero"
                                         : "derived VECTOR endpoints must be different",
                          vector.location);
            } else {
                for (auto& component : direction)
                    component /= magnitude;
            }
            vector_values.emplace(vector.name, direction);
            progress = true;
        }
    }

    for (const auto& point : program.points) {
        const auto value = point_values.find(point.name);
        if (value == point_values.end()) {
            add_error(result, "ICAD-S0031",
                      "derived POINT3 has an unknown or cyclic point/vector dependency",
                      point.location);
            continue;
        }
        ir::SpatialPoint lowered_point;
        lowered_point.name = point.name;
        lowered_point.position_mm = value->second;
        if (point.derived) {
            lowered_point.kind = ir::SpatialPointKind::offset;
            lowered_point.base_point = point.base_point;
            lowered_point.direction = point.direction;
            lowered_point.distance_mm = point_distances.at(point.name);
            lowered_point.distance_reference = point.distance.parameter_reference;
        }
        lowered.points.push_back(std::move(lowered_point));
    }
    for (const auto& vector : program.vectors) {
        const auto value = vector_values.find(vector.name);
        if (value == vector_values.end()) {
            add_error(result, "ICAD-S0031",
                      "derived VECTOR has an unknown or cyclic point/vector dependency",
                      vector.location);
            continue;
        }
        ir::Direction lowered_vector;
        lowered_vector.name = vector.name;
        lowered_vector.unit = value->second;
        if (vector.rotated) {
            lowered_vector.kind = ir::DirectionKind::rotated;
            lowered_vector.source_direction = vector.source_vector;
            lowered_vector.around_axis = vector.around_axis;
            lowered_vector.angle_degrees = vector_angles.at(vector.name);
            lowered_vector.angle_reference = vector.angle.parameter_reference;
        } else if (vector.derived) {
            lowered_vector.kind = ir::DirectionKind::between_points;
            lowered_vector.from_point = vector.from_point;
            lowered_vector.to_point = vector.to_point;
        }
        lowered.vectors.push_back(std::move(lowered_vector));
    }

    std::unordered_set<std::string> material_names;
    for (const auto& material : program.materials) {
        material_names.insert(material.name);
        const auto* physical_profile = material.profile.empty()
                                           ? nullptr
                                           : materials::find_profile(material.profile);
        if (!material.profile.empty() && physical_profile == nullptr) {
            add_error(result, "ICAD-S0037",
                      "unknown engineering material profile '" + material.profile + "'",
                      material.location);
            continue;
        }
        const std::string resolved_preset = material.preset.empty() && physical_profile != nullptr
                                                ? physical_profile->appearance_preset
                                                : material.preset;
        if (resolved_preset.empty()) {
            add_error(result, "ICAD-S0011", "material block requires PROFILE or PRESET",
                      material.location);
            continue;
        }
        const auto preset = materials::find(resolved_preset);
        if (!preset) {
            add_error(result, "ICAD-S0011", "unknown predefined material '" + resolved_preset + "'",
                      material.location);
            continue;
        }
        ir::Material lowered_material{material.name,
                                      physical_profile == nullptr ? std::string{}
                                                                  : physical_profile->id,
                                      resolved_preset, preset->base_color,
                                      preset->metallic, preset->roughness,
                                      std::string{preset->texture}, preset->texture_seed};
        if (material.has_base_color) {
            if (std::ranges::any_of(material.base_color,
                                    [](double value) { return value < 0.0 || value > 1.0; })) {
                add_error(result, "ICAD-S0011", "BASE_COLOR components must be within [0, 1]",
                          material.location);
            }
            lowered_material.base_color = material.base_color;
        }
        if (material.has_metallic) {
            if (material.metallic < 0.0 || material.metallic > 1.0) {
                add_error(result, "ICAD-S0011", "METALLIC must be within [0, 1]",
                          material.location);
            }
            lowered_material.metallic = material.metallic;
        }
        if (material.has_roughness) {
            if (material.roughness < 0.0 || material.roughness > 1.0) {
                add_error(result, "ICAD-S0011", "ROUGHNESS must be within [0, 1]",
                          material.location);
            }
            lowered_material.roughness = material.roughness;
        }
        if (material.has_texture_scale) {
            const auto scale = lower_value(material.texture_scale, units::Dimension::length,
                                           parameter_values, result);
            if (scale.value <= 0.0) {
                add_error(result, "ICAD-S0011", "TEXTURE_SCALE must be positive",
                          material.location);
            }
            lowered_material.texture_scale_mm = scale.value;
        }
        if (!material.uv_mode.empty()) {
            constexpr std::string_view uv_modes[]{"BOX", "PLANAR", "CYLINDRICAL", "SPHERICAL"};
            if (std::ranges::find(uv_modes, material.uv_mode) == std::end(uv_modes)) {
                add_error(result, "ICAD-S0011",
                          "UV_MODE must be BOX, PLANAR, CYLINDRICAL, or SPHERICAL",
                          material.location);
            }
            lowered_material.uv_mode = material.uv_mode;
        }
        lowered.materials.push_back(std::move(lowered_material));
    }

    std::unordered_map<std::string, std::size_t> profile_indices;
    for (const auto& profile : program.profiles) {
        ir::Profile lowered_profile;
        lowered_profile.name = profile.name;
        lower_profile(profile, lowered_profile, result);
        if (lowered_profile.points.size() < 3) {
            add_error(result, "ICAD-S0019", "profile requires at least three points",
                      profile.location);
        } else if (has_duplicate_consecutive_point(lowered_profile.points)) {
            add_error(result, "ICAD-S0020", "profile must not contain duplicate consecutive points",
                      profile.location);
        } else if (has_adjacent_overlap(lowered_profile.points)) {
            add_error(result, "ICAD-S0020", "profile boundary must not backtrack or overlap",
                      profile.location);
        } else if (std::abs(signed_area(lowered_profile.points)) <= 1e-9) {
            add_error(result, "ICAD-S0020", "profile area must be non-zero", profile.location);
        } else if (self_intersects(lowered_profile.points)) {
            add_error(result, "ICAD-S0020", "profile must not self-intersect", profile.location);
        }
        profile_indices.emplace(profile.name, lowered.profiles.size());
        lowered.profiles.push_back(std::move(lowered_profile));
    }

    struct SketchSource {
        std::string body;
        const ast::SketchDecl* declaration{};
    };
    std::vector<SketchSource> sketch_declarations;
    sketch_declarations.reserve(program.sketches.size());
    for (const auto& sketch : program.sketches)
        sketch_declarations.push_back({{}, &sketch});
    for (const auto& body : program.bodies) {
        for (const auto& sketch : body.sketches)
            sketch_declarations.push_back({body.name, &sketch});
    }
    for (const auto& source : sketch_declarations) {
        const auto& sketch = *source.declaration;
        const std::string canonical_name =
            source.body.empty() ? sketch.name : source.body + "::" + sketch.name;
        ir::Sketch lowered_sketch;
        lowered_sketch.name = canonical_name;
        lowered_sketch.body = source.body;
        lowered_sketch.plane = sketch.plane;
        lowered_sketch.support_feature = sketch.support_feature;
        lowered_sketch.support_face = sketch.support_face;
        lowered_sketch.support_reference = sketch.support_reference;
        lowered_sketch.support_topology_id = sketch.support_topology_path;
        lowered_sketch.solve_requirement = sketch.solve_requirement;
        if (!sketch.shapes.empty()) {
            bool valid = true;
            std::unordered_set<std::string> point_names;
            std::unordered_set<std::string> entity_names;
            for (const auto& shape : sketch.shapes) {
                ir::SketchShape lowered_shape;
                lowered_shape.name = shape.name;
                lowered_shape.role = shape.role;
                lowered_shape.closed = shape.closure == "CLOSED";
                lowered_shape.profile = canonical_name + "." + shape.name;
                if (!lowered_shape.closed && shape.role != "CONSTRUCTION") {
                    add_error(result, "ICAD-S0042",
                              "OPEN SHAPE requires ROLE CONSTRUCTION in MULTI_SHAPE_SKETCH_V1",
                              shape.location);
                    valid = false;
                }
                for (const auto& point : shape.points) {
                    const std::string scoped_name = shape.name + "." + point.name;
                    if (!point_names.insert(scoped_name).second) {
                        add_error(result, "ICAD-S0042",
                                  "duplicate scoped SHAPE point '" + scoped_name + "'",
                                  point.location);
                        valid = false;
                    }
                    const double x = lower_value(point.x, units::Dimension::length,
                                                 parameter_values, result)
                                         .value;
                    const double y = lower_value(point.y, units::Dimension::length,
                                                 parameter_values, result)
                                         .value;
                    lowered_sketch.points.push_back(
                        ir::SketchPoint{scoped_name, {x, y}, {x, y}, point.fixed});
                    lowered_shape.points.push_back(scoped_name);
                }
                bool saw_circle = false;
                for (const auto& entity : shape.entities) {
                    const std::string scoped_name = shape.name + "." + entity.name;
                    const auto point_name = [&](const std::string& local) {
                        return local.empty() ? std::string{} : shape.name + "." + local;
                    };
                    ir::SketchEntity lowered_entity;
                    lowered_entity.name = scoped_name;
                    lowered_entity.kind = entity.kind == ast::SketchEntityKind::line
                                              ? ir::ProfileSegmentKind::line
                                              : ir::ProfileSegmentKind::circular_arc;
                    lowered_entity.start = point_name(entity.start);
                    lowered_entity.end = point_name(entity.end);
                    lowered_entity.center = point_name(entity.center);
                    lowered_entity.counterclockwise = entity.counterclockwise;
                    lowered_entity.full_circle = entity.kind == ast::SketchEntityKind::circle;
                    if (lowered_entity.full_circle) {
                        const auto radius = lower_value(entity.radius, units::Dimension::length,
                                                        parameter_values, result);
                        lowered_entity.radius_mm = radius.value;
                        lowered_entity.radius_reference = entity.radius.parameter_reference;
                        if (radius.value <= 0.0) {
                            add_error(result, "ICAD-S0042",
                                      "SHAPE CIRCLE radius must be positive", entity.location);
                            valid = false;
                        }
                        saw_circle = true;
                    }
                    if (!entity_names.insert(scoped_name).second ||
                        (lowered_entity.full_circle &&
                         !point_names.contains(lowered_entity.center)) ||
                        (!lowered_entity.full_circle &&
                         (!point_names.contains(lowered_entity.start) ||
                          !point_names.contains(lowered_entity.end) ||
                          (entity.kind == ast::SketchEntityKind::circular_arc &&
                           !point_names.contains(lowered_entity.center))))) {
                        add_error(result, "ICAD-S0042",
                                  "SHAPE entities must be unique and reference local points",
                                  entity.location);
                        valid = false;
                    }
                    lowered_sketch.entities.push_back(std::move(lowered_entity));
                    lowered_shape.entities.push_back(scoped_name);
                }
                if (shape.entities.empty()) {
                    add_error(result, "ICAD-S0042", "SHAPE requires boundary entities",
                              shape.location);
                    valid = false;
                }
                if (saw_circle && shape.entities.size() != 1) {
                    add_error(result, "ICAD-S0042",
                              "a CIRCLE must be the only boundary entity in its SHAPE",
                              shape.location);
                    valid = false;
                }
                if (lowered_shape.closed && !saw_circle) {
                    for (std::size_t entity = 0; entity < shape.entities.size(); ++entity) {
                        const auto& current = shape.entities[entity];
                        const auto& next = shape.entities[(entity + 1) % shape.entities.size()];
                        if (current.end != next.start) {
                            add_error(result, "ICAD-S0043",
                                      "ordered CLOSED SHAPE entities must form one endpoint chain",
                                      next.location);
                            valid = false;
                        }
                    }
                }
                lowered_sketch.shapes.push_back(std::move(lowered_shape));
            }
            for (const auto& constraint : sketch.constraints) {
                if (!lower_sketch_constraint(constraint, lowered_sketch, parameter_values, result,
                                             "ICAD-S0042"))
                    valid = false;
            }
            if (valid) {
                constraints::solve_sketch(lowered_sketch);
                if (lowered_sketch.status == ir::SketchSolveStatus::inconsistent) {
                    add_error(result, "ICAD-S0044",
                              "multi-shape sketch constraints are inconsistent", sketch.location);
                    valid = false;
                } else if (sketch.solve_requirement == "FULL" &&
                           lowered_sketch.status != ir::SketchSolveStatus::fully_constrained) {
                    add_error(result, "ICAD-S0044",
                              "SOLVE FULL requires a fully constrained multi-shape sketch",
                              sketch.location);
                    valid = false;
                }
            }
            std::vector<std::size_t> shape_profile_indices(sketch.shapes.size(),
                                                           static_cast<std::size_t>(-1));
            if (valid) {
                const auto solved_point = [&](const std::string& name) {
                    return std::ranges::find(lowered_sketch.points, name,
                                             &ir::SketchPoint::name)
                        ->solved;
                };
                constexpr double full_circle = 2.0 * std::numbers::pi;
                for (std::size_t shape_index = 0; shape_index < sketch.shapes.size();
                     ++shape_index) {
                    const auto& shape = sketch.shapes[shape_index];
                    auto& lowered_shape = lowered_sketch.shapes[shape_index];
                    if (!lowered_shape.closed)
                        continue;
                    ir::Profile profile;
                    profile.name = lowered_shape.profile;
                    const auto first_entity = std::ranges::find(
                        lowered_sketch.entities, lowered_shape.entities.front(),
                        &ir::SketchEntity::name);
                    if (first_entity != lowered_sketch.entities.end() &&
                        first_entity->full_circle) {
                        const auto center = solved_point(first_entity->center);
                        const ir::Point2 start{center.x_mm + first_entity->radius_mm, center.y_mm};
                        profile.segments.push_back({ir::ProfileSegmentKind::circular_arc, start,
                                                    start, center, first_entity->radius_mm,
                                                    full_circle});
                    } else {
                        for (const auto& entity_name : lowered_shape.entities) {
                            const auto entity = std::ranges::find(
                                lowered_sketch.entities, entity_name, &ir::SketchEntity::name);
                            if (entity == lowered_sketch.entities.end())
                                continue;
                            const auto start = solved_point(entity->start);
                            const auto end = solved_point(entity->end);
                            if (entity->kind == ir::ProfileSegmentKind::line) {
                                profile.segments.push_back(line_segment(start, end));
                                continue;
                            }
                            const auto center = solved_point(entity->center);
                            const double start_radius =
                                std::hypot(start.x_mm - center.x_mm, start.y_mm - center.y_mm);
                            const double end_radius =
                                std::hypot(end.x_mm - center.x_mm, end.y_mm - center.y_mm);
                            const double scale = std::max({1.0, start_radius, end_radius});
                            if (start_radius <= 1e-9 ||
                                std::abs(start_radius - end_radius) > 1e-7 * scale) {
                                add_error(result, "ICAD-S0043",
                                          "SHAPE ARC endpoints require an equal non-zero radius",
                                          shape.location);
                                valid = false;
                            }
                            const double start_angle =
                                std::atan2(start.y_mm - center.y_mm,
                                           start.x_mm - center.x_mm);
                            const double end_angle =
                                std::atan2(end.y_mm - center.y_mm, end.x_mm - center.x_mm);
                            double sweep = end_angle - start_angle;
                            if (entity->counterclockwise) {
                                while (sweep <= 0.0)
                                    sweep += full_circle;
                            } else {
                                while (sweep >= 0.0)
                                    sweep -= full_circle;
                            }
                            profile.segments.push_back({ir::ProfileSegmentKind::circular_arc,
                                                        start, end, center, start_radius, sweep});
                        }
                    }
                    profile.points = tessellate(profile.segments);
                    if (signed_area(profile.points) < 0.0) {
                        reverse_segments(profile.segments);
                        profile.points = tessellate(profile.segments);
                    }
                    if (profile.points.size() < 3 ||
                        std::abs(signed_area(profile.points)) <= 1e-9 ||
                        has_duplicate_consecutive_point(profile.points) ||
                        has_adjacent_overlap(profile.points) || self_intersects(profile.points)) {
                        add_error(result, "ICAD-S0043",
                                  "CLOSED SHAPE must produce a simple non-zero region",
                                  shape.location);
                        valid = false;
                        continue;
                    }
                    lowered_shape.area_mm2 = std::abs(signed_area(profile.points));
                    shape_profile_indices[shape_index] = lowered.profiles.size();
                    profile_indices.emplace(profile.name, lowered.profiles.size());
                    lowered.profiles.push_back(std::move(profile));
                }
            }
            if (valid) {
                for (std::size_t first = 0; first < sketch.shapes.size(); ++first) {
                    if (shape_profile_indices[first] == static_cast<std::size_t>(-1))
                        continue;
                    for (std::size_t second = first + 1; second < sketch.shapes.size(); ++second) {
                        if (shape_profile_indices[second] == static_cast<std::size_t>(-1))
                            continue;
                        const auto& first_profile = lowered.profiles[shape_profile_indices[first]];
                        const auto& second_profile = lowered.profiles[shape_profile_indices[second]];
                        if (boundaries_intersect(first_profile.points, second_profile.points)) {
                            add_error(result, "ICAD-S0045",
                                      "SHAPE boundaries may not intersect or touch",
                                      sketch.shapes[second].location);
                            valid = false;
                        }
                    }
                }
                for (std::size_t hole = 0; hole < sketch.shapes.size(); ++hole) {
                    if (sketch.shapes[hole].role != "HOLE" ||
                        shape_profile_indices[hole] == static_cast<std::size_t>(-1)) {
                        continue;
                    }
                    const auto& hole_profile = lowered.profiles[shape_profile_indices[hole]];
                    std::size_t containers = 0;
                    for (std::size_t material = 0; material < sketch.shapes.size(); ++material) {
                        const auto role = sketch.shapes[material].role;
                        if ((role != "STOCK" && role != "ADDITIVE") ||
                            shape_profile_indices[material] == static_cast<std::size_t>(-1)) {
                            continue;
                        }
                        const auto& material_profile =
                            lowered.profiles[shape_profile_indices[material]];
                        if (point_inside(hole_profile.points.front(), material_profile.points)) {
                            ++containers;
                            lowered_sketch.shapes[hole].containing_shape =
                                sketch.shapes[material].name;
                        }
                    }
                    if (containers != 1) {
                        add_error(result, "ICAD-S0045",
                                  "HOLE SHAPE must be contained by exactly one STOCK or ADDITIVE "
                                  "SHAPE",
                                  sketch.shapes[hole].location);
                        valid = false;
                    }
                }
            }
            if (valid) {
                std::unordered_set<std::string> region_names;
                for (const auto& region : sketch.regions) {
                    ir::SketchRegion lowered_region;
                    lowered_region.name = region.name;
                    lowered_region.outer_shape = region.outer_shape;
                    lowered_region.hole_shapes = region.hole_shapes;
                    if (!region_names.insert(region.name).second) {
                        add_error(result, "ICAD-S0046",
                                  "duplicate REGION name '" + region.name + "'", region.location);
                        valid = false;
                        continue;
                    }
                    const auto outer = std::ranges::find(
                        lowered_sketch.shapes, region.outer_shape, &ir::SketchShape::name);
                    if (outer == lowered_sketch.shapes.end() || !outer->closed ||
                        (outer->role != "STOCK" && outer->role != "ADDITIVE")) {
                        add_error(result, "ICAD-S0046",
                                  "REGION OUTER must name one closed STOCK or ADDITIVE SHAPE",
                                  region.location);
                        valid = false;
                        continue;
                    }
                    lowered_region.outer_profile = outer->profile;
                    lowered_region.area_mm2 = outer->area_mm2;
                    for (const auto& hole_name : region.hole_shapes) {
                        const auto hole = std::ranges::find(
                            lowered_sketch.shapes, hole_name, &ir::SketchShape::name);
                        if (hole == lowered_sketch.shapes.end() || !hole->closed ||
                            hole->role != "HOLE" || hole->containing_shape != outer->name) {
                            add_error(result, "ICAD-S0046",
                                      "REGION HOLES must name closed HOLE SHAPEs contained by its OUTER",
                                      region.location);
                            valid = false;
                            continue;
                        }
                        lowered_region.hole_profiles.push_back(hole->profile);
                        lowered_region.area_mm2 -= hole->area_mm2;
                    }
                    if (lowered_region.area_mm2 <= 1e-9) {
                        add_error(result, "ICAD-S0046",
                                  "REGION must retain positive material area", region.location);
                        valid = false;
                    }
                    lowered_sketch.regions.push_back(std::move(lowered_region));
                }
            }
            lowered.sketches.push_back(std::move(lowered_sketch));
            continue;
        }
        if (sketch.circle) {
            const auto center_x = lower_value(sketch.circle_center[0], units::Dimension::length,
                                              parameter_values, result);
            const auto center_y = lower_value(sketch.circle_center[1], units::Dimension::length,
                                              parameter_values, result);
            const auto radius = lower_value(sketch.circle_radius, units::Dimension::length,
                                            parameter_values, result);
            if (radius.value <= 0.0) {
                add_error(result, "ICAD-S0037", "SKETCH CIRCLE radius must be positive",
                          sketch.location);
            }
            constexpr double full_circle = 2.0 * std::numbers::pi;
            ir::Profile sketch_profile;
            sketch_profile.name = canonical_name;
            const ir::Point2 center{center_x.value, center_y.value};
            const ir::Point2 start{center.x_mm + radius.value, center.y_mm};
            sketch_profile.segments.push_back({ir::ProfileSegmentKind::circular_arc, start, start,
                                               center, radius.value, full_circle});
            sketch_profile.points = tessellate(sketch_profile.segments);
            profile_indices.emplace(canonical_name, lowered.profiles.size());
            lowered.profiles.push_back(std::move(sketch_profile));
            lowered_sketch.status = ir::SketchSolveStatus::fully_constrained;
            lowered.sketches.push_back(std::move(lowered_sketch));
            continue;
        }
        std::unordered_set<std::string> sketch_point_names;
        bool valid = true;
        for (const auto& point : sketch.points) {
            if (!sketch_point_names.insert(point.name).second) {
                valid = false;
                continue;
            }
            const double x = lower_value(point.x, units::Dimension::length, parameter_values,
                                         result)
                                 .value;
            const double y = lower_value(point.y, units::Dimension::length, parameter_values,
                                         result)
                                 .value;
            lowered_sketch.points.push_back(
                ir::SketchPoint{point.name, {x, y}, {x, y}, point.fixed});
        }
        if (sketch.points.size() < 2) {
            add_error(result, "ICAD-S0037", "SKETCH requires at least two named points",
                      sketch.location);
            valid = false;
        }
        std::unordered_set<std::string> sketch_entity_names;
        for (const auto& entity : sketch.entities) {
            if (!sketch_entity_names.insert(entity.name).second ||
                !sketch_point_names.contains(entity.start) ||
                !sketch_point_names.contains(entity.end) ||
                (entity.kind == ast::SketchEntityKind::circular_arc &&
                 !sketch_point_names.contains(entity.center))) {
                add_error(result, "ICAD-S0037",
                          "sketch entity names must be unique and reference declared points",
                          entity.location);
                valid = false;
            }
            lowered_sketch.entities.push_back(
                {entity.name,
                 entity.kind == ast::SketchEntityKind::circular_arc
                     ? ir::ProfileSegmentKind::circular_arc
                     : ir::ProfileSegmentKind::line,
                 entity.start, entity.end, entity.center, entity.counterclockwise, false, 0.0,
                 {}});
        }
        if (!sketch.entities.empty()) {
            for (std::size_t entity = 0; entity < sketch.entities.size(); ++entity) {
                const auto& current = sketch.entities[entity];
                const auto& next = sketch.entities[(entity + 1) % sketch.entities.size()];
                if (current.end != next.start) {
                    add_error(result, "ICAD-S0039",
                              "ordered sketch entities must form one closed endpoint chain",
                              next.location);
                    valid = false;
                }
            }
        }
        for (const auto& constraint : sketch.constraints) {
            if (!lower_sketch_constraint(constraint, lowered_sketch, parameter_values, result,
                                         "ICAD-S0037"))
                valid = false;
        }
        if (valid) {
            constraints::solve_sketch(lowered_sketch);
            if (lowered_sketch.status == ir::SketchSolveStatus::inconsistent) {
                add_error(result, "ICAD-S0038",
                          "SKETCH constraints are inconsistent or failed to converge",
                          sketch.location);
            } else if (sketch.solve_requirement == "FULL" &&
                       lowered_sketch.status != ir::SketchSolveStatus::fully_constrained) {
                add_error(result, "ICAD-S0044",
                          "SOLVE FULL requires a fully constrained sketch", sketch.location);
            }
        }
        if (valid && lowered_sketch.status != ir::SketchSolveStatus::inconsistent &&
            lowered_sketch.points.size() >= 3) {
            ir::Profile sketch_profile;
            sketch_profile.name = canonical_name;
            if (lowered_sketch.entities.empty()) {
                for (const auto& point : lowered_sketch.points)
                    sketch_profile.points.push_back(point.solved);
                if (signed_area(sketch_profile.points) < 0.0)
                    std::ranges::reverse(sketch_profile.points);
                for (std::size_t point = 0; point < sketch_profile.points.size(); ++point) {
                    const auto& start = sketch_profile.points[point];
                    const auto& end =
                        sketch_profile.points[(point + 1) % sketch_profile.points.size()];
                    sketch_profile.segments.push_back(ir::ProfileSegment{
                        ir::ProfileSegmentKind::line, start, end, {}, 0.0, 0.0});
                }
            } else {
                const auto solved_point = [&](const std::string& name) {
                    return std::ranges::find(lowered_sketch.points, name,
                                             &ir::SketchPoint::name)
                        ->solved;
                };
                constexpr double full_circle = 2.0 * std::numbers::pi;
                for (const auto& entity : lowered_sketch.entities) {
                    const auto start = solved_point(entity.start);
                    const auto end = solved_point(entity.end);
                    if (entity.kind == ir::ProfileSegmentKind::line) {
                        sketch_profile.segments.push_back(line_segment(start, end));
                        continue;
                    }
                    const auto center = solved_point(entity.center);
                    const double start_radius =
                        std::hypot(start.x_mm - center.x_mm, start.y_mm - center.y_mm);
                    const double end_radius =
                        std::hypot(end.x_mm - center.x_mm, end.y_mm - center.y_mm);
                    const double scale = std::max({1.0, start_radius, end_radius});
                    if (start_radius <= 1e-9 ||
                        std::abs(start_radius - end_radius) > 1e-7 * scale) {
                        add_error(result, "ICAD-S0039",
                                  "sketch ARC endpoints must have equal non-zero center radius",
                                  sketch.location);
                    }
                    const double start_angle =
                        std::atan2(start.y_mm - center.y_mm, start.x_mm - center.x_mm);
                    const double end_angle =
                        std::atan2(end.y_mm - center.y_mm, end.x_mm - center.x_mm);
                    double sweep = end_angle - start_angle;
                    if (entity.counterclockwise) {
                        while (sweep <= 0.0)
                            sweep += full_circle;
                    } else {
                        while (sweep >= 0.0)
                            sweep -= full_circle;
                    }
                    sketch_profile.segments.push_back({ir::ProfileSegmentKind::circular_arc,
                                                       start, end, center, start_radius, sweep});
                }
                sketch_profile.points = tessellate(sketch_profile.segments);
                if (signed_area(sketch_profile.points) < 0.0) {
                    reverse_segments(sketch_profile.segments);
                    sketch_profile.points = tessellate(sketch_profile.segments);
                }
            }
            if (std::abs(signed_area(sketch_profile.points)) <= 1e-9 ||
                has_duplicate_consecutive_point(sketch_profile.points) ||
                has_adjacent_overlap(sketch_profile.points) ||
                self_intersects(sketch_profile.points)) {
                add_error(result, "ICAD-S0039",
                          "solved SKETCH boundary must be a simple non-zero closed profile",
                          sketch.location);
            } else {
                profile_indices.emplace(canonical_name, lowered.profiles.size());
                lowered.profiles.push_back(std::move(sketch_profile));
            }
        }
        lowered.sketches.push_back(std::move(lowered_sketch));
    }

    std::unordered_set<std::string> body_names;
    for (const auto& body : program.bodies) {
        body_names.insert(body.name);
    }

    std::unordered_set<std::string> posed_bodies;
    for (const auto& pose : program.poses) {
        if (!body_names.contains(pose.body)) {
            add_error(result, "ICAD-S0032", "POSE references unknown BODY '" + pose.body + "'",
                      pose.location);
        }
        const auto point = point_values.find(pose.point);
        if (point == point_values.end()) {
            add_error(result, "ICAD-S0032", "POSE references unknown POINT3 '" + pose.point + "'",
                      pose.location);
        }
        if (!posed_bodies.insert(pose.body).second) {
            add_error(result, "ICAD-S0032", "BODY may have only one POSE", pose.location);
        }
        ir::Transform transform;
        if (point != point_values.end()) {
            transform.position_mm = point->second;
        }
        for (std::size_t axis = 0; axis < transform.rotation_deg.size(); ++axis) {
            transform.rotation_deg[axis] =
                lower_value(pose.rotation[axis], units::Dimension::angle, parameter_values, result)
                    .value;
        }
        lowered.poses.push_back({pose.body, pose.point, transform});
    }

    std::unordered_set<std::string> occurrence_names = body_names;
    for (const auto& instance : program.instances) {
        if (!body_names.contains(instance.body)) {
            add_error(result, "ICAD-S0041",
                      "INSTANCE references unknown BODY definition '" + instance.body + "'",
                      instance.location);
        }
        const auto point = point_values.find(instance.point);
        if (point == point_values.end()) {
            add_error(result, "ICAD-S0041",
                      "INSTANCE references unknown POINT3 '" + instance.point + "'",
                      instance.location);
        }
        if (!occurrence_names.insert(instance.name).second) {
            add_error(result, "ICAD-S0041", "INSTANCE name collides with an occurrence",
                      instance.location);
        }
        ir::Transform transform;
        if (point != point_values.end())
            transform.position_mm = point->second;
        for (std::size_t axis = 0; axis < transform.rotation_deg.size(); ++axis) {
            transform.rotation_deg[axis] =
                lower_value(instance.rotation[axis], units::Dimension::angle, parameter_values,
                            result)
                    .value;
        }
        lowered.instances.push_back(
            {instance.name, instance.body, instance.point, transform});
    }

    for (const auto& body : program.bodies) {
        ir::Body lowered_body;
        lowered_body.name = body.name;
        lowered_body.material = body.material;
        for (const auto& reference : body.face_references) {
            lowered_body.face_references.push_back(
                {reference.name, reference.feature, reference.role, reference.topology_path});
        }
        for (const auto& selection : body.topology_selections) {
            const auto source_feature = std::ranges::find(
                body.features, selection.source_feature, &ast::FeatureDecl::name);
            bool annular_source = source_feature != body.features.end() &&
                                  source_feature->type == "EXTRUDE" &&
                                  !source_feature->region.empty();
            if (annular_source) {
                const auto separator = source_feature->region.find('.');
                const auto sketch_name = source_feature->region.substr(0, separator);
                const auto region_name = separator == std::string::npos
                                             ? std::string{}
                                             : source_feature->region.substr(separator + 1);
                const auto sketch = std::ranges::find(body.sketches, sketch_name,
                                                      &ast::SketchDecl::name);
                if (sketch == body.sketches.end()) {
                    annular_source = false;
                } else {
                    const auto region = std::ranges::find(sketch->regions, region_name,
                                                          &ast::SketchRegionDecl::name);
                    annular_source = region != sketch->regions.end() &&
                                     region->hole_shapes.size() == 1;
                    if (annular_source) {
                        const auto outer = std::ranges::find(
                            sketch->shapes, region->outer_shape, &ast::SketchShapeDecl::name);
                        const auto hole = std::ranges::find(
                            sketch->shapes, region->hole_shapes.front(),
                            &ast::SketchShapeDecl::name);
                        annular_source = outer != sketch->shapes.end() &&
                                         hole != sketch->shapes.end() &&
                                         outer->entities.size() == 1 &&
                                         hole->entities.size() == 1 &&
                                         outer->entities.front().kind ==
                                             ast::SketchEntityKind::circle &&
                                         hole->entities.front().kind ==
                                             ast::SketchEntityKind::circle;
                    }
                }
            }
            if (!annular_source) {
                add_error(result, "ICAD-S0047",
                          "TOPOLOGY_QUERY_V1 currently requires an EXTRUDE source from a REGION with exactly one circular hole",
                          selection.location);
            }
            const std::string location = selection.adjacent_face;
            const std::string classification =
                selection.convexity == "CONCAVE" ? "INNER" : "OUTER";
            lowered_body.topology_selections.push_back(
                {selection.name,
                 selection.source_feature,
                 selection.entity_kind,
                 selection.geometry,
                 selection.convexity,
                 selection.adjacent_face,
                 body.name + "/" + selection.source_feature + "/edge.loop." +
                     (location == "TOP" ? "top" : "bottom") + "." +
                     (classification == "INNER" ? "inner" : "outer")});
        }
        if (!body.material.empty() && !material_names.contains(body.material)) {
            add_error(result, "ICAD-S0012",
                      "BODY references unknown material '" + body.material + "'", body.location);
        }
        const auto scoped_profile = [&](const std::string& name) {
            if (name.empty())
                return std::string{};
            const auto separator = name.find('.');
            const auto sketch_name = name.substr(0, separator);
            const bool body_sketch = std::ranges::any_of(
                body.sketches, [&](const auto& sketch) { return sketch.name == sketch_name; });
            return body_sketch ? body.name + "::" + name : name;
        };
        for (std::size_t feature_index = 0; feature_index < body.features.size(); ++feature_index) {
            const auto& feature = body.features[feature_index];
            ir::Feature lowered_feature;
            lowered_feature.name = feature.name;
            lowered_feature.source_keyword = feature.source_keyword;
            lowered_feature.type = feature.type;
            lowered_feature.region = scoped_profile(feature.region);
            lowered_feature.profile = scoped_profile(feature.profile);
            if (!lowered_feature.region.empty()) {
                bool found_region = false;
                for (const auto& candidate_sketch : lowered.sketches) {
                    const std::string prefix = candidate_sketch.name + ".";
                    if (!lowered_feature.region.starts_with(prefix))
                        continue;
                    const auto region_name = lowered_feature.region.substr(prefix.size());
                    const auto region = std::ranges::find(candidate_sketch.regions, region_name,
                                                          &ir::SketchRegion::name);
                    if (region == candidate_sketch.regions.end())
                        continue;
                    lowered_feature.profile = region->outer_profile;
                    lowered_feature.region_hole_profiles = region->hole_profiles;
                    found_region = true;
                    break;
                }
                if (!found_region) {
                    add_error(result, "ICAD-S0046",
                              "feature references unknown or invalid REGION '" + feature.region +
                                  "'",
                              feature.location);
                }
            }
            lowered_feature.target_profile = scoped_profile(feature.target_profile);
            lowered_feature.selected_edge_point = feature.selected_edge_point;
            lowered_feature.selected_edge_location = feature.selected_edge_location;
            lowered_feature.selected_edge_classification =
                feature.selected_edge_classification;
            lowered_feature.selected_edge_set = feature.selected_edge_set;
            if (!feature.selected_edge_set.empty()) {
                const auto selection = std::ranges::find(
                    lowered_body.topology_selections, feature.selected_edge_set,
                    &ir::TopologySelection::name);
                const auto declaration = std::ranges::find(
                    body.topology_selections, feature.selected_edge_set,
                    &ast::TopologySelectionDecl::name);
                if (selection == lowered_body.topology_selections.end() ||
                    declaration == body.topology_selections.end()) {
                    add_error(result, "ICAD-S0047",
                              "SELECT EDGESET references unknown SELECTION '" +
                                  feature.selected_edge_set + "'",
                              feature.location);
                } else if (declaration->location.line >= feature.location.line) {
                    add_error(result, "ICAD-S0047",
                              "SELECT EDGESET must reference an earlier SELECTION in the BODY",
                              feature.location);
                } else if (feature_index == 0 ||
                           body.features[feature_index - 1].name !=
                               selection->source_feature) {
                    add_error(result, "ICAD-S0047",
                              "TOPOLOGY_QUERY_V1 selection source must be the immediately previous feature result",
                              feature.location);
                } else {
                    lowered_feature.selected_edge_location = selection->adjacent_face;
                    lowered_feature.selected_edge_classification =
                        selection->convexity == "CONCAVE" ? "INNER" : "OUTER";
                    lowered_feature.selected_topology_id = selection->topology_id;
                }
            }
            lowered_feature.direction = feature.direction;
            lowered_feature.plane_point = feature.plane_point;
            lowered_feature.plane_normal = feature.plane_normal;
            lowered_feature.sketch_plane = feature.sketch_plane;
            lowered_feature.support_feature = feature.support_feature;
            lowered_feature.support_face = feature.support_face;
            lowered_feature.support_reference = feature.support_reference;
            lowered_feature.support_topology_id = feature.support_topology_path;
            lowered_feature.path_points = feature.path_points;
            lowered_feature.count = feature.count;
            if (feature.operation.empty() || feature.operation == "NEW") {
                lowered_feature.operation = ir::FeatureOperation::create;
            } else if (feature.operation == "UNION") {
                lowered_feature.operation = ir::FeatureOperation::unite;
            } else if (feature.operation == "CUT") {
                lowered_feature.operation = ir::FeatureOperation::cut;
            } else if (feature.operation == "INTERSECT") {
                lowered_feature.operation = ir::FeatureOperation::intersect;
            } else {
                add_error(result, "ICAD-S0034",
                          "OPERATION must be NEW, UNION, CUT, or INTERSECT", feature.location);
            }
            if (feature_index == 0 &&
                lowered_feature.operation != ir::FeatureOperation::create) {
                add_error(result, "ICAD-S0034",
                          "the first feature in a BODY must use OPERATION NEW", feature.location);
            }

            if (feature.type.empty()) {
                add_error(result, "ICAD-S0006", "feature must declare TYPE", feature.location);
                lowered_body.features.push_back(std::move(lowered_feature));
                continue;
            }
            const auto* schema = types::find_feature_schema(feature.type);
            if (schema == nullptr) {
                add_error(result, "ICAD-S0007", "unsupported feature TYPE '" + feature.type + "'",
                          feature.location);
                lowered_body.features.push_back(std::move(lowered_feature));
                continue;
            }

            const bool edge_modifier = feature.type == "CHAMFER" || feature.type == "FILLET";
            const bool pattern_modifier = feature.type == "LINEAR_PATTERN";
            const bool mirror_modifier = feature.type == "MIRROR";
            const bool sweep_feature = feature.type == "SWEEP";
            const bool loft_feature = feature.type == "LOFT";
            const bool freeform_feature = feature.type == "FREEFORM";
            const bool modifier = edge_modifier || pattern_modifier || mirror_modifier;
            if (modifier && feature_index == 0) {
                add_error(result, "ICAD-S0035",
                          feature.type + " requires an earlier solid feature in the BODY",
                          feature.location);
            }
            if (modifier && !feature.operation.empty()) {
                add_error(result, "ICAD-S0035",
                          "modeling modifiers do not accept OPERATION", feature.location);
            }
            if (edge_modifier) {
                const bool semantic_loop = !feature.selected_edge_location.empty() &&
                                           !feature.selected_edge_classification.empty();
                const bool named_loop = !feature.selected_edge_set.empty();
                if (feature.selected_edge_point.empty() && !semantic_loop && !named_loop) {
                    add_error(result, "ICAD-S0035",
                              feature.type +
                                  " requires SELECT EDGE NEAREST point, EDGE TOP|BOTTOM INNER|OUTER, or EDGESET name",
                              feature.location);
                } else if (!feature.selected_edge_point.empty() &&
                           !point_values.contains(feature.selected_edge_point)) {
                    add_error(result, "ICAD-S0035",
                              "edge selector references unknown POINT3 '" +
                                  feature.selected_edge_point + "'",
                              feature.location);
                }
            } else if (pattern_modifier) {
                if (feature.direction.empty() || !vector_values.contains(feature.direction)) {
                    add_error(result, "ICAD-S0035",
                              "LINEAR_PATTERN requires DIRECTION with a known VECTOR",
                              feature.location);
                }
                if (!feature.has_count || feature.count < 2 || feature.count > 1000) {
                    add_error(result, "ICAD-S0035",
                              "LINEAR_PATTERN COUNT must be between 2 and 1000",
                              feature.location);
                }
            } else if (mirror_modifier) {
                if (feature.plane_point.empty() || !point_values.contains(feature.plane_point) ||
                    feature.plane_normal.empty() ||
                    !vector_values.contains(feature.plane_normal)) {
                    add_error(result, "ICAD-S0035",
                              "MIRROR requires PLANE with a known POINT3 and normal VECTOR",
                              feature.location);
                }
            } else if (sweep_feature) {
                if (feature.path_points.size() < 2) {
                    add_error(result, "ICAD-S0036",
                              "SWEEP requires PATH with at least two POINT3 values",
                              feature.location);
                }
                for (std::size_t point = 0; point < feature.path_points.size(); ++point) {
                    if (!point_values.contains(feature.path_points[point])) {
                        add_error(result, "ICAD-S0036",
                                  "SWEEP PATH references unknown POINT3 '" +
                                      feature.path_points[point] + "'",
                                  feature.location);
                        continue;
                    }
                    if (point != 0 && point_values.contains(feature.path_points[point - 1])) {
                        const auto& first = point_values.at(feature.path_points[point - 1]);
                        const auto& second = point_values.at(feature.path_points[point]);
                        const double squared =
                            (first[0] - second[0]) * (first[0] - second[0]) +
                            (first[1] - second[1]) * (first[1] - second[1]) +
                            (first[2] - second[2]) * (first[2] - second[2]);
                        if (squared <= 1e-18)
                            add_error(result, "ICAD-S0036",
                                      "SWEEP PATH cannot repeat consecutive POINT3 values",
                                      feature.location);
                    }
                }
            } else if (loft_feature || freeform_feature) {
                if (lowered_feature.target_profile.empty() ||
                    !profile_indices.contains(lowered_feature.target_profile)) {
                    add_error(result, "ICAD-S0036",
                              feature.type + " requires a known TARGET_PROFILE",
                              feature.location);
                }
                if (freeform_feature &&
                    (!feature.has_count || feature.count < 3 || feature.count > 128)) {
                    add_error(result, "ICAD-S0036",
                              "FREEFORM COUNT must be between 3 and 128 sections",
                              feature.location);
                }
            } else if (!feature.selected_edge_point.empty() ||
                       !feature.selected_edge_location.empty() ||
                       !feature.selected_edge_set.empty() || !feature.direction.empty() ||
                       !feature.plane_point.empty() || feature.has_count ||
                       !feature.path_points.empty() || !feature.target_profile.empty()) {
                add_error(result, "ICAD-S0035",
                          "selection, direction, plane, and count are valid only for modeling "
                          "modifiers",
                          feature.location);
            }

            std::unordered_set<std::string> seen_properties;
            for (const auto& property : feature.properties) {
                if (!seen_properties.insert(property.name).second) {
                    add_error(result, "ICAD-S0008", "duplicate property '" + property.name + "'",
                              property.location);
                    continue;
                }
                const auto* property_spec = types::find_property(*schema, property.name);
                if (property_spec == nullptr) {
                    add_error(result, "ICAD-S0009",
                              "property '" + property.name + "' is not valid for TYPE " +
                                  feature.type,
                              property.location);
                    continue;
                }
                ir::Quantity quantity;
                if (!property.expression.empty()) {
                    quantity = lower_expression(property.expression, property_spec->dimension,
                                                property_spec->must_be_positive, parameter_values,
                                                result);
                } else if (!property.parameter_reference.empty()) {
                    const auto parameter = parameter_values.find(property.parameter_reference);
                    if (parameter == parameter_values.end()) {
                        add_error(result, "ICAD-S0022",
                                  "unknown parameter '" + property.parameter_reference + "'",
                                  property.location);
                        continue;
                    }
                    quantity = parameter->second;
                    if (quantity.dimension != property_spec->dimension) {
                        add_error(result, "ICAD-S0023",
                                  "parameter '" + property.parameter_reference +
                                      "' has the wrong physical dimension for " + property.name,
                                  property.location);
                    }
                    if (property_spec->must_be_positive && quantity.value <= 0.0) {
                        add_error(result, "ICAD-S0003", "quantity must be greater than zero",
                                  property.location);
                    }
                } else {
                    quantity = lower_quantity(property.value, property_spec->dimension,
                                              property_spec->must_be_positive, result);
                }
                lowered_feature.properties.push_back(ir::Property{
                    property.name, std::move(quantity), property.expression.source});
            }
            for (const auto& property_spec : schema->properties) {
                if (property_spec.required &&
                    !seen_properties.contains(std::string{property_spec.name})) {
                    add_error(result, "ICAD-S0010",
                              "missing required property '" + std::string{property_spec.name} + "'",
                              feature.location);
                }
            }
            if (feature.type == "CONE") {
                const auto first_radius = std::ranges::find(
                    lowered_feature.properties, "RADIUS1", &ir::Property::name);
                const auto second_radius = std::ranges::find(
                    lowered_feature.properties, "RADIUS2", &ir::Property::name);
                if (first_radius != lowered_feature.properties.end() &&
                    second_radius != lowered_feature.properties.end() &&
                    (first_radius->value.value < 0.0 || second_radius->value.value < 0.0 ||
                     (first_radius->value.value <= 1e-9 &&
                      second_radius->value.value <= 1e-9))) {
                    add_error(result, "ICAD-S0003",
                              "CONE radii must be non-negative and at least one must be greater than zero",
                              feature.location);
                }
            }
            const bool profile_feature = feature.type == "EXTRUDE" || feature.type == "REVOLVE" ||
                                         sweep_feature || loft_feature || freeform_feature;
            if (profile_feature && lowered_feature.profile.empty()) {
                add_error(result, "ICAD-S0021", feature.type + " requires PROFILE",
                          feature.location);
            } else if (!lowered_feature.profile.empty() &&
                       !profile_indices.contains(lowered_feature.profile)) {
                add_error(result, "ICAD-S0021", "unknown profile '" + feature.profile + "'",
                          feature.location);
            } else if (!profile_feature && !lowered_feature.profile.empty()) {
                add_error(result, "ICAD-S0024", "PROFILE is not valid for this feature TYPE",
                          feature.location);
            }
            if (feature.type == "REVOLVE" &&
                profile_indices.contains(lowered_feature.profile)) {
                const auto& profile =
                    lowered.profiles[profile_indices.at(lowered_feature.profile)];
                if (std::ranges::any_of(profile.points,
                                        [](const auto& point) { return point.x_mm <= 0.0; })) {
                    add_error(result, "ICAD-S0025",
                              "REVOLVE profile radius coordinates must be greater than zero",
                              feature.location);
                }
                const auto angle =
                    std::ranges::find(lowered_feature.properties, "ANGLE", &ir::Property::name);
                if (angle != lowered_feature.properties.end() &&
                    std::abs(angle->value.value - 360.0) > 1e-9) {
                    add_error(result, "ICAD-S0025", "REVOLVE currently requires ANGLE 360 deg",
                              feature.location);
                }
            }
            lowered_body.features.push_back(std::move(lowered_feature));
        }
        lowered.bodies.push_back(std::move(lowered_body));
    }

    std::unordered_set<std::string> joint_names;
    std::unordered_map<std::string, std::string> joint_parent;
    for (const auto& joint : program.joints) {
        if (!joint_names.insert(joint.name).second) {
            add_error(result, "ICAD-S0033", "duplicate JOINT name '" + joint.name + "'",
                      joint.location);
        }
        const bool parent_exists =
            joint.parent_body == "WORLD" || occurrence_names.contains(joint.parent_body);
        if (!parent_exists || !occurrence_names.contains(joint.child_body) ||
            joint.parent_body == joint.child_body) {
            add_error(result, "ICAD-S0033",
                      "JOINT must connect WORLD or one occurrence to a different existing child occurrence",
                      joint.location);
        }
        if (!point_values.contains(joint.point) || !vector_values.contains(joint.axis)) {
            add_error(result, "ICAD-S0033", "JOINT requires an existing POINT3 and VECTOR axis",
                      joint.location);
        }
        if (joint_parent.contains(joint.child_body)) {
            add_error(result, "ICAD-S0033", "a mechanism BODY may have only one parent JOINT",
                      joint.location);
        } else {
            joint_parent.emplace(joint.child_body, joint.parent_body);
        }
        ir::Joint lowered_joint;
        lowered_joint.name = joint.name;
        lowered_joint.parent_body = joint.parent_body;
        lowered_joint.child_body = joint.child_body;
        lowered_joint.point = joint.point;
        lowered_joint.axis = joint.axis;
        if (joint.kind == "FIXED") {
            lowered_joint.kind = ir::JointKind::fixed;
        } else {
            const auto dimension =
                joint.kind == "REVOLUTE" ? units::Dimension::angle : units::Dimension::length;
            if (joint.kind != "REVOLUTE" && joint.kind != "PRISMATIC") {
                add_error(result, "ICAD-S0033", "JOINT type must be FIXED, REVOLUTE, or PRISMATIC",
                          joint.location);
            }
            lowered_joint.kind =
                joint.kind == "PRISMATIC" ? ir::JointKind::prismatic : ir::JointKind::revolute;
            const auto value = lower_value(joint.value, dimension, parameter_values, result);
            const auto lower = lower_value(joint.lower_limit, dimension, parameter_values, result);
            const auto upper = lower_value(joint.upper_limit, dimension, parameter_values, result);
            lowered_joint.value = value.value;
            lowered_joint.lower_limit = lower.value;
            lowered_joint.upper_limit = upper.value;
            lowered_joint.unit = value.unit;
            lowered_joint.value_reference = joint.value.parameter_reference;
            lowered_joint.lower_limit_reference = joint.lower_limit.parameter_reference;
            lowered_joint.upper_limit_reference = joint.upper_limit.parameter_reference;
            lowered_joint.driven = joint.driven;
            if (lower.value > upper.value || value.value < lower.value - 1e-9 ||
                value.value > upper.value + 1e-9) {
                add_error(result, "ICAD-S0033", "JOINT VALUE must be within ordered LIMIT values",
                          joint.location);
            }
        }
        lowered.joints.push_back(std::move(lowered_joint));
    }
    for (const auto& [child, parent] : joint_parent) {
        std::unordered_set<std::string> ancestors{child};
        auto current = parent;
        while (current != "WORLD" && joint_parent.contains(current)) {
            if (!ancestors.insert(current).second) {
                add_error(result, "ICAD-S0033", "JOINT graph must be acyclic", program.location);
                break;
            }
            current = joint_parent.at(current);
        }
    }

    constexpr std::string_view interface_kinds[]{
        "MOUNT", "FLANGE", "SHAFT", "BORE", "PIN", "HOLE",
        "BEARING_SEAT", "WELD_SEAM", "BOND_FACE", "DATUM"};
    std::unordered_map<std::string, const ast::InterfaceDecl*> interface_declarations;
    for (const auto& interface : program.interfaces) {
        if (!interface_declarations.emplace(interface.name, &interface).second) {
            add_error(result, "ICAD-S0041", "duplicate INTERFACE name '" + interface.name + "'",
                      interface.location);
        }
        if (!occurrence_names.contains(interface.occurrence)) {
            add_error(result, "ICAD-S0041", "INTERFACE references unknown body or instance '" +
                                                interface.occurrence + "'",
                      interface.location);
        }
        if (!point_values.contains(interface.point) || !vector_values.contains(interface.axis)) {
            add_error(result, "ICAD-S0041", "INTERFACE requires an existing POINT3 and VECTOR",
                      interface.location);
        }
        if (!std::ranges::contains(interface_kinds, interface.kind)) {
            add_error(result, "ICAD-S0041", "unsupported manufacturing INTERFACE type '" +
                                                interface.kind + "'",
                      interface.location);
        }
        ir::ComponentInterface lowered_interface;
        lowered_interface.name = interface.name;
        lowered_interface.occurrence = interface.occurrence;
        lowered_interface.point = interface.point;
        lowered_interface.axis = interface.axis;
        lowered_interface.kind = interface.kind;
        lowered_interface.has_size = interface.has_size;
        if (interface.has_size) {
            lowered_interface.size_mm =
                lower_value(interface.size, units::Dimension::length, parameter_values, result).value;
            if (lowered_interface.size_mm <= 0.0) {
                add_error(result, "ICAD-S0041", "INTERFACE SIZE must be greater than zero",
                          interface.location);
            }
        }
        lowered.interfaces.push_back(std::move(lowered_interface));
    }

    constexpr std::string_view connection_methods[]{
        "BOLTED", "SCREWED", "PINNED", "PRESS_FIT", "SLIP_FIT",
        "BEARING", "WELDED", "BRAZED", "BONDED"};
    const auto compatible_connection = [](const std::string& method,
                                          const std::string& first_kind,
                                          const std::string& second_kind) {
        const auto pair_is = [&](std::string_view first, std::string_view second) {
            return (first_kind == first && second_kind == second) ||
                   (first_kind == second && second_kind == first);
        };
        const auto both_are = [&](std::initializer_list<std::string_view> allowed) {
            return std::ranges::contains(allowed, std::string_view{first_kind}) &&
                   std::ranges::contains(allowed, std::string_view{second_kind});
        };
        if (method == "BOLTED" || method == "SCREWED") {
            return both_are({"MOUNT", "FLANGE", "HOLE"});
        }
        if (method == "PINNED" || method == "PRESS_FIT" || method == "SLIP_FIT") {
            return pair_is("PIN", "HOLE") || pair_is("SHAFT", "BORE");
        }
        if (method == "BEARING") {
            return pair_is("SHAFT", "BEARING_SEAT") || pair_is("SHAFT", "BORE");
        }
        if (method == "WELDED" || method == "BRAZED") {
            return both_are({"WELD_SEAM", "MOUNT", "FLANGE"});
        }
        if (method == "BONDED") {
            return both_are({"BOND_FACE", "MOUNT", "FLANGE"});
        }
        return false;
    };
    std::unordered_set<std::string> connection_names;
    for (const auto& connection : program.connections) {
        if (!connection_names.insert(connection.name).second) {
            add_error(result, "ICAD-S0042", "duplicate CONNECT name '" + connection.name + "'",
                      connection.location);
        }
        const auto first = interface_declarations.find(connection.first_interface);
        const auto second = interface_declarations.find(connection.second_interface);
        if (first == interface_declarations.end() || second == interface_declarations.end() ||
            connection.first_interface == connection.second_interface) {
            add_error(result, "ICAD-S0042",
                      "CONNECT requires two different existing INTERFACE declarations",
                      connection.location);
            continue;
        }
        if (first->second->occurrence == second->second->occurrence) {
            add_error(result, "ICAD-S0042", "CONNECT interfaces must belong to different occurrences",
                      connection.location);
        }
        if (!std::ranges::contains(connection_methods, connection.method)) {
            add_error(result, "ICAD-S0042", "unsupported manufacturing connection method '" +
                                                connection.method + "'",
                      connection.location);
        } else if (!compatible_connection(connection.method, first->second->kind,
                                           second->second->kind)) {
            add_error(result, "ICAD-S0042",
                      connection.method + " is incompatible with INTERFACE types " +
                          first->second->kind + " and " + second->second->kind,
                      connection.location);
        }
        if (connection.standard.empty()) {
            add_error(result, "ICAD-S0042", "CONNECT requires STANDARD manufacturing metadata",
                      connection.location);
        }
        const bool fastened = connection.method == "BOLTED" || connection.method == "SCREWED" ||
                              connection.method == "PINNED";
        const bool fitted = connection.method == "PRESS_FIT" || connection.method == "SLIP_FIT" ||
                            connection.method == "BEARING";
        if (fastened && connection.fastener.empty()) {
            add_error(result, "ICAD-S0042", connection.method + " CONNECT requires FASTENER",
                      connection.location);
        }
        if (fitted && connection.fit.empty()) {
            add_error(result, "ICAD-S0042", connection.method + " CONNECT requires FIT",
                      connection.location);
        }
        const bool welded = connection.method == "WELDED" || connection.method == "BRAZED";
        if (welded && (connection.filler.empty() || connection.weld_process.empty() ||
                       !connection.has_weld_size || !connection.has_weld_length ||
                       !connection.has_filler_diameter || !connection.has_stock_length ||
                       !connection.has_deposition_efficiency)) {
            add_error(result, "ICAD-S0042",
                      connection.method +
                          " CONNECT requires FILLER, PROCESS, WELD_SIZE, WELD_LENGTH, "
                          "FILLER_DIAMETER, STOCK_LENGTH, and DEPOSITION_EFFICIENCY",
                      connection.location);
        }

        const bool has_clearance = !connection.clearance.expression.empty() ||
                                   !connection.clearance.parameter_reference.empty() ||
                                   !connection.clearance.literal.unit.empty();
        const double clearance = has_clearance
                                     ? lower_value(connection.clearance, units::Dimension::length,
                                                   parameter_values, result)
                                           .value
                                     : lowered.tolerance.linear_mm;
        if (clearance < 0.0) {
            add_error(result, "ICAD-S0042", "CONNECT CLEARANCE cannot be negative",
                      connection.location);
        }
        const auto lower_connection_length = [&](const ast::ValueDecl& declared, bool present,
                                                 std::string_view label) {
            if (!present)
                return 0.0;
            const double lowered_value =
                lower_value(declared, units::Dimension::length, parameter_values, result).value;
            if (lowered_value <= 0.0)
                add_error(result, "ICAD-S0042", std::string{label} + " must be greater than zero",
                          connection.location);
            return lowered_value;
        };
        const double weld_size =
            lower_connection_length(connection.weld_size, connection.has_weld_size, "WELD_SIZE");
        const double weld_length = lower_connection_length(connection.weld_length,
                                                            connection.has_weld_length,
                                                            "WELD_LENGTH");
        const double filler_diameter = lower_connection_length(
            connection.filler_diameter, connection.has_filler_diameter, "FILLER_DIAMETER");
        const double stock_length = lower_connection_length(connection.stock_length,
                                                             connection.has_stock_length,
                                                             "STOCK_LENGTH");
        if (connection.has_deposition_efficiency &&
            (connection.deposition_efficiency <= 0.0 || connection.deposition_efficiency > 1.0)) {
            add_error(result, "ICAD-S0042", "DEPOSITION_EFFICIENCY must be within (0, 1]",
                      connection.location);
        }
        double gap = 0.0;
        double alignment = 0.0;
        const auto first_point = point_values.find(first->second->point);
        const auto second_point = point_values.find(second->second->point);
        const auto first_axis = vector_values.find(first->second->axis);
        const auto second_axis = vector_values.find(second->second->axis);
        if (first_point != point_values.end() && second_point != point_values.end()) {
            gap = std::hypot(first_point->second[0] - second_point->second[0],
                             first_point->second[1] - second_point->second[1],
                             first_point->second[2] - second_point->second[2]);
        }
        if (first_axis != vector_values.end() && second_axis != vector_values.end()) {
            alignment = std::abs(first_axis->second[0] * second_axis->second[0] +
                                 first_axis->second[1] * second_axis->second[1] +
                                 first_axis->second[2] * second_axis->second[2]);
        }
        const double angular_cosine =
            std::cos(lowered.tolerance.angular_degrees * std::numbers::pi / 180.0);
        const bool aligned = gap <= clearance + lowered.tolerance.linear_mm &&
                             alignment + 1e-12 >= angular_cosine;
        lowered.connections.push_back(ir::AssemblyConnection{
            connection.name, connection.first_interface, connection.second_interface,
            connection.method, connection.standard, connection.fastener, connection.fit,
            connection.filler, connection.weld_process, clearance, weld_size, weld_length,
            filler_diameter, stock_length, connection.deposition_efficiency, gap, alignment,
            connection.quantity, connection.quantity_explicit, connection.automatic, aligned});
    }

    for (const auto& constraint : program.constraints) {
        const bool distance = constraint.kind == "MIN_DISTANCE";
        const bool coincident = constraint.kind == "COINCIDENT";
        const bool directional = constraint.kind == "PARALLEL" ||
                                 constraint.kind == "PERPENDICULAR" ||
                                 constraint.kind == "ANGLE_BETWEEN";
        if (!distance && !coincident && !directional) {
            add_error(result, "ICAD-S0026", "unsupported constraint kind '" + constraint.kind + "'",
                      constraint.location);
        }
        if (distance && (!body_names.contains(constraint.first_body) ||
                         !body_names.contains(constraint.second_body) ||
                         constraint.first_body == constraint.second_body)) {
            add_error(result, "ICAD-S0027",
                      "constraint must reference two different existing bodies",
                      constraint.location);
        }
        if (coincident && (!point_values.contains(constraint.first_body) ||
                           !point_values.contains(constraint.second_body))) {
            add_error(result, "ICAD-S0027", "COINCIDENT requires two POINT3 references",
                      constraint.location);
        }
        if (directional && (!vector_values.contains(constraint.first_body) ||
                            !vector_values.contains(constraint.second_body))) {
            add_error(result, "ICAD-S0027", "direction constraint requires two VECTOR references",
                      constraint.location);
        }
        ir::Quantity target;
        if (distance || coincident) {
            target = lower_value(constraint.target, units::Dimension::length, parameter_values,
                                 result);
        } else if (constraint.kind == "ANGLE_BETWEEN") {
            target =
                lower_value(constraint.target, units::Dimension::angle, parameter_values, result);
        } else {
            target = ir::Quantity{0.0, "deg", units::Dimension::angle};
        }
        if (target.value < 0.0 || (constraint.kind == "ANGLE_BETWEEN" && target.value > 180.0)) {
            add_error(result, "ICAD-S0028", "minimum distance cannot be negative",
                      constraint.location);
        }
        lowered.constraints.push_back(ir::Constraint{constraint.name, constraint.kind,
                                                     constraint.first_body, constraint.second_body,
                                                     target.value, target.value, target.unit,
                                                     constraint.target.parameter_reference});
    }

    constexpr std::string_view face_selectors[]{"X_MIN", "X_MAX", "Y_MIN", "Y_MAX",
                                                 "Z_MIN", "Z_MAX"};
    constexpr std::string_view edge_selectors[]{
        "X_AT_Y_MIN_Z_MIN", "X_AT_Y_MIN_Z_MAX", "X_AT_Y_MAX_Z_MIN",
        "X_AT_Y_MAX_Z_MAX", "Y_AT_X_MIN_Z_MIN", "Y_AT_X_MIN_Z_MAX",
        "Y_AT_X_MAX_Z_MIN", "Y_AT_X_MAX_Z_MAX", "Z_AT_X_MIN_Y_MIN",
        "Z_AT_X_MIN_Y_MAX", "Z_AT_X_MAX_Y_MIN", "Z_AT_X_MAX_Y_MAX"};
    std::unordered_set<std::string> mate_names;
    for (const auto& mate : program.mates) {
        if (!mate_names.insert(mate.name).second) {
            add_error(result, "ICAD-S0034", "duplicate MATE name '" + mate.name + "'",
                      mate.location);
        }
        if (!occurrence_names.contains(mate.first_occurrence) ||
            !occurrence_names.contains(mate.second_occurrence) ||
            mate.first_occurrence == mate.second_occurrence) {
            add_error(result, "ICAD-S0034",
                      "MATE must reference two different existing body or instance occurrences",
                      mate.location);
        }
        const bool face = mate.kind == "FACE";
        const bool edge = mate.kind == "EDGE";
        const auto selector_valid = [&](const std::string& selector) {
            return face ? std::ranges::contains(face_selectors, selector)
                        : std::ranges::contains(edge_selectors, selector);
        };
        if ((!face && !edge) || !selector_valid(mate.first_selector) ||
            !selector_valid(mate.second_selector)) {
            add_error(result, "ICAD-S0034",
                      "MATE requires a supported world-semantic FACE or EDGE selector",
                      mate.location);
        } else if (mate.first_selector.front() != mate.second_selector.front()) {
            add_error(result, "ICAD-S0034", "mated selectors must use the same world axis",
                      mate.location);
        }
        const auto target =
            lower_value(mate.target, units::Dimension::length, parameter_values, result);
        if (target.value < 0.0) {
            add_error(result, "ICAD-S0034", "MATE target cannot be negative", mate.location);
        }
        lowered.mates.push_back(
            ir::Mate{mate.name, face ? ir::MateKind::face : ir::MateKind::edge,
                     mate.first_occurrence, mate.first_selector, mate.second_occurrence,
                     mate.second_selector, target.value, mate.target.parameter_reference});
    }

    constexpr std::string_view backgrounds[]{"SKY_DAY", "STUDIO", "NIGHT", "TRANSPARENT"};
    for (const auto& scene : program.scenes) {
        ir::Scene lowered_scene;
        lowered_scene.name = scene.name;
        lowered_scene.background = scene.background;
        lowered_scene.duration_seconds =
            lower_quantity(scene.duration, units::Dimension::time, true, result).value;
        lowered_scene.frames_per_second = scene.frames_per_second;
        lowered_scene.loop_count = scene.loop_count;
        if (scene.frames_per_second <= 0.0 || scene.frames_per_second > 240.0) {
            add_error(result, "ICAD-S0013", "scene FPS must be greater than 0 and at most 240",
                      scene.location);
        }
        if (std::ranges::find(backgrounds, scene.background) == std::end(backgrounds)) {
            add_error(result, "ICAD-S0014", "unknown scene background '" + scene.background + "'",
                      scene.location);
        }
        for (const auto& light : scene.lights) {
            if (light.kind != "DIRECTIONAL" && light.kind != "POINT") {
                add_error(result, "ICAD-S0014", "LIGHT kind must be DIRECTIONAL or POINT",
                          light.location);
            }
            if (light.intensity < 0.0 || std::ranges::any_of(
                    light.color, [](double value) { return value < 0.0 || value > 1.0; })) {
                add_error(result, "ICAD-S0014",
                          "LIGHT intensity must be non-negative and color within [0, 1]",
                          light.location);
            }
            ir::SceneLight lowered_light;
            lowered_light.name = light.name;
            lowered_light.kind = light.kind;
            lowered_light.color = light.color;
            lowered_light.intensity = light.intensity;
            if (light.kind == "POINT") {
                const auto point = std::ranges::find(lowered.points, light.point,
                                                     &ir::SpatialPoint::name);
                if (light.point.empty() || point == lowered.points.end()) {
                    add_error(result, "ICAD-S0014", "POINT LIGHT requires AT with a named POINT3",
                              light.location);
                } else {
                    lowered_light.position_mm = point->position_mm;
                }
            } else if (!light.point.empty()) {
                add_error(result, "ICAD-S0014", "DIRECTIONAL LIGHT does not accept AT",
                          light.location);
            }
            lowered_scene.lights.push_back(std::move(lowered_light));
        }
        for (const auto& event : scene.events) {
            const auto time = lower_quantity(event.time, units::Dimension::time, false, result).value;
            if (time < 0.0 || time > lowered_scene.duration_seconds) {
                add_error(result, "ICAD-S0014", "EVENT time must be within scene duration",
                          event.location);
            }
            lowered_scene.events.push_back(ir::SceneEvent{time, event.name});
        }
        for (const auto& track : scene.tracks) {
            ir::AnimationTrack lowered_track;
            lowered_track.name = track.name;
            lowered_track.target_kind = track.target_kind;
            lowered_track.target = track.target;
            lowered_track.easing = track.easing;
            constexpr std::string_view easing_modes[]{"LINEAR", "EASE_IN", "EASE_OUT",
                                                       "EASE_IN_OUT", "STEP"};
            if (std::ranges::find(easing_modes, track.easing) == std::end(easing_modes)) {
                add_error(result, "ICAD-S0015",
                          "EASING must be LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT, or STEP",
                          track.location);
            }
            if (track.target_kind != "BODY" && track.target_kind != "CAMERA" &&
                track.target_kind != "JOINT" && track.target_kind != "VISIBILITY") {
                add_error(result, "ICAD-S0015",
                          "track target kind must be BODY, CAMERA, JOINT, or VISIBILITY",
                          track.location);
            } else if ((track.target_kind == "BODY" || track.target_kind == "VISIBILITY") &&
                       !occurrence_names.contains(track.target)) {
                add_error(result, "ICAD-S0016",
                          "track references unknown occurrence '" + track.target + "'", track.location);
            } else if (track.target_kind == "JOINT" && !joint_names.contains(track.target)) {
                add_error(result, "ICAD-S0016",
                          "track references unknown JOINT '" + track.target + "'", track.location);
            }
            if (track.keyframes.size() < 2) {
                add_error(result, "ICAD-S0017", "animation track requires at least two keyframes",
                          track.location);
            }
            double previous_time = -1.0;
            const auto target_joint = std::ranges::find(lowered.joints, track.target,
                                                        &ir::Joint::name);
            for (const auto& frame : track.keyframes) {
                const double time =
                    lower_quantity(frame.time, units::Dimension::time, false, result).value;
                if (time < 0.0 || time <= previous_time || time > lowered_scene.duration_seconds) {
                    add_error(result, "ICAD-S0018",
                              "keyframe times must increase within the scene duration",
                              frame.location);
                }
                previous_time = time;
                if (track.target_kind == "VISIBILITY") {
                    if (!frame.visibility_only) {
                        add_error(result, "ICAD-S0019",
                                  "VISIBILITY tracks require KEYFRAME TIME VISIBLE ON|OFF",
                                  frame.location);
                        continue;
                    }
                    ir::Keyframe lowered_frame;
                    lowered_frame.time_seconds = time;
                    lowered_frame.visible = frame.visible;
                    lowered_track.keyframes.push_back(std::move(lowered_frame));
                    continue;
                }
                if (track.target_kind == "JOINT") {
                    if (!frame.value_only) {
                        add_error(result, "ICAD-S0019",
                                  "JOINT tracks require KEYFRAME TIME VALUE scalar",
                                  frame.location);
                        continue;
                    }
                    const auto dimension =
                        target_joint != lowered.joints.end() &&
                                target_joint->kind == ir::JointKind::prismatic
                            ? units::Dimension::length
                            : units::Dimension::angle;
                    const auto value = lower_value(frame.joint_value, dimension, parameter_values,
                                                   result);
                    if (target_joint != lowered.joints.end()) {
                        if (target_joint->kind == ir::JointKind::fixed) {
                            add_error(result, "ICAD-S0019", "FIXED JOINT cannot be animated",
                                      frame.location);
                        } else if (value.value < target_joint->lower_limit - 1e-9 ||
                                   value.value > target_joint->upper_limit + 1e-9) {
                            add_error(result, "ICAD-S0019",
                                      "joint keyframe VALUE must remain within JOINT LIMIT",
                                      frame.location);
                        }
                    }
                    ir::Keyframe lowered_frame;
                    lowered_frame.time_seconds = time;
                    lowered_frame.joint_value = value.value;
                    lowered_frame.joint_unit = value.unit;
                    lowered_track.keyframes.push_back(std::move(lowered_frame));
                    continue;
                }
                if (frame.value_only || frame.visibility_only) {
                    add_error(result, "ICAD-S0019",
                              "BODY and CAMERA tracks require POSITION and ROTATION keyframes",
                              frame.location);
                    continue;
                }
                const auto px =
                    lower_quantity(frame.position_x, units::Dimension::length, false, result);
                const auto py =
                    lower_quantity(frame.position_y, units::Dimension::length, false, result);
                const auto pz =
                    lower_quantity(frame.position_z, units::Dimension::length, false, result);
                const auto rx =
                    lower_quantity(frame.rotation_x, units::Dimension::angle, false, result);
                const auto ry =
                    lower_quantity(frame.rotation_y, units::Dimension::angle, false, result);
                const auto rz =
                    lower_quantity(frame.rotation_z, units::Dimension::angle, false, result);
                ir::Keyframe lowered_frame;
                lowered_frame.time_seconds = time;
                lowered_frame.transform =
                    {{px.value, py.value, pz.value}, {rx.value, ry.value, rz.value}};
                lowered_track.keyframes.push_back(std::move(lowered_frame));
            }
            lowered_scene.tracks.push_back(std::move(lowered_track));
        }
        lowered.scenes.push_back(std::move(lowered_scene));
    }

    if (result.diagnostics.empty()) {
        result.project = std::move(lowered);
    }
    return result;
}

} // namespace icad::compiler
