#include "icad/compiler/parser/parser.hpp"

#include "icad/compiler/expression.hpp"
#include "icad/compiler/language.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <locale>
#include <ranges>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace icad::compiler {
namespace {

using Line = std::vector<Token>;
using Lines = std::vector<Line>;

auto add_error(ParseResult& result, std::string code, std::string message, SourceLocation location)
    -> void {
    result.diagnostics.push_back(
        Diagnostic{DiagnosticSeverity::error, std::move(code), std::move(message), location});
}

[[nodiscard]] auto lines_from(const std::vector<Token>& tokens) -> Lines {
    Lines lines;
    Line current;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::end_of_file) {
            break;
        }
        if (token.kind == TokenKind::comment) {
            continue;
        }
        if (token.kind == TokenKind::newline) {
            if (!current.empty()) {
                lines.push_back(std::move(current));
                current = {};
            }
            continue;
        }
        current.push_back(token);
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }
    return lines;
}

[[nodiscard]] auto keyword(const Line& line) -> std::string_view {
    return line.empty() ? std::string_view{} : std::string_view{line.front().lexeme};
}

[[nodiscard]] auto valid_identifier(const Token& token) -> bool {
    return token.kind == TokenKind::identifier;
}

[[nodiscard]] auto parse_qualified_name(const Line& line, std::size_t& cursor,
                                        ParseResult& result) -> std::string {
    if (cursor >= line.size() || !valid_identifier(line[cursor])) {
        add_error(result, "ICAD-P0026", "expected an identifier or qualified name",
                  cursor < line.size() ? line[cursor].location : line.front().location);
        return {};
    }
    std::string name = line[cursor++].lexeme;
    while (cursor < line.size() && line[cursor].kind == TokenKind::dot) {
        if (cursor + 1 >= line.size() || !valid_identifier(line[cursor + 1])) {
            add_error(result, "ICAD-P0026", "qualified name expects an identifier after '.'",
                      line[cursor].location);
            ++cursor;
            return name;
        }
        name.push_back('.');
        name += line[cursor + 1].lexeme;
        cursor += 2;
    }
    return name;
}

[[nodiscard]] auto split_persistent_face_path(std::string_view path, std::string& feature,
                                              std::string& role) -> bool {
    const auto first = path.find('.');
    const auto second = first == std::string_view::npos ? std::string_view::npos
                                                        : path.find('.', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        path.find('.', second + 1) != std::string_view::npos ||
        path.substr(first + 1, second - first - 1) != "face") {
        return false;
    }
    const auto parsed_role = path.substr(second + 1);
    if (parsed_role != "top" && parsed_role != "bottom") {
        return false;
    }
    feature = std::string{path.substr(0, first)};
    role = std::string{parsed_role};
    return !feature.empty();
}

[[nodiscard]] auto face_role_selector(std::string_view role) -> std::string {
    return role == "top" ? "Z_MAX" : "Z_MIN";
}

[[nodiscard]] auto parse_number(const Token& token, double& value) -> bool {
    if (token.kind != TokenKind::number) {
        return false;
    }
    std::istringstream stream{token.lexeme};
    stream.imbue(std::locale::classic());
    if (!(stream >> value))
        return false;
    char trailing{};
    return !(stream >> trailing);
}

[[nodiscard]] auto parse_quantity(const Line& line, std::size_t value_index, ParseResult& result)
    -> ast::QuantityLiteral {
    ast::QuantityLiteral quantity;
    if (value_index + 1 >= line.size()) {
        add_error(result, "ICAD-P0002", "expected NUMBER UNIT quantity", line.front().location);
        return quantity;
    }
    quantity.location = line[value_index].location;
    if (!parse_number(line[value_index], quantity.value)) {
        add_error(result, "ICAD-P0002", "expected a numeric quantity", line[value_index].location);
    }
    if (!valid_identifier(line[value_index + 1])) {
        add_error(result, "ICAD-P0003", "expected a unit after the quantity",
                  line[value_index + 1].location);
    } else {
        quantity.unit = line[value_index + 1].lexeme;
    }
    return quantity;
}

[[nodiscard]] auto parse_expression(const Line& line, std::size_t begin, std::size_t end,
                                    ParseResult& result) -> ast::ScalarExpression {
    if (begin > end || end > line.size()) {
        add_error(result, "ICAD-E0001", "invalid scalar expression token range",
                  line.front().location);
        return {};
    }
    auto parsed = parse_scalar_expression(
        std::span<const Token>{line.data() + begin, end - begin});
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(parsed.diagnostics.begin()),
                              std::make_move_iterator(parsed.diagnostics.end()));
    return std::move(parsed.expression);
}

auto retain_legacy_value(const ast::ScalarExpression& expression, ast::QuantityLiteral& literal,
                         std::string& reference) -> void {
    if (expression.postfix.size() != 1) {
        return;
    }
    const auto& node = expression.postfix.front();
    if (node.operation == ast::ScalarExpressionOp::literal && !node.unit.empty()) {
        literal = ast::QuantityLiteral{node.literal, node.unit, node.location};
    } else if (node.operation == ast::ScalarExpressionOp::reference) {
        reference = node.symbol;
    }
}

[[nodiscard]] auto parse_value(const Line& line, std::size_t& index, ParseResult& result)
    -> ast::ValueDecl {
    ast::ValueDecl value;
    if (index >= line.size()) {
        add_error(result, "ICAD-P0020", "expected a quantity or parameter reference",
                  line.front().location);
        return value;
    }
    value.location = line[index].location;
    if (line[index].kind == TokenKind::number) {
        const std::size_t begin = index;
        value.literal = parse_quantity(line, index, result);
        index += std::min<std::size_t>(2, line.size() - index);
        value.expression = parse_expression(line, begin, index, result);
        return value;
    }
    if (valid_identifier(line[index])) {
        const std::size_t begin = index;
        value.parameter_reference = line[index++].lexeme;
        while (index < line.size() && line[index].kind == TokenKind::dot) {
            if (index + 1 >= line.size() || !valid_identifier(line[index + 1])) {
                add_error(result, "ICAD-E0001",
                          "qualified name expects an identifier after '.'", line[index].location);
                ++index;
                break;
            }
            value.parameter_reference.push_back('.');
            value.parameter_reference += line[index + 1].lexeme;
            index += 2;
        }
        value.expression = parse_expression(line, begin, index, result);
        return value;
    }
    add_error(result, "ICAD-P0020", "expected a quantity or parameter reference",
              line[index].location);
    ++index;
    return value;
}

