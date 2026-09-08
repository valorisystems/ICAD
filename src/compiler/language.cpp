#include "icad/compiler/language.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace icad::compiler::language {
namespace {

constexpr std::array supported_capabilities{
    std::string_view{"CAPABILITY_NEGOTIATION"},
    std::string_view{"PARAMETER_EXPRESSIONS_V1"},
    std::string_view{"QUALIFIED_VALUE_REFERENCES_V1"},
    std::string_view{"MULTI_SHAPE_SKETCH_V1"},
    std::string_view{"SKETCH_REGION_ARRANGEMENT_V1"},
    std::string_view{"ADVANCED_SKETCH_CONSTRAINTS_V1"},
    std::string_view{"SKETCH_LINE_ARC_TANGENCY_V1"},
    std::string_view{"SEMANTIC_EDGE_LOOP_SELECTION_V1"},
    std::string_view{"TOPOLOGY_QUERY_V1"},
    std::string_view{"PERSISTENT_FACE_REFERENCES_V1"},
    std::string_view{"IMPORT_INJECT"},
    std::string_view{"BODY_HISTORY"},
    std::string_view{"NAMED_SKETCH_ENTITIES"},
    std::string_view{"SKETCH_CONSTRAINTS"},
    std::string_view{"ASSEMBLY_OCCURRENCES"},
    std::string_view{"MATES_AND_JOINTS"},
    std::string_view{"MANUFACTURING_CONNECTIONS_V1"},
    std::string_view{"MATERIAL_PROFILE_V1"},
    std::string_view{"CALCULATED_BILINGUAL_BOM_V1"},
    std::string_view{"MAGNETIC_INTERFACE_SNAP_V1"},
    std::string_view{"SCENES"},
    std::string_view{"VISUAL_JSON_V1"},
};

using TokenLine = std::vector<const Token*>;

auto add_error(RequirementResult& result, std::string code, std::string message,
               SourceLocation location) -> void {
    result.diagnostics.push_back(
        Diagnostic{DiagnosticSeverity::error, std::move(code), std::move(message), location});
}

[[nodiscard]] auto parse_component(std::string_view source, std::size_t& value) -> bool {
    if (source.empty()) {
        return false;
    }
    value = 0;
    for (const char character : source) {
        if (character < '0' || character > '9') {
            return false;
        }
        const auto digit = static_cast<std::size_t>(character - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    return true;
}

[[nodiscard]] auto parse_version(std::string_view source, std::size_t& major,
                                 std::size_t& minor) -> bool {
    const auto separator = source.find('.');
    return separator != std::string_view::npos && source.find('.', separator + 1) == std::string_view::npos &&
           parse_component(source.substr(0, separator), major) &&
           parse_component(source.substr(separator + 1), minor);
}

[[nodiscard]] auto version_supported(std::size_t major, std::size_t minor) -> bool {
    return major < version_major || (major == version_major && minor <= version_minor);
}

auto check_line(const TokenLine& line, bool& declarations_started, bool& language_seen,
                std::set<std::string>& capability_requirements, RequirementResult& result) -> void {
    if (line.empty()) {
        return;
    }
    const Token& first = *line.front();
    if (first.lexeme != "REQUIRES") {
        declarations_started = true;
        return;
    }
    if (declarations_started) {
        add_error(result, "ICAD-C0005",
                  "REQUIRES declarations must precede PROJECT and all other declarations",
                  first.location);
        return;
    }
    if (line.size() != 3 || line[1]->kind != TokenKind::identifier) {
        add_error(result, "ICAD-C0001",
                  "REQUIRES expects ICAD MAJOR.MINOR or CAPABILITY NAME", first.location);
        return;
    }

    if (line[1]->lexeme == "ICAD") {
        if (language_seen) {
            add_error(result, "ICAD-C0004", "ICAD language version is required more than once",
                      first.location);
            return;
        }
        language_seen = true;
        std::size_t major = 0;
        std::size_t minor = 0;
        if (line[2]->kind != TokenKind::number ||
            !parse_version(line[2]->lexeme, major, minor)) {
            add_error(result, "ICAD-C0001", "ICAD language version must use MAJOR.MINOR",
                      line[2]->location);
            return;
        }
        if (!version_supported(major, minor)) {
            add_error(result, "ICAD-C0002",
                      "source requires ICAD language " + line[2]->lexeme +
                          " but this compiler supports through " + std::string{version},
                      line[2]->location);
            return;
        }
        result.requirements.push_back(ast::RequirementDecl{
            ast::RequirementKind::language_version, major, minor, {}, first.location});
        return;
    }

    if (line[1]->lexeme == "CAPABILITY") {
        if (line[2]->kind != TokenKind::identifier) {
            add_error(result, "ICAD-C0001", "capability name must be an identifier",
                      line[2]->location);
            return;
        }
        const std::string capability = line[2]->lexeme;
        if (!capability_requirements.insert(capability).second) {
            add_error(result, "ICAD-C0004",
                      "capability '" + capability + "' is required more than once",
                      line[2]->location);
            return;
        }
        if (!supports_capability(capability)) {
            add_error(result, "ICAD-C0003",
                      "required capability '" + capability + "' is not implemented",
                      line[2]->location);
            return;
        }
        result.requirements.push_back(ast::RequirementDecl{
            ast::RequirementKind::capability, 0, 0, capability, first.location});
        return;
    }

    add_error(result, "ICAD-C0001", "REQUIRES expects ICAD or CAPABILITY after the keyword",
              line[1]->location);
}

} // namespace

auto capabilities() noexcept -> std::span<const std::string_view> {
    return supported_capabilities;
}

auto supports_capability(std::string_view capability) noexcept -> bool {
    return std::ranges::find(supported_capabilities, capability) != supported_capabilities.end();
}

auto check_requirements(const std::vector<Token>& tokens) -> RequirementResult {
    RequirementResult result;
    TokenLine line;
    bool declarations_started = false;
    bool language_seen = false;
    std::set<std::string> capability_requirements;

    for (const auto& token : tokens) {
        if (token.kind == TokenKind::comment) {
            continue;
        }
        if (token.kind == TokenKind::newline || token.kind == TokenKind::end_of_file) {
            check_line(line, declarations_started, language_seen, capability_requirements, result);
            line.clear();
            if (token.kind == TokenKind::end_of_file) {
                break;
            }
            continue;
        }
        line.push_back(&token);
    }
    return result;
}

} // namespace icad::compiler::language