[[nodiscard]] auto parse_feature(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::FeatureDecl {
    const Line& declaration = lines[index];
    ast::FeatureDecl feature;
    feature.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0001", "FEATURE expects exactly one identifier",
                  declaration.front().location);
    } else {
        feature.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0001", "END does not accept arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "TYPE") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "TYPE expects exactly one identifier",
                          line.front().location);
            } else if (!feature.type.empty()) {
                add_error(result, "ICAD-P0004", "feature TYPE is declared more than once",
                          line.front().location);
            } else {
                feature.type = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "PROFILE") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !feature.profile.empty()) {
                add_error(result, "ICAD-P0017",
                          "feature PROFILE expects one profile and may appear once",
                          line.front().location);
            } else {
                feature.profile = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "TARGET_PROFILE") {
            if (line.size() != 2 || !valid_identifier(line[1]) ||
                !feature.target_profile.empty()) {
                add_error(result, "ICAD-P0025",
                          "TARGET_PROFILE expects one profile and may appear once",
                          line.front().location);
            } else {
                feature.target_profile = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "PATH") {
            const bool valid = line.size() >= 3 && feature.path_points.empty() &&
                               std::ranges::all_of(line | std::views::drop(1), valid_identifier);
            if (!valid) {
                add_error(result, "ICAD-P0025",
                          "PATH expects at least two POINT3 identifiers and may appear once",
                          line.front().location);
            } else {
                for (std::size_t point = 1; point < line.size(); ++point)
                    feature.path_points.push_back(line[point].lexeme);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "OPERATION") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !feature.operation.empty()) {
                add_error(result, "ICAD-P0023",
                          "feature OPERATION expects NEW, UNION, CUT, or INTERSECT and may appear "
                          "once",
                          line.front().location);
            } else {
                feature.operation = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "SELECT") {
            const bool nearest = line.size() == 4 && line[1].lexeme == "EDGE" &&
                                 line[2].lexeme == "NEAREST" &&
                                 valid_identifier(line[3]);
            const bool semantic_loop = line.size() == 4 && line[1].lexeme == "EDGE" &&
                                       (line[2].lexeme == "TOP" ||
                                        line[2].lexeme == "BOTTOM") &&
                                       (line[3].lexeme == "INNER" ||
                                        line[3].lexeme == "OUTER");
            const bool named_set = line.size() == 3 && line[1].lexeme == "EDGESET" &&
                                   valid_identifier(line[2]);
            if ((!nearest && !semantic_loop && !named_set) ||
                !feature.selected_edge_point.empty() ||
                !feature.selected_edge_location.empty() || !feature.selected_edge_set.empty()) {
                add_error(result, "ICAD-P0024",
                          "SELECT expects EDGE NEAREST point, EDGE TOP|BOTTOM INNER|OUTER, or EDGESET name and may appear once",
                          line.front().location);
            } else if (nearest) {
                feature.selected_edge_point = line[3].lexeme;
            } else if (semantic_loop) {
                feature.selected_edge_location = line[2].lexeme;
                feature.selected_edge_classification = line[3].lexeme;
            } else {
                feature.selected_edge_set = line[2].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "DIRECTION") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !feature.direction.empty()) {
                add_error(result, "ICAD-P0024",
                          "DIRECTION expects one VECTOR and may appear once",
                          line.front().location);
            } else {
                feature.direction = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "PLANE") {
            if (line.size() != 4 || !valid_identifier(line[1]) || line[2].lexeme != "NORMAL" ||
                !valid_identifier(line[3]) || !feature.plane_point.empty()) {
                add_error(result, "ICAD-P0024",
                          "PLANE expects POINT NORMAL VECTOR and may appear once",
                          line.front().location);
            } else {
                feature.plane_point = line[1].lexeme;
                feature.plane_normal = line[3].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "COUNT") {
            double count = 0.0;
            if (line.size() != 2 || feature.has_count || !parse_number(line[1], count) ||
                count < 0.0 || count > 1'000'000.0 || std::floor(count) != count) {
                add_error(result, "ICAD-P0024",
                          "COUNT expects one non-negative integer and may appear once",
                          line.front().location);
            } else {
                feature.count = static_cast<std::size_t>(count);
                feature.has_count = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "FEATURE" || keyword(line) == "BODY") {
            add_error(result, "ICAD-P0005", "feature block is missing END", feature.location);
            break;
        }
        if (!valid_identifier(line[0]) || line.size() < 2) {
            add_error(result, "ICAD-P0001",
                      "feature property expects NAME SCALAR_EXPRESSION",
                      line.front().location);
            ++index;
            continue;
        }
        auto expression = parse_expression(line, 1, line.size(), result);
        ast::PropertyDecl property;
        property.name = line[0].lexeme;
        property.location = line.front().location;
        property.expression = std::move(expression);
        retain_legacy_value(property.expression, property.value, property.parameter_reference);
        feature.properties.push_back(std::move(property));
        ++index;
    }

    if (!closed && index >= lines.size()) {
        add_error(result, "ICAD-P0005", "feature block is missing END", feature.location);
    }
    return feature;
}

[[nodiscard]] auto parse_profile(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::ProfileDecl {
    const Line& declaration = lines[index];
    ast::ProfileDecl profile;
    profile.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0018", "PROFILE expects exactly one identifier",
                  declaration.front().location);
    } else {
        profile.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    const auto select_mode = [&](ast::ProfileMode mode, const Line& line) {
        if (profile.mode == ast::ProfileMode::unset) {
            profile.mode = mode;
            return true;
        }
        if (profile.mode != mode) {
            add_error(result, "ICAD-P0018", "PROFILE cannot mix POINT, path, and CIRCLE syntax",
                      line.front().location);
            return false;
        }
        return true;
    };
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0018", "END does not accept profile arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "POINT") {
            if (line.size() != 5 || !select_mode(ast::ProfileMode::points, line)) {
                add_error(result, "ICAD-P0018", "POINT expects X UNIT Y UNIT",
                          line.front().location);
                ++index;
                continue;
            }
            profile.points.push_back(ast::Point2Decl{parse_quantity(line, 1, result),
                                                     parse_quantity(line, 3, result),
                                                     line.front().location});
            ++index;
            continue;
        }
        if (keyword(line) == "START") {
            if (line.size() != 5 || profile.mode != ast::ProfileMode::unset) {
                add_error(result, "ICAD-P0018",
                          "START expects X UNIT Y UNIT and must begin a path profile",
                          line.front().location);
            } else {
                profile.mode = ast::ProfileMode::path;
                profile.path_start =
                    ast::Point2Decl{parse_quantity(line, 1, result),
                                    parse_quantity(line, 3, result), line.front().location};
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LINE") {
            if (line.size() != 5 || profile.mode != ast::ProfileMode::path || profile.path_closed) {
                add_error(result, "ICAD-P0018",
                          "LINE expects X UNIT Y UNIT inside an open path profile",
                          line.front().location);
            } else {
                profile.path_segments.push_back(
                    ast::PathSegmentDecl{ast::PathSegmentKind::line,
                                         {parse_quantity(line, 1, result),
                                          parse_quantity(line, 3, result), line.front().location},
                                         {},
                                         true,
                                         line.front().location});
            }
            ++index;
            continue;
        }
        if (keyword(line) == "ARC") {
            const bool layout = line.size() == 11 && line[5].lexeme == "CENTER" &&
                                (line[10].lexeme == "CW" || line[10].lexeme == "CCW");
            if (!layout || profile.mode != ast::ProfileMode::path || profile.path_closed) {
                add_error(
                    result, "ICAD-P0018",
                    "ARC expects X UNIT Y UNIT CENTER X UNIT Y UNIT CW|CCW inside an open path",
                    line.front().location);
            } else {
                profile.path_segments.push_back(
                    ast::PathSegmentDecl{ast::PathSegmentKind::circular_arc,
                                         {parse_quantity(line, 1, result),
                                          parse_quantity(line, 3, result), line.front().location},
                                         {parse_quantity(line, 6, result),
                                          parse_quantity(line, 8, result), line.front().location},
                                         line[10].lexeme == "CCW",
                                         line.front().location});
            }
            ++index;
            continue;
        }
        if (keyword(line) == "CLOSE") {
            if (line.size() != 1 || profile.mode != ast::ProfileMode::path || profile.path_closed) {
                add_error(result, "ICAD-P0018", "CLOSE must end one open path profile",
                          line.front().location);
            } else {
                profile.path_closed = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "CIRCLE") {
            if (line.size() != 7 || !select_mode(ast::ProfileMode::circle, line) ||
                profile.circle_radius.unit.size() != 0) {
                add_error(result, "ICAD-P0018",
                          "CIRCLE expects CENTER_X UNIT CENTER_Y UNIT RADIUS UNIT and must be the "
                          "only profile statement",
                          line.front().location);
            } else {
                profile.circle_center =
                    ast::Point2Decl{parse_quantity(line, 1, result),
                                    parse_quantity(line, 3, result), line.front().location};
                profile.circle_radius = parse_quantity(line, 5, result);
            }
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0018",
                  "PROFILE accepts POINT, START, LINE, ARC, CLOSE, or CIRCLE statements",
                  line.front().location);
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0018", "profile block is missing END", profile.location);
    }
    if (profile.mode == ast::ProfileMode::path && !profile.path_closed) {
        add_error(result, "ICAD-P0018", "path profile requires CLOSE before END", profile.location);
    }
    return profile;
}

[[nodiscard]] auto parse_sketch_shape(const Lines& lines, std::size_t& index,
                                      ParseResult& result) -> ast::SketchShapeDecl {
    const Line& declaration = lines[index];
    ast::SketchShapeDecl shape;
    shape.location = declaration.front().location;
    constexpr std::string_view closures[]{"OPEN", "CLOSED"};
    constexpr std::string_view roles[]{"STOCK", "ADDITIVE", "HOLE", "CONSTRUCTION"};
    const bool valid = declaration.size() == 5 && valid_identifier(declaration[1]) &&
                       std::ranges::find(closures, declaration[2].lexeme) != std::end(closures) &&
                       declaration[3].lexeme == "ROLE" &&
                       std::ranges::find(roles, declaration[4].lexeme) != std::end(roles);
    if (!valid) {
        add_error(result, "ICAD-P0028",
                  "SHAPE expects NAME OPEN|CLOSED ROLE STOCK|ADDITIVE|HOLE|CONSTRUCTION",
                  declaration.front().location);
    } else {
        shape.name = declaration[1].lexeme;
        shape.closure = declaration[2].lexeme;
        shape.role = declaration[4].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0028", "END does not accept SHAPE arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "POINT") {
            if (line.size() < 4 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0028", "SHAPE POINT expects NAME X Y [FIXED]",
                          line.front().location);
                ++index;
                continue;
            }
            std::size_t cursor = 2;
            ast::SketchPointDecl point;
            point.name = line[1].lexeme;
            point.x = parse_value(line, cursor, result);
            point.y = parse_value(line, cursor, result);
            point.location = line.front().location;
            if (cursor < line.size() && line[cursor].lexeme == "FIXED") {
                point.fixed = true;
                ++cursor;
            }
            if (cursor != line.size()) {
                add_error(result, "ICAD-P0028", "SHAPE POINT has unexpected trailing values",
                          line[cursor].location);
            }
            if (std::ranges::any_of(shape.points, [&](const auto& existing) {
                    return existing.name == point.name;
                })) {
                add_error(result, "ICAD-P0028", "duplicate SHAPE point name '" + point.name +
                                                     "'",
                          point.location);
            }
            shape.points.push_back(std::move(point));
            ++index;
            continue;
        }
        if (keyword(line) == "LINE" || keyword(line) == "ARC") {
            const bool arc = keyword(line) == "ARC";
            const bool line_shape = !arc && line.size() == 6 && valid_identifier(line[1]) &&
                                    line[2].lexeme == "FROM" && valid_identifier(line[3]) &&
                                    line[4].lexeme == "TO" && valid_identifier(line[5]);
            const bool arc_shape = arc && line.size() == 9 && valid_identifier(line[1]) &&
                                   line[2].lexeme == "FROM" && valid_identifier(line[3]) &&
                                   line[4].lexeme == "TO" && valid_identifier(line[5]) &&
                                   line[6].lexeme == "CENTER" && valid_identifier(line[7]) &&
                                   (line[8].lexeme == "CW" || line[8].lexeme == "CCW");
            if (!line_shape && !arc_shape) {
                add_error(result, "ICAD-P0028",
                          arc ? "SHAPE ARC expects NAME FROM POINT TO POINT CENTER POINT CW|CCW"
                              : "SHAPE LINE expects NAME FROM POINT TO POINT",
                          line.front().location);
                ++index;
                continue;
            }
            ast::SketchEntityDecl entity;
            entity.name = line[1].lexeme;
            entity.kind = arc ? ast::SketchEntityKind::circular_arc
                              : ast::SketchEntityKind::line;
            entity.start = line[3].lexeme;
            entity.end = line[5].lexeme;
            if (arc) {
                entity.center = line[7].lexeme;
                entity.counterclockwise = line[8].lexeme == "CCW";
            }
            entity.location = line.front().location;
            const auto declared_point = [&](const std::string& name) {
                return std::ranges::any_of(
                    shape.points, [&](const auto& point) { return point.name == name; });
            };
            if (!declared_point(entity.start) || !declared_point(entity.end) ||
                (arc && !declared_point(entity.center))) {
                add_error(result, "ICAD-P0028",
                          "SHAPE entities must reference points declared earlier in the SHAPE",
                          entity.location);
            }
            if (entity.start == entity.end) {
                add_error(result, "ICAD-P0028", "SHAPE entity endpoints must be different",
                          entity.location);
            }
            if (std::ranges::any_of(shape.entities, [&](const auto& existing) {
                    return existing.name == entity.name;
                })) {
                add_error(result, "ICAD-P0028", "duplicate SHAPE entity name '" + entity.name +
                                                     "'",
                          entity.location);
            }
            shape.entities.push_back(std::move(entity));
            ++index;
            continue;
        }
        if (keyword(line) == "CIRCLE") {
            const bool prefix = line.size() >= 6 && valid_identifier(line[1]) &&
                                line[2].lexeme == "CENTER" && valid_identifier(line[3]) &&
                                line[4].lexeme == "RADIUS";
            if (!prefix) {
                add_error(result, "ICAD-P0028",
                          "SHAPE CIRCLE expects NAME CENTER POINT RADIUS VALUE",
                          line.front().location);
                ++index;
                continue;
            }
            ast::SketchEntityDecl entity;
            entity.name = line[1].lexeme;
            entity.kind = ast::SketchEntityKind::circle;
            entity.center = line[3].lexeme;
            entity.location = line.front().location;
            std::size_t cursor = 5;
            entity.radius = parse_value(line, cursor, result);
            if (cursor != line.size()) {
                add_error(result, "ICAD-P0028", "SHAPE CIRCLE has unexpected trailing values",
                          line[cursor].location);
            }
            if (std::ranges::none_of(shape.points, [&](const auto& point) {
                    return point.name == entity.center;
                })) {
                add_error(result, "ICAD-P0028",
                          "SHAPE CIRCLE center must reference a point declared earlier",
                          entity.location);
            }
            if (!shape.entities.empty()) {
                add_error(result, "ICAD-P0028",
                          "a CIRCLE SHAPE cannot contain other boundary entities",
                          entity.location);
            }
            shape.entities.push_back(std::move(entity));
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0028",
                  "SHAPE accepts POINT, LINE, ARC, or CIRCLE statements",
                  line.front().location);
        ++index;
    }
    if (!closed)
        add_error(result, "ICAD-P0028", "SHAPE block is missing END", shape.location);
    return shape;
}

[[nodiscard]] auto parse_sketch_region(const Lines& lines, std::size_t& index,
                                       ParseResult& result) -> ast::SketchRegionDecl {
    const Line& declaration = lines[index];
    ast::SketchRegionDecl region;
    region.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0029", "REGION expects exactly one name",
                  declaration.front().location);
    } else {
        region.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    bool holes_seen = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1)
                add_error(result, "ICAD-P0029", "END does not accept REGION arguments",
                          line.front().location);
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "OUTER") {
            if (line.size() != 2 || !valid_identifier(line[1]) ||
                !region.outer_shape.empty()) {
                add_error(result, "ICAD-P0029",
                          "REGION requires exactly one OUTER SHAPE name",
                          line.front().location);
            } else {
                region.outer_shape = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "HOLES") {
            if (line.size() < 2 || holes_seen) {
                add_error(result, "ICAD-P0029",
                          "REGION accepts one HOLES line with one or more SHAPE names",
                          line.front().location);
            } else {
                holes_seen = true;
                for (std::size_t hole = 1; hole < line.size(); ++hole) {
                    if (!valid_identifier(line[hole])) {
                        add_error(result, "ICAD-P0029", "HOLES accepts only SHAPE names",
                                  line[hole].location);
                        continue;
                    }
                    if (std::ranges::contains(region.hole_shapes, line[hole].lexeme)) {
                        add_error(result, "ICAD-P0029",
                                  "duplicate REGION hole SHAPE '" + line[hole].lexeme + "'",
                                  line[hole].location);
                    } else {
                        region.hole_shapes.push_back(line[hole].lexeme);
                    }
                }
            }
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0029", "REGION accepts OUTER and optional HOLES statements",
                  line.front().location);
        ++index;
    }
    if (!closed)
        add_error(result, "ICAD-P0029", "REGION block is missing END", region.location);
    if (region.outer_shape.empty())
        add_error(result, "ICAD-P0029", "REGION requires one OUTER SHAPE", region.location);
    if (std::ranges::contains(region.hole_shapes, region.outer_shape))
        add_error(result, "ICAD-P0029", "REGION OUTER cannot also be a HOLE", region.location);
    return region;
}

[[nodiscard]] auto parse_sketch(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::SketchDecl {
    const Line& declaration = lines[index];
    ast::SketchDecl sketch;
    sketch.location = declaration.front().location;
    constexpr std::string_view planes[]{"XY", "XZ", "YZ"};
    constexpr std::string_view faces[]{"X_MIN", "X_MAX", "Y_MIN", "Y_MAX", "Z_MIN", "Z_MAX"};
    const bool plain = declaration.size() == 2 && valid_identifier(declaration[1]);
    const bool on_plane = declaration.size() == 5 && valid_identifier(declaration[1]) &&
                          declaration[2].lexeme == "ON" && declaration[3].lexeme == "PLANE" &&
                          std::ranges::find(planes, declaration[4].lexeme) != std::end(planes);
    const bool on_legacy_face = declaration.size() == 6 && valid_identifier(declaration[1]) &&
                         declaration[2].lexeme == "ON" && declaration[3].lexeme == "FACE" &&
                         valid_identifier(declaration[4]) &&
                         std::ranges::find(faces, declaration[5].lexeme) != std::end(faces);
    const bool face_prefix = declaration.size() >= 5 && valid_identifier(declaration[1]) &&
                             declaration[2].lexeme == "ON" &&
                             declaration[3].lexeme == "FACE";
    bool on_persistent_face = false;
    bool on_face_alias = false;
    std::string persistent_path;
    std::string persistent_feature;
    std::string persistent_role;
    if (face_prefix && !on_legacy_face) {
        std::size_t cursor = 4;
        persistent_path = parse_qualified_name(declaration, cursor, result);
        if (cursor == declaration.size()) {
            on_persistent_face = split_persistent_face_path(
                persistent_path, persistent_feature, persistent_role);
            on_face_alias = !on_persistent_face && persistent_path.find('.') == std::string::npos;
        }
    }
    if (!plain && !on_plane && !on_legacy_face && !on_persistent_face && !on_face_alias) {
        add_error(result, "ICAD-P0026",
                  "SKETCH expects NAME, NAME ON PLANE XY|XZ|YZ, NAME ON FACE FEATURE SELECTOR, "
                  "or NAME ON FACE FEATURE.face.top|bottom",
                  declaration.front().location);
    } else {
        sketch.name = declaration[1].lexeme;
        if (on_plane)
            sketch.plane = declaration[4].lexeme;
        if (on_legacy_face) {
            sketch.support_feature = declaration[4].lexeme;
            sketch.support_face = declaration[5].lexeme;
        } else if (on_persistent_face) {
            sketch.support_feature = persistent_feature;
            sketch.support_face = face_role_selector(persistent_role);
            sketch.support_reference = persistent_path;
            sketch.support_topology_path = persistent_feature + "/face." + persistent_role;
        } else if (on_face_alias) {
            sketch.support_reference = persistent_path;
        }
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0026", "END does not accept sketch arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "SHAPE") {
            if (sketch.circle || !sketch.points.empty() || !sketch.entities.empty()) {
                add_error(result, "ICAD-P0028",
                          "SKETCH cannot mix legacy boundary syntax with SHAPE blocks",
                          line.front().location);
            }
            auto shape = parse_sketch_shape(lines, index, result);
            if (std::ranges::any_of(sketch.shapes, [&](const auto& existing) {
                    return existing.name == shape.name;
                })) {
                add_error(result, "ICAD-P0028", "duplicate SHAPE name '" + shape.name + "'",
                          shape.location);
            }
            sketch.shapes.push_back(std::move(shape));
            continue;
        }
        if (keyword(line) == "REGION") {
            if (sketch.shapes.empty() || sketch.circle || !sketch.points.empty() ||
                !sketch.entities.empty()) {
                add_error(result, "ICAD-P0029",
                          "REGION requires preceding SHAPE declarations in a multi-shape SKETCH",
                          line.front().location);
            }
            auto region = parse_sketch_region(lines, index, result);
            if (std::ranges::any_of(sketch.regions, [&](const auto& existing) {
                    return existing.name == region.name;
                })) {
                add_error(result, "ICAD-P0029", "duplicate REGION name '" + region.name + "'",
                          region.location);
            }
            const auto known_shape = [&](const std::string& name) {
                return std::ranges::any_of(sketch.shapes,
                                           [&](const auto& shape) { return shape.name == name; });
            };
            if (!region.outer_shape.empty() && !known_shape(region.outer_shape)) {
                add_error(result, "ICAD-P0029",
                          "REGION OUTER references unknown preceding SHAPE '" +
                              region.outer_shape + "'",
                          region.location);
            }
            for (const auto& hole : region.hole_shapes) {
                if (!known_shape(hole))
                    add_error(result, "ICAD-P0029",
                              "REGION HOLES references unknown preceding SHAPE '" + hole + "'",
                              region.location);
            }
            sketch.regions.push_back(std::move(region));
            continue;
        }
        if (keyword(line) == "SOLVE") {
            if (line.size() != 2 ||
                (line[1].lexeme != "FULL" && line[1].lexeme != "ALLOW_UNDER")) {
                add_error(result, "ICAD-P0028", "SOLVE expects FULL or ALLOW_UNDER",
                          line.front().location);
            } else if (!sketch.solve_requirement.empty()) {
                add_error(result, "ICAD-P0028", "SKETCH accepts only one SOLVE requirement",
                          line.front().location);
            } else {
                sketch.solve_requirement = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "POINT") {
            if (!sketch.shapes.empty()) {
                add_error(result, "ICAD-P0028",
                          "SKETCH cannot mix legacy POINT syntax with SHAPE blocks",
                          line.front().location);
            }
            if (sketch.circle) {
                add_error(result, "ICAD-P0026", "CIRCLE sketch cannot also contain POINT",
                          line.front().location);
            }
            if (line.size() < 4 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0026", "sketch POINT expects NAME X Y [FIXED]",
                          line.front().location);
                ++index;
                continue;
            }
            std::size_t cursor = 2;
            ast::SketchPointDecl point_decl;
            point_decl.name = line[1].lexeme;
            point_decl.x = parse_value(line, cursor, result);
            point_decl.y = parse_value(line, cursor, result);
            point_decl.location = line.front().location;
            if (cursor < line.size() && line[cursor].lexeme == "FIXED") {
                point_decl.fixed = true;
                ++cursor;
            }
            if (cursor != line.size()) {
                add_error(result, "ICAD-P0026", "sketch POINT has unexpected trailing values",
                          line[cursor].location);
            }
            sketch.points.push_back(std::move(point_decl));
            ++index;
            continue;
        }
        if (keyword(line) == "CIRCLE") {
            if (!sketch.shapes.empty()) {
                add_error(result, "ICAD-P0028",
                          "SKETCH cannot mix legacy CIRCLE syntax with SHAPE blocks",
                          line.front().location);
            }
            std::size_t cursor = 1;
            if (sketch.circle || !sketch.points.empty() || !sketch.entities.empty()) {
                add_error(result, "ICAD-P0026", "SKETCH accepts one CIRCLE or a point boundary",
                          line.front().location);
            } else {
                sketch.circle_center[0] = parse_value(line, cursor, result);
                sketch.circle_center[1] = parse_value(line, cursor, result);
                sketch.circle_radius = parse_value(line, cursor, result);
                sketch.circle = true;
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0026", "CIRCLE has unexpected trailing values",
                              line[cursor].location);
                }
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LINE" || keyword(line) == "ARC") {
            if (!sketch.shapes.empty()) {
                add_error(result, "ICAD-P0028",
                          "SKETCH cannot mix legacy entity syntax with SHAPE blocks",
                          line.front().location);
            }
            if (sketch.circle) {
                add_error(result, "ICAD-P0026", "CIRCLE sketch cannot also contain entities",
                          line.front().location);
                ++index;
                continue;
            }
            const bool arc = keyword(line) == "ARC";
            const bool line_shape = !arc && line.size() == 6 && valid_identifier(line[1]) &&
                                    line[2].lexeme == "FROM" && valid_identifier(line[3]) &&
                                    line[4].lexeme == "TO" && valid_identifier(line[5]);
            const bool arc_shape = arc && line.size() == 9 && valid_identifier(line[1]) &&
                                   line[2].lexeme == "FROM" && valid_identifier(line[3]) &&
                                   line[4].lexeme == "TO" && valid_identifier(line[5]) &&
                                   line[6].lexeme == "CENTER" && valid_identifier(line[7]) &&
                                   (line[8].lexeme == "CW" || line[8].lexeme == "CCW");
            if (!line_shape && !arc_shape) {
                add_error(result, "ICAD-P0026",
                          arc ? "sketch ARC expects NAME FROM POINT TO POINT CENTER POINT CW|CCW"
                              : "sketch LINE expects NAME FROM POINT TO POINT",
                          line.front().location);
                ++index;
                continue;
            }
            ast::SketchEntityDecl entity;
            entity.name = line[1].lexeme;
            entity.kind = arc ? ast::SketchEntityKind::circular_arc
                              : ast::SketchEntityKind::line;
            entity.start = line[3].lexeme;
            entity.end = line[5].lexeme;
            if (arc) {
                entity.center = line[7].lexeme;
                entity.counterclockwise = line[8].lexeme == "CCW";
            }
            entity.location = line.front().location;
            const auto declared_point = [&](const std::string& name) {
                return std::ranges::any_of(
                    sketch.points, [&](const auto& point) { return point.name == name; });
            };
            if (!declared_point(entity.start) || !declared_point(entity.end) ||
                (arc && !declared_point(entity.center))) {
                add_error(result, "ICAD-P0026",
                          "sketch entities must reference points declared earlier in the SKETCH",
                          line.front().location);
            }
            if (entity.start == entity.end) {
                add_error(result, "ICAD-P0026", "sketch entity endpoints must be different",
                          line.front().location);
            }
            if (std::ranges::any_of(sketch.entities, [&](const auto& existing) {
                    return existing.name == entity.name;
                })) {
                add_error(result, "ICAD-P0026", "duplicate sketch entity name '" + entity.name +
                                                     "'",
                          line.front().location);
            }
            sketch.entities.push_back(std::move(entity));
            ++index;
            continue;
        }
        if (keyword(line) == "CONSTRAINT") {
            if (sketch.circle) {
                add_error(result, "ICAD-P0026",
                          "CIRCLE sketch cannot also contain point constraints",
                          line.front().location);
                ++index;
                continue;
            }
            const bool prefix = line.size() >= 5 && valid_identifier(line[1]) &&
                                valid_identifier(line[2]);
            if (!prefix) {
                add_error(result, "ICAD-P0026",
                          "sketch CONSTRAINT expects NAME KIND and point references",
                          line.front().location);
                ++index;
                continue;
            }
            ast::SketchConstraintDecl constraint;
            constraint.name = line[1].lexeme;
            constraint.kind = line[2].lexeme;
            constraint.location = line.front().location;
            std::size_t cursor = 3;
            const bool symmetric = constraint.kind == "SYMMETRIC";
            const bool tangent = constraint.kind == "TANGENT";
            const std::size_t reference_count =
                constraint.kind == "ANGLE" || symmetric || tangent ? 3 : 2;
            for (std::size_t reference = 0; reference < reference_count; ++reference) {
                if ((symmetric || tangent) && reference == 2) {
                    const std::string_view separator = symmetric ? "ABOUT" : "AT";
                    if (cursor >= line.size() || line[cursor].lexeme != separator) {
                        add_error(result, "ICAD-P0026",
                                  std::string{symmetric ? "SYMMETRIC requires ABOUT before its axis"
                                                        : "TANGENT requires AT before its contact point"},
                                  line.front().location);
                        break;
                    }
                    ++cursor;
                }
                if (cursor >= line.size())
                    break;
                auto name = parse_qualified_name(line, cursor, result);
                if (!name.empty())
                    constraint.references.push_back(std::move(name));
            }
            if (constraint.references.size() != reference_count) {
                add_error(result, "ICAD-P0026", "sketch constraint has too few references",
                          line.front().location);
            }
            if (constraint.kind == "DISTANCE" || constraint.kind == "H_DISTANCE" ||
                constraint.kind == "V_DISTANCE" || constraint.kind == "ANGLE")
                constraint.target = parse_value(line, cursor, result);
            if (cursor != line.size()) {
                add_error(result, "ICAD-P0026", "sketch constraint has unexpected trailing values",
                          line[cursor].location);
            }
            sketch.constraints.push_back(std::move(constraint));
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0026",
                  "SKETCH accepts SHAPE, REGION, CIRCLE, POINT, LINE, ARC, CONSTRAINT, or SOLVE statements",
                  line.front().location);
        ++index;
    }
    if (!closed)
        add_error(result, "ICAD-P0026", "sketch block is missing END", sketch.location);
    return sketch;
}

[[nodiscard]] auto parse_topology_selection(const Lines& lines, std::size_t& index,
                                             ParseResult& result)
    -> ast::TopologySelectionDecl {
    const Line& declaration = lines[index];
    ast::TopologySelectionDecl selection;
    selection.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0031", "SELECTION expects exactly one identifier",
                  declaration.front().location);
    } else {
        selection.name = declaration[1].lexeme;
    }
    ++index;

    bool source_seen = false;
    bool query_seen = false;
    bool loop_seen = false;
    bool circular_seen = false;
    bool convexity_seen = false;
    bool adjacency_seen = false;
    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1)
                add_error(result, "ICAD-P0031", "END does not accept arguments",
                          line.front().location);
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "FROM") {
            if (source_seen || line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0031",
                          "SELECTION FROM expects one source feature and may appear once",
                          line.front().location);
            } else {
                selection.source_feature = line[1].lexeme;
                source_seen = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "EDGES") {
            if (query_seen || line.size() != 2 || line[1].lexeme != "WHERE") {
                add_error(result, "ICAD-P0031",
                          "SELECTION supports one EDGES WHERE query",
                          line.front().location);
            } else {
                query_seen = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LOOP") {
            if (loop_seen || line.size() != 1)
                add_error(result, "ICAD-P0031", "LOOP may appear once without arguments",
                          line.front().location);
            loop_seen = true;
            ++index;
            continue;
        }
        if (keyword(line) == "CIRCULAR") {
            if (circular_seen || line.size() != 1)
                add_error(result, "ICAD-P0031", "CIRCULAR may appear once without arguments",
                          line.front().location);
            circular_seen = true;
            ++index;
            continue;
        }
        if (keyword(line) == "CONCAVE" || keyword(line) == "CONVEX") {
            if (convexity_seen || line.size() != 1) {
                add_error(result, "ICAD-P0031",
                          "edge-loop query expects exactly one CONCAVE or CONVEX predicate",
                          line.front().location);
            } else {
                selection.convexity = std::string{keyword(line)};
                convexity_seen = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "ADJACENT_TO") {
            const bool valid = !adjacency_seen && line.size() == 3 &&
                               line[1].lexeme == "FACE" &&
                               (line[2].lexeme == "top" || line[2].lexeme == "bottom");
            if (!valid) {
                add_error(result, "ICAD-P0031",
                          "ADJACENT_TO expects FACE top|bottom and may appear once",
                          line.front().location);
            } else {
                selection.adjacent_face = line[2].lexeme == "top" ? "TOP" : "BOTTOM";
                adjacency_seen = true;
            }
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0031",
                  "SELECTION accepts FROM, EDGES WHERE, LOOP, CIRCULAR, CONCAVE|CONVEX, and ADJACENT_TO FACE top|bottom",
                  line.front().location);
        ++index;
    }
    if (!closed)
        add_error(result, "ICAD-P0031", "SELECTION block is missing END", selection.location);
    if (!source_seen || !query_seen || !loop_seen || !circular_seen || !convexity_seen ||
        !adjacency_seen) {
        add_error(result, "ICAD-P0031",
                  "SELECTION requires FROM, EDGES WHERE, LOOP, CIRCULAR, convexity, and face adjacency",
                  selection.location);
    }
    return selection;
}

[[nodiscard]] auto parse_body(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::BodyDecl {
    const Line& declaration = lines[index];
    ast::BodyDecl body;
    body.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0001", "BODY expects exactly one identifier",
                  declaration.front().location);
    } else {
        body.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0001", "END does not accept arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "MATERIAL") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !body.material.empty()) {
                add_error(result, "ICAD-P0013",
                          "BODY MATERIAL expects one material and may appear once",
                          line.front().location);
            } else {
                body.material = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "FEATURE") {
            body.features.push_back(parse_feature(lines, index, result));
            continue;
        }
        if (keyword(line) == "SELECTION") {
            auto selection = parse_topology_selection(lines, index, result);
            if (std::ranges::any_of(body.topology_selections, [&](const auto& existing) {
                    return existing.name == selection.name;
                })) {
                add_error(result, "ICAD-P0031",
                          "duplicate SELECTION name '" + selection.name + "'",
                          selection.location);
            }
            if (std::ranges::none_of(body.features, [&](const auto& feature) {
                    return feature.name == selection.source_feature;
                })) {
                add_error(result, "ICAD-P0031",
                          "SELECTION FROM must reference an earlier feature in the BODY",
                          selection.location);
            }
            body.topology_selections.push_back(std::move(selection));
            continue;
        }
        if (keyword(line) == "FACE") {
            ast::FaceReferenceDecl reference;
            reference.location = line.front().location;
            const bool prefix = line.size() >= 6 && valid_identifier(line[1]) &&
                                line[2].lexeme == "FROM";
            std::size_t cursor = 3;
            std::string path;
            if (prefix)
                path = parse_qualified_name(line, cursor, result);
            if (!prefix || cursor != line.size() ||
                !split_persistent_face_path(path, reference.feature, reference.role)) {
                add_error(result, "ICAD-P0030",
                          "FACE expects NAME FROM FEATURE.face.top|bottom",
                          line.front().location);
                ++index;
                continue;
            }
            reference.name = line[1].lexeme;
            reference.topology_path = body.name + "/" + reference.feature + "/face." +
                                      reference.role;
            if (std::ranges::any_of(body.face_references, [&](const auto& existing) {
                    return existing.name == reference.name;
                })) {
                add_error(result, "ICAD-P0030",
                          "duplicate FACE reference name '" + reference.name + "'",
                          reference.location);
            }
            if (std::ranges::none_of(body.features, [&](const auto& feature) {
                    return feature.name == reference.feature;
                })) {
                add_error(result, "ICAD-P0030",
                          "FACE reference must select an earlier feature in the BODY",
                          reference.location);
            }
            body.face_references.push_back(std::move(reference));
            ++index;
            continue;
        }
        if (keyword(line) == "SKETCH") {
            const bool explicit_support = line.size() >= 5 && line[2].lexeme == "ON";
            auto sketch = parse_sketch(lines, index, result);
            if (!explicit_support) {
                add_error(result, "ICAD-P0027",
                          "BODY SKETCH requires ON PLANE or ON FACE support",
                          sketch.location);
            }
            if (!sketch.support_reference.empty() && sketch.support_feature.empty()) {
                const auto reference = std::ranges::find(
                    body.face_references, sketch.support_reference,
                    &ast::FaceReferenceDecl::name);
                if (reference == body.face_references.end()) {
                    add_error(result, "ICAD-P0030",
                              "face-attached SKETCH references an unknown prior FACE alias '" +
                                  sketch.support_reference + "'",
                              sketch.location);
                } else {
                    sketch.support_feature = reference->feature;
                    sketch.support_face = face_role_selector(reference->role);
                    sketch.support_topology_path = reference->topology_path;
                }
            } else if (!sketch.support_topology_path.empty()) {
                sketch.support_topology_path = body.name + "/" + sketch.support_topology_path;
            }
            if (sketch.support_feature.empty() == false &&
                std::ranges::none_of(body.features, [&](const auto& feature) {
                    return feature.name == sketch.support_feature;
                })) {
                add_error(result, "ICAD-P0027",
                          "face-attached SKETCH must reference an earlier feature in the BODY",
                          sketch.location);
            }
            if (std::ranges::any_of(body.sketches, [&](const auto& existing) {
                    return existing.name == sketch.name;
                })) {
                add_error(result, "ICAD-P0027", "duplicate body SKETCH name '" + sketch.name + "'",
                          sketch.location);
            }
            body.sketches.push_back(std::move(sketch));
            continue;
        }
        if (keyword(line) == "PAD" || keyword(line) == "POCKET") {
            const bool pad = keyword(line) == "PAD";
            const std::size_t minimum_tokens = pad ? 7 : 6;
            const bool prefix = line.size() >= minimum_tokens && valid_identifier(line[1]) &&
                                line[2].lexeme == "FROM";
            std::size_t cursor = 3;
            std::string profile;
            if (prefix)
                profile = parse_qualified_name(line, cursor, result);
            if (!prefix || profile.empty() || cursor >= line.size() ||
                line[cursor].lexeme != "DEPTH") {
                add_error(result, "ICAD-P0027",
                          pad ? "PAD expects NAME FROM SKETCH[.SHAPE] DEPTH VALUE NEW|ADD"
                              : "POCKET expects NAME FROM SKETCH[.SHAPE] DEPTH VALUE",
                          line.front().location);
                ++index;
                continue;
            }
            ++cursor;
            auto depth = parse_value(line, cursor, result);
            std::string operation;
            if (pad && cursor < line.size())
                operation = line[cursor++].lexeme;
            if (cursor != line.size() || (pad && operation != "NEW" && operation != "ADD")) {
                add_error(result, "ICAD-P0027",
                          pad ? "PAD must end with NEW or ADD" : "POCKET has trailing values",
                          line.front().location);
            }
            const auto separator = profile.find('.');
            const std::string sketch_name = profile.substr(0, separator);
            const auto sketch = std::ranges::find(body.sketches, sketch_name,
                                                  &ast::SketchDecl::name);
            if (sketch == body.sketches.end()) {
                add_error(result, "ICAD-P0027", "history operation references unknown prior SKETCH",
                          line.front().location);
            } else if (separator != std::string::npos) {
                const auto member_name = profile.substr(separator + 1);
                const bool shape_member =
                    std::ranges::any_of(sketch->shapes, [&](const auto& shape) {
                        return shape.name == member_name;
                    });
                const bool region_member =
                    std::ranges::any_of(sketch->regions, [&](const auto& region) {
                        return region.name == member_name;
                    });
                if (!shape_member && !region_member) {
                    add_error(result, "ICAD-P0027",
                              "history operation references unknown SHAPE or REGION '" +
                                  member_name + "'",
                              line.front().location);
                }
            } else if (!sketch->shapes.empty()) {
                add_error(result, "ICAD-P0027",
                          "multi-shape SKETCH operations must select SKETCH.SHAPE explicitly",
                          line.front().location);
            }
            ast::FeatureDecl feature;
            feature.name = line[1].lexeme;
            feature.source_keyword = pad ? "PAD" : "POCKET";
            feature.type = "EXTRUDE";
            feature.profile = profile;
            if (sketch != body.sketches.end() && separator != std::string::npos) {
                const auto member_name = profile.substr(separator + 1);
                if (std::ranges::any_of(sketch->regions, [&](const auto& region) {
                        return region.name == member_name;
                    })) {
                    feature.region = profile;
                }
            }
            feature.operation = pad ? (operation == "NEW" ? "NEW" : "UNION") : "CUT";
            feature.location = line.front().location;
            ast::PropertyDecl height;
            height.name = "HEIGHT";
            height.location = depth.location;
            height.value = std::move(depth.literal);
            height.parameter_reference = std::move(depth.parameter_reference);
            feature.properties.push_back(std::move(height));
            if (sketch != body.sketches.end()) {
                feature.sketch_plane = sketch->plane;
                feature.support_feature = sketch->support_feature;
                feature.support_face = sketch->support_face;
                feature.support_reference = sketch->support_reference;
                feature.support_topology_path = sketch->support_topology_path;
            }
            body.features.push_back(std::move(feature));
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0006",
                  "BODY accepts MATERIAL, FACE, SELECTION, SKETCH, PAD, POCKET, and FEATURE statements only",
                  line.front().location);
        ++index;
    }

    if (!closed) {
        add_error(result, "ICAD-P0007", "body block is missing END", body.location);
    }
    for (const auto& sketch : body.sketches) {
        const bool consumed = std::ranges::any_of(body.features, [&](const auto& feature) {
            const auto separator = feature.profile.find('.');
            const auto source_sketch = feature.profile.substr(0, separator);
            return feature.location.line > sketch.location.line &&
                   (source_sketch == sketch.name || feature.target_profile == sketch.name);
        });
        if (!consumed) {
            add_error(result, "ICAD-P0027",
                      "BODY SKETCH '" + sketch.name + "' must feed a later operation",
                      sketch.location);
        }
        for (const auto& shape : sketch.shapes) {
            if (shape.role == "CONSTRUCTION")
                continue;
            const auto reference = sketch.name + "." + shape.name;
            const bool directly_consumed = std::ranges::any_of(
                body.features, [&](const auto& feature) { return feature.profile == reference; });
            const bool region_consumed = std::ranges::any_of(sketch.regions, [&](const auto& region) {
                const bool contains = region.outer_shape == shape.name ||
                                      std::ranges::contains(region.hole_shapes, shape.name);
                const auto region_reference = sketch.name + "." + region.name;
                return contains && std::ranges::any_of(body.features, [&](const auto& feature) {
                           return feature.region == region_reference;
                       });
            });
            if (!directly_consumed && !region_consumed) {
                add_error(result, "ICAD-P0027",
                          "SHAPE '" + reference + "' must feed a later operation",
                          shape.location);
            }
        }
        for (const auto& region : sketch.regions) {
            const auto reference = sketch.name + "." + region.name;
            if (std::ranges::none_of(body.features, [&](const auto& feature) {
                    return feature.region == reference;
                })) {
                add_error(result, "ICAD-P0027",
                          "REGION '" + reference + "' must feed a later operation",
                          region.location);
            }
        }
    }
    return body;
}

[[nodiscard]] auto parse_keyframe(const Line& line, ParseResult& result) -> ast::KeyframeDecl {
    ast::KeyframeDecl frame;
    frame.location = line.front().location;
    const bool visibility_layout = line.size() == 5 && keyword(line) == "KEYFRAME" &&
                                   line[3].lexeme == "VISIBLE";
    if (visibility_layout) {
        frame.time = parse_quantity(line, 1, result);
        if (line[4].lexeme != "ON" && line[4].lexeme != "OFF") {
            add_error(result, "ICAD-P0016", "VISIBLE expects ON or OFF", line[4].location);
        }
        frame.visible = line[4].lexeme == "ON";
        frame.visibility_only = true;
        return frame;
    }
    const bool value_layout = line.size() >= 4 && keyword(line) == "KEYFRAME" &&
                              line[3].lexeme == "VALUE";
    if (value_layout) {
        frame.time = parse_quantity(line, 1, result);
        std::size_t cursor = 4;
        frame.joint_value = parse_value(line, cursor, result);
        frame.value_only = true;
        if (cursor != line.size()) {
            add_error(result, "ICAD-P0016", "joint KEYFRAME has unexpected trailing values",
                      line[cursor].location);
        }
        return frame;
    }
    const bool layout = line.size() == 17 && keyword(line) == "KEYFRAME" &&
                        line[3].lexeme == "POSITION" && line[10].lexeme == "ROTATION";
    if (!layout) {
        add_error(result, "ICAD-P0016",
                  "KEYFRAME expects TIME POSITION X Y Z ROTATION X Y Z, TIME VALUE scalar, "
                  "or TIME VISIBLE ON|OFF",
                  line.front().location);
        return frame;
    }
    frame.time = parse_quantity(line, 1, result);
    frame.position_x = parse_quantity(line, 4, result);
    frame.position_y = parse_quantity(line, 6, result);
    frame.position_z = parse_quantity(line, 8, result);
    frame.rotation_x = parse_quantity(line, 11, result);
    frame.rotation_y = parse_quantity(line, 13, result);
    frame.rotation_z = parse_quantity(line, 15, result);
    return frame;
}

[[nodiscard]] auto parse_track(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::TrackDecl {
    const Line& declaration = lines[index];
    ast::TrackDecl track;
    track.location = declaration.front().location;
    if (declaration.size() != 4 || !valid_identifier(declaration[1]) ||
        !valid_identifier(declaration[2]) || !valid_identifier(declaration[3])) {
        add_error(result, "ICAD-P0015", "TRACK expects NAME BODY|CAMERA|JOINT TARGET",
                  declaration.front().location);
    } else {
        track.name = declaration[1].lexeme;
        track.target_kind = declaration[2].lexeme;
        track.target = declaration[3].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "KEYFRAME") {
            track.keyframes.push_back(parse_keyframe(line, result));
        } else if (keyword(line) == "EASING" && line.size() == 2 && valid_identifier(line[1])) {
            track.easing = line[1].lexeme;
        } else {
            add_error(result, "ICAD-P0015", "TRACK accepts EASING and KEYFRAME statements",
                      line.front().location);
        }
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0015", "track block is missing END", track.location);
    }
    return track;
}

[[nodiscard]] auto parse_scene(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::SceneDecl {
    const Line& declaration = lines[index];
    ast::SceneDecl scene;
    scene.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0014", "SCENE expects exactly one identifier",
                  declaration.front().location);
    } else {
        scene.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "DURATION") {
            if (line.size() != 3) {
                add_error(result, "ICAD-P0014", "DURATION expects NUMBER UNIT",
                          line.front().location);
            } else {
                scene.duration = parse_quantity(line, 1, result);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "FPS") {
            if (line.size() != 2 || !parse_number(line[1], scene.frames_per_second)) {
                add_error(result, "ICAD-P0014", "FPS expects one number", line.front().location);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "BACKGROUND") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0014", "BACKGROUND expects one preset",
                          line.front().location);
            } else {
                scene.background = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LOOP") {
            double count{};
            if (line.size() != 2 || !parse_number(line[1], count) || count < 1.0 ||
                count != static_cast<double>(static_cast<std::size_t>(count))) {
                add_error(result, "ICAD-P0014", "LOOP expects a positive integer",
                          line.front().location);
            } else {
                scene.loop_count = static_cast<std::size_t>(count);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LIGHT") {
            const bool layout = (line.size() == 9 || line.size() == 11) &&
                                valid_identifier(line[1]) && valid_identifier(line[2]) &&
                                line[3].lexeme == "COLOR" && line[7].lexeme == "INTENSITY" &&
                                (line.size() == 9 || line[9].lexeme == "AT");
            ast::LightDecl light;
            light.location = line.front().location;
            if (!layout) {
                add_error(result, "ICAD-P0014",
                          "LIGHT expects NAME DIRECTIONAL|POINT COLOR R G B INTENSITY N [AT POINT]",
                          line.front().location);
            } else {
                light.name = line[1].lexeme;
                light.kind = line[2].lexeme;
                bool valid = parse_number(line[4], light.color[0]);
                valid = parse_number(line[5], light.color[1]) && valid;
                valid = parse_number(line[6], light.color[2]) && valid;
                valid = parse_number(line[8], light.intensity) && valid;
                if (!valid) {
                    add_error(result, "ICAD-P0014", "LIGHT color and intensity must be numbers",
                              line.front().location);
                }
                if (line.size() == 11) {
                    light.point = line[10].lexeme;
                }
                scene.lights.push_back(std::move(light));
            }
            ++index;
            continue;
        }
        if (keyword(line) == "EVENT") {
            if (line.size() != 4 || !valid_identifier(line[3])) {
                add_error(result, "ICAD-P0014", "EVENT expects TIME NAME", line.front().location);
            } else {
                ast::SceneEventDecl event;
                event.time = parse_quantity(line, 1, result);
                event.name = line[3].lexeme;
                event.location = line.front().location;
                scene.events.push_back(std::move(event));
            }
            ++index;
            continue;
        }
        if (keyword(line) == "TRACK") {
            scene.tracks.push_back(parse_track(lines, index, result));
            continue;
        }
        add_error(result, "ICAD-P0014",
                  "SCENE accepts DURATION, FPS, BACKGROUND, LOOP, LIGHT, EVENT, and TRACK statements",
                  line.front().location);
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0014", "scene block is missing END", scene.location);
    }
    return scene;
}

[[nodiscard]] auto parse_material(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::MaterialDecl {
    const Line& declaration = lines[index];
    ast::MaterialDecl material;
    material.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0013", "MATERIAL block expects one name",
                  declaration.front().location);
    } else {
        material.name = declaration[1].lexeme;
    }
    ++index;
    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        const auto entry = keyword(line);
        if (entry == "END") {
            ++index;
            closed = true;
            break;
        }
        if (entry == "PROFILE" && line.size() == 2 && valid_identifier(line[1])) {
            material.profile = line[1].lexeme;
        } else if (entry == "PRESET" && line.size() == 2 && valid_identifier(line[1])) {
            material.preset = line[1].lexeme;
        } else if (entry == "BASE_COLOR" && line.size() == 5) {
            bool valid = true;
            for (std::size_t component = 0; component < 4; ++component) {
                valid = parse_number(line[component + 1], material.base_color[component]) && valid;
            }
            if (!valid) {
                add_error(result, "ICAD-P0013", "BASE_COLOR expects four numbers",
                          line.front().location);
            } else {
                material.has_base_color = true;
            }
        } else if ((entry == "METALLIC" || entry == "ROUGHNESS") && line.size() == 2) {
            double value{};
            if (!parse_number(line[1], value)) {
                add_error(result, "ICAD-P0013", std::string{entry} + " expects one number",
                          line.front().location);
            } else if (entry == "METALLIC") {
                material.metallic = value;
                material.has_metallic = true;
            } else {
                material.roughness = value;
                material.has_roughness = true;
            }
        } else if (entry == "TEXTURE_SCALE" && line.size() == 3) {
            material.texture_scale.literal = parse_quantity(line, 1, result);
            material.texture_scale.location = line.front().location;
            material.has_texture_scale = true;
        } else if (entry == "UV_MODE" && line.size() == 2 && valid_identifier(line[1])) {
            material.uv_mode = line[1].lexeme;
        } else {
            add_error(result, "ICAD-P0013",
                      "MATERIAL accepts PROFILE, PRESET, BASE_COLOR, METALLIC, ROUGHNESS, "
                      "TEXTURE_SCALE, and UV_MODE",
                      line.front().location);
        }
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0013", "material block is missing END", material.location);
    }
    return material;
}

} // namespace

auto parse(const std::vector<Token>& tokens) -> ParseResult {
    ParseResult result;
    auto requirements = language::check_requirements(tokens);
    result.program.requirements = std::move(requirements.requirements);
    result.diagnostics = std::move(requirements.diagnostics);
    if (!result.diagnostics.empty()) {
        return result;
    }
    const Lines lines = lines_from(tokens);
    std::size_t index = 0;

    while (index < lines.size()) {
        const Line& line = lines[index];
        const std::string_view first = keyword(line);
        if (first == "REQUIRES") {
            ++index;
            continue;
        }
        if (first == "PROJECT") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "PROJECT expects exactly one identifier",
                          line.front().location);
            } else if (!result.program.project_name.empty()) {
                add_error(result, "ICAD-P0008", "PROJECT is declared more than once",
                          line.front().location);
            } else {
                result.program.project_name = line[1].lexeme;
                result.program.location = line.front().location;
            }
            ++index;
            continue;
        }
        if (first == "UNITS") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "UNITS expects exactly one identifier",
                          line.front().location);
            } else if (!result.program.default_length_unit.empty()) {
                add_error(result, "ICAD-P0009", "UNITS is declared more than once",
                          line.front().location);
            } else {
                result.program.default_length_unit = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (first == "PARAMETER") {
            if (line.size() < 3 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "PARAMETER expects NAME SCALAR_EXPRESSION",
                          line.front().location);
            } else {
                ast::ParameterDecl parameter;
                parameter.name = line[1].lexeme;
                parameter.location = line.front().location;
                parameter.expression = parse_expression(line, 2, line.size(), result);
                std::string unused_reference;
                retain_legacy_value(parameter.expression, parameter.value, unused_reference);
                result.program.parameters.push_back(std::move(parameter));
            }
            ++index;
            continue;
        }
        if (first == "ANGLE") {
            if (line.size() < 3 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0020", "ANGLE expects NAME SCALAR_EXPRESSION",
                          line.front().location);
            } else {
                ast::ValueDecl value;
                value.location = line[2].location;
                value.expression = parse_expression(line, 2, line.size(), result);
                retain_legacy_value(value.expression, value.literal,
                                    value.parameter_reference);
                result.program.angles.push_back(
                    ast::AngleDecl{line[1].lexeme, std::move(value), line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "POINT3") {
            const bool derived = line.size() >= 8 && line[2].lexeme == "FROM" &&
                                 valid_identifier(line[3]) && line[4].lexeme == "ALONG" &&
                                 valid_identifier(line[5]) && line[6].lexeme == "DISTANCE";
            std::size_t cursor = 2;
            if (!valid_identifier(line[1])) {
                add_error(result, "ICAD-P0020",
                          "POINT3 expects a name and an absolute or derived point definition",
                          line.front().location);
            } else if (derived) {
                ast::Point3Decl point;
                point.name = line[1].lexeme;
                point.base_point = line[3].lexeme;
                point.direction = line[5].lexeme;
                point.derived = true;
                point.location = line.front().location;
                cursor = 7;
                point.distance = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "POINT3 has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.points.push_back(std::move(point));
            } else if (line.size() < 5) {
                add_error(result, "ICAD-P0020",
                          "POINT3 expects NAME and three length quantities or parameters",
                          line.front().location);
            } else {
                std::array<ast::ValueDecl, 3> coordinates;
                for (auto& coordinate : coordinates) {
                    coordinate = parse_value(line, cursor, result);
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "POINT3 has unexpected trailing values",
                              line[cursor].location);
                }
                ast::Point3Decl point;
                point.name = line[1].lexeme;
                point.coordinates = std::move(coordinates);
                point.location = line.front().location;
                result.program.points.push_back(std::move(point));
            }
            ++index;
            continue;
        }
        if (first == "VECTOR") {
            const bool between_points = line.size() == 6 && line[2].lexeme == "FROM" &&
                                 valid_identifier(line[3]) && line[4].lexeme == "TO" &&
                                 valid_identifier(line[5]);
            const bool rotated = line.size() >= 8 && line[2].lexeme == "ROTATE" &&
                                 valid_identifier(line[3]) && line[4].lexeme == "AROUND" &&
                                 valid_identifier(line[5]) && line[6].lexeme == "BY";
            std::array<double, 3> components{};
            if (between_points && valid_identifier(line[1])) {
                ast::VectorDecl vector;
                vector.name = line[1].lexeme;
                vector.from_point = line[3].lexeme;
                vector.to_point = line[5].lexeme;
                vector.derived = true;
                vector.location = line.front().location;
                result.program.vectors.push_back(std::move(vector));
            } else if (rotated && valid_identifier(line[1])) {
                ast::VectorDecl vector;
                vector.name = line[1].lexeme;
                vector.source_vector = line[3].lexeme;
                vector.around_axis = line[5].lexeme;
                vector.rotated = true;
                vector.location = line.front().location;
                std::size_t cursor = 7;
                vector.angle = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "VECTOR has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.vectors.push_back(std::move(vector));
            } else if (line.size() != 5 || !valid_identifier(line[1]) ||
                       !parse_number(line[2], components[0]) ||
                       !parse_number(line[3], components[1]) ||
                       !parse_number(line[4], components[2])) {
                add_error(result, "ICAD-P0020",
                          "VECTOR expects NAME X Y Z, NAME FROM POINT TO POINT, or NAME ROTATE "
                          "VECTOR AROUND VECTOR BY ANGLE",
                          line.front().location);
            } else {
                ast::VectorDecl vector;
                vector.name = line[1].lexeme;
                vector.components = components;
                vector.location = line.front().location;
                result.program.vectors.push_back(std::move(vector));
            }
            ++index;
            continue;
        }
        if (first == "POSE") {
            const bool prefix = line.size() >= 8 && valid_identifier(line[1]) &&
                                line[2].lexeme == "AT" && valid_identifier(line[3]) &&
                                line[4].lexeme == "ROTATION";
            if (!prefix) {
                add_error(result, "ICAD-P0021", "POSE expects BODY AT POINT ROTATION X Y Z angles",
                          line.front().location);
            } else {
                std::size_t cursor = 5;
                std::array<ast::ValueDecl, 3> rotation;
                for (auto& angle : rotation) {
                    angle = parse_value(line, cursor, result);
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0021", "POSE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.poses.push_back(ast::PoseDecl{
                    line[1].lexeme, line[3].lexeme, std::move(rotation), line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "TOLERANCE") {
            const bool prefix = line.size() >= 5 && line[1].lexeme == "LINEAR";
            if (!prefix || result.program.tolerance.declared) {
                add_error(result, "ICAD-P0027",
                          "TOLERANCE expects LINEAR length ANGULAR angle and may appear once",
                          line.front().location);
            } else {
                std::size_t cursor = 2;
                ast::ToleranceDecl tolerance;
                tolerance.linear = parse_value(line, cursor, result);
                if (cursor >= line.size() || line[cursor].lexeme != "ANGULAR") {
                    add_error(result, "ICAD-P0027", "TOLERANCE requires ANGULAR after LINEAR",
                              line.front().location);
                } else {
                    ++cursor;
                    tolerance.angular = parse_value(line, cursor, result);
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0027", "TOLERANCE has unexpected trailing values",
                              line[cursor].location);
                }
                tolerance.declared = true;
                tolerance.location = line.front().location;
                result.program.tolerance = std::move(tolerance);
            }
            ++index;
            continue;
        }
        if (first == "INSTANCE") {
            const bool prefix = line.size() >= 9 && valid_identifier(line[1]) &&
                                line[2].lexeme == "OF" && valid_identifier(line[3]) &&
                                line[4].lexeme == "AT" && valid_identifier(line[5]) &&
                                line[6].lexeme == "ROTATION";
            if (!prefix) {
                add_error(result, "ICAD-P0028",
                          "INSTANCE expects NAME OF BODY AT POINT ROTATION X Y Z",
                          line.front().location);
            } else {
                std::size_t cursor = 7;
                std::array<ast::ValueDecl, 3> rotation;
                for (auto& angle : rotation)
                    angle = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0028", "INSTANCE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.instances.push_back(ast::InstanceDecl{
                    line[1].lexeme, line[3].lexeme, line[5].lexeme, std::move(rotation),
                    line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "JOINT") {
            const bool prefix =
                line.size() >= 9 && valid_identifier(line[1]) && valid_identifier(line[2]) &&
                valid_identifier(line[3]) && valid_identifier(line[4]) && line[5].lexeme == "AT" &&
                valid_identifier(line[6]) && line[7].lexeme == "AXIS" && valid_identifier(line[8]);
            if (!prefix) {
                add_error(result, "ICAD-P0022",
                          "JOINT expects NAME TYPE PARENT CHILD AT POINT AXIS VECTOR",
                          line.front().location);
            } else {
                ast::JointDecl joint;
                joint.name = line[1].lexeme;
                joint.kind = line[2].lexeme;
                joint.parent_body = line[3].lexeme;
                joint.child_body = line[4].lexeme;
                joint.point = line[6].lexeme;
                joint.axis = line[8].lexeme;
                joint.location = line.front().location;
                std::size_t cursor = 9;
                if (joint.kind != "FIXED") {
                    if (cursor >= line.size() || line[cursor].lexeme != "VALUE") {
                        add_error(result, "ICAD-P0022",
                                  "moving JOINT requires VALUE and LIMIT declarations",
                                  line.front().location);
                    } else {
                        ++cursor;
                        joint.value = parse_value(line, cursor, result);
                        if (cursor >= line.size() || line[cursor].lexeme != "LIMIT") {
                            add_error(result, "ICAD-P0022",
                                      "moving JOINT requires LIMIT LOWER UPPER", joint.location);
                        } else {
                            ++cursor;
                            joint.lower_limit = parse_value(line, cursor, result);
                            joint.upper_limit = parse_value(line, cursor, result);
                            joint.driven = true;
                        }
                    }
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0022", "JOINT has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.joints.push_back(std::move(joint));
            }
            ++index;
            continue;
        }
        if (first == "INTERFACE") {
            const bool prefix = line.size() >= 10 && valid_identifier(line[1]) &&
                                line[2].lexeme == "BODY" && valid_identifier(line[3]) &&
                                line[4].lexeme == "AT" && valid_identifier(line[5]) &&
                                line[6].lexeme == "AXIS" && valid_identifier(line[7]) &&
                                line[8].lexeme == "TYPE" && valid_identifier(line[9]);
            if (!prefix) {
                add_error(result, "ICAD-P0030",
                          "INTERFACE expects NAME BODY OCCURRENCE AT POINT AXIS VECTOR TYPE KIND [SIZE VALUE]",
                          line.front().location);
            } else {
                ast::InterfaceDecl interface;
                interface.name = line[1].lexeme;
                interface.occurrence = line[3].lexeme;
                interface.point = line[5].lexeme;
                interface.axis = line[7].lexeme;
                interface.kind = line[9].lexeme;
                interface.location = line.front().location;
                std::size_t cursor = 10;
                if (cursor < line.size() && line[cursor].lexeme == "SIZE") {
                    ++cursor;
                    interface.size = parse_value(line, cursor, result);
                    interface.has_size = true;
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0030", "INTERFACE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.interfaces.push_back(std::move(interface));
            }
            ++index;
            continue;
        }
        if (first == "CONNECT") {
            const bool prefix = line.size() >= 6 && valid_identifier(line[1]) &&
                                valid_identifier(line[2]) && valid_identifier(line[3]) &&
                                line[4].lexeme == "METHOD" && valid_identifier(line[5]);
            if (!prefix) {
                add_error(result, "ICAD-P0031",
                          "CONNECT expects NAME INTERFACE INTERFACE METHOD KIND plus manufacturing metadata",
                          line.front().location);
            } else {
                ast::ConnectionDecl connection;
                connection.name = line[1].lexeme;
                connection.first_interface = line[2].lexeme;
                connection.second_interface = line[3].lexeme;
                connection.method = line[5].lexeme;
                connection.location = line.front().location;
                std::size_t cursor = 6;
                while (cursor < line.size()) {
                    const std::string option = line[cursor].lexeme;
                    ++cursor;
                    if (option == "AUTO") {
                        connection.automatic = true;
                    } else if (option == "STANDARD" || option == "FASTENER" || option == "FIT" ||
                               option == "FILLER" || option == "PROCESS") {
                        if (cursor >= line.size() || !valid_identifier(line[cursor])) {
                            add_error(result, "ICAD-P0031", option + " expects an identifier",
                                      line[cursor - 1].location);
                            break;
                        }
                        if (option == "STANDARD")
                            connection.standard = line[cursor].lexeme;
                        else if (option == "FASTENER")
                            connection.fastener = line[cursor].lexeme;
                        else if (option == "FIT")
                            connection.fit = line[cursor].lexeme;
                        else if (option == "FILLER")
                            connection.filler = line[cursor].lexeme;
                        else
                            connection.weld_process = line[cursor].lexeme;
                        ++cursor;
                    } else if (option == "QUANTITY") {
                        double quantity = 0.0;
                        if (cursor >= line.size() || !parse_number(line[cursor], quantity) ||
                            quantity < 1.0 || quantity > 1'000'000.0 ||
                            std::floor(quantity) != quantity) {
                            add_error(result, "ICAD-P0031", "QUANTITY expects a positive integer",
                                      line[cursor - 1].location);
                            break;
                        }
                        connection.quantity = static_cast<std::size_t>(quantity);
                        connection.quantity_explicit = true;
                        ++cursor;
                    } else if (option == "DEPOSITION_EFFICIENCY") {
                        if (cursor >= line.size() ||
                            !parse_number(line[cursor], connection.deposition_efficiency)) {
                            add_error(result, "ICAD-P0031",
                                      "DEPOSITION_EFFICIENCY expects a number",
                                      line[cursor - 1].location);
                            break;
                        }
                        connection.has_deposition_efficiency = true;
                        ++cursor;
                    } else if (option == "CLEARANCE") {
                        connection.clearance = parse_value(line, cursor, result);
                    } else if (option == "WELD_SIZE" || option == "WELD_LENGTH" ||
                               option == "FILLER_DIAMETER" || option == "STOCK_LENGTH") {
                        auto parsed = parse_value(line, cursor, result);
                        if (option == "WELD_SIZE") {
                            connection.weld_size = std::move(parsed);
                            connection.has_weld_size = true;
                        } else if (option == "WELD_LENGTH") {
                            connection.weld_length = std::move(parsed);
                            connection.has_weld_length = true;
                        } else if (option == "FILLER_DIAMETER") {
                            connection.filler_diameter = std::move(parsed);
                            connection.has_filler_diameter = true;
                        } else {
                            connection.stock_length = std::move(parsed);
                            connection.has_stock_length = true;
                        }
                    } else {
                        add_error(result, "ICAD-P0031", "unknown CONNECT option '" + option + "'",
                                  line[cursor - 1].location);
                        break;
                    }
                }
                result.program.connections.push_back(std::move(connection));
            }
            ++index;
            continue;
        }
        if (first == "MATERIAL") {
            if (line.size() == 2) {
                result.program.materials.push_back(parse_material(lines, index, result));
                continue;
            }
            if (line.size() != 3 || !valid_identifier(line[1]) || !valid_identifier(line[2])) {
                add_error(result, "ICAD-P0013", "MATERIAL expects NAME PRESET",
                          line.front().location);
            } else {
                ast::MaterialDecl material;
                material.name = line[1].lexeme;
                material.preset = line[2].lexeme;
                material.location = line.front().location;
                result.program.materials.push_back(std::move(material));
            }
            ++index;
            continue;
        }
        if (first == "PROFILE") {
            result.program.profiles.push_back(parse_profile(lines, index, result));
            continue;
        }
        if (first == "SKETCH") {
            const bool top_level_form = line.size() == 2;
            const auto location = line.front().location;
            auto sketch = parse_sketch(lines, index, result);
            if (!top_level_form) {
                add_error(result, "ICAD-P0027",
                          "SKETCH ON PLANE or ON FACE must appear inside a BODY", location);
            }
            result.program.sketches.push_back(std::move(sketch));
            continue;
        }
        if (first == "BODY") {
            result.program.bodies.push_back(parse_body(lines, index, result));
            continue;
        }
        if (first == "CONSTRAINT") {
            const bool references = line.size() >= 5 && valid_identifier(line[1]) &&
                                    valid_identifier(line[2]) && valid_identifier(line[3]) &&
                                    valid_identifier(line[4]);
            const bool no_value = line.size() == 5 && (line[2].lexeme == "PARALLEL" ||
                                                       line[2].lexeme == "PERPENDICULAR");
            const bool value_kind = line[2].lexeme == "MIN_DISTANCE" ||
                                    line[2].lexeme == "COINCIDENT" ||
                                    line[2].lexeme == "ANGLE_BETWEEN";
            const bool with_value = line.size() >= 6 && value_kind;
            if (!references || (!no_value && !with_value)) {
                add_error(result, "ICAD-P0019",
                          "CONSTRAINT expects NAME KIND REFERENCE REFERENCE and an optional "
                          "tolerance or target quantity",
                          line.front().location);
            } else {
                ast::ConstraintDecl constraint;
                constraint.name = line[1].lexeme;
                constraint.kind = line[2].lexeme;
                constraint.first_body = line[3].lexeme;
                constraint.second_body = line[4].lexeme;
                constraint.location = line.front().location;
                if (with_value) {
                    std::size_t cursor = 5;
                    constraint.target = parse_value(line, cursor, result);
                    if (cursor != line.size()) {
                        add_error(result, "ICAD-P0019",
                                  "CONSTRAINT has unexpected trailing values",
                                  line[cursor].location);
                    }
                }
                result.program.constraints.push_back(std::move(constraint));
            }
            ++index;
            continue;
        }
        if (first == "MATE") {
            const bool header = line.size() >= 9 && valid_identifier(line[1]) &&
                                valid_identifier(line[3]) && valid_identifier(line[4]) &&
                                valid_identifier(line[5]) && valid_identifier(line[6]);
            const bool face = header && line[2].lexeme == "FACE" &&
                              line[7].lexeme == "OFFSET";
            const bool edge = header && line[2].lexeme == "EDGE" &&
                              line[7].lexeme == "TOLERANCE";
            if (!face && !edge) {
                add_error(result, "ICAD-P0027",
                          "MATE expects NAME FACE OCCURRENCE SELECTOR OCCURRENCE SELECTOR OFFSET VALUE or NAME EDGE ... TOLERANCE VALUE",
                          line.front().location);
            } else {
                ast::MateDecl mate;
                mate.name = line[1].lexeme;
                mate.kind = line[2].lexeme;
                mate.first_occurrence = line[3].lexeme;
                mate.first_selector = line[4].lexeme;
                mate.second_occurrence = line[5].lexeme;
                mate.second_selector = line[6].lexeme;
                mate.location = line.front().location;
                std::size_t cursor = 8;
                mate.target = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0027", "MATE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.mates.push_back(std::move(mate));
            }
            ++index;
            continue;
        }
        if (first == "SCENE") {
            result.program.scenes.push_back(parse_scene(lines, index, result));
            continue;
        }

        add_error(result, "ICAD-P0010",
                  "unexpected top-level statement '" + std::string{first} + "'",
                  line.front().location);
        ++index;
    }

    if (result.program.project_name.empty()) {
        add_error(result, "ICAD-P0011", "source must declare exactly one PROJECT",
                  SourceLocation{1, 1});
    }
    if (result.program.default_length_unit.empty()) {
        add_error(result, "ICAD-P0012", "source must declare default UNITS",
                  result.program.location);
    }
    return result;
}

} // namespace icad::compiler
