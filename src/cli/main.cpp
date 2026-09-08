#include "icad/ai/inspector.hpp"
#include "icad/agent/workflow.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/cad/queries.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/compiler/language.hpp"
#include "icad/compiler/lexer/lexer.hpp"
#include "icad/compiler/lexer/token.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/drawings/exporter.hpp"
#include "icad/document/exporter.hpp"
#include "icad/evidence/compliance.hpp"
#include "icad/exchange/exporter.hpp"
#include "icad/lsp/server.hpp"
#include "icad/manufacturing/validator.hpp"
#include "icad/materials/library.hpp"
#include "icad/mcp/server.hpp"
#include "icad/project/builder.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view version = ICAD_VERSION;

auto print_usage(std::ostream& output) -> void {
    output << "usage: icad check <source.icad>\n"
              "       icad tokens <source.icad>\n"
              "       icad ast <source.icad>\n"
              "       icad inspect <source.icad>\n"
              "       icad inspect-json <source.icad>\n"
              "       icad visual-json <source.icad>\n"
              "       icad compare-json <first.icad> <second.icad>\n"
              "       icad topology-json <source.icad>\n"
              "       icad diagnostics-json <source.icad>\n"
              "       icad evidence-json <source.icad> --manifest <evidence.json>\n"
              "       icad compliance-json <source.icad> --manifest <evidence.json> --basis <basis>\n"
              "       icad compliance-report <source.icad> --manifest <evidence.json> --format html [--output <report.html>]\n"
              "       icad measure <source.icad>\n"
              "       icad distance-json <source.icad> <first-body> <second-body>\n"
              "       icad interference-json <source.icad>\n"
              "       icad section-json <source.icad> <px> <py> <pz> <nx> <ny> <nz> [body]\n"
              "       icad validate <source.icad>\n"
              "       icad manufacturing <source.icad>\n"
              "       icad inspect-step <model.step>\n"
              "       icad inspect-stl <model.stl>\n"
              "       icad inspect-gltf <model.gltf|model.glb>\n"
              "       icad inspect-3mf <model.3mf>\n"
              "       icad inspect-dxf <drawing.dxf>\n"
              "       icad agent-concept <prompt>\n"
              "       icad agent-bootstrap <prompt> [--source-out <source.icad>]\n"
              "       icad agent-create <prompt> --source-out <source.icad> --output-dir <directory>\n"
              "       icad agent-review <source.icad>\n"
              "       icad drawing-svg <source.icad> <drawing.svg>\n"
              "       icad drawing-dxf <source.icad> <drawing.dxf>\n"
              "       icad build <source.icad> [--output-dir <directory>]\n"
              "       icad language\n"
              "       icad materials\n"
              "       icad materials-json [--class <class>]\n"
              "       icad material-json <profile-id-or-alias>\n"
              "       icad bom-json <source.icad>\n"
              "       icad lsp\n"
              "       icad mcp [--workspace <directory>]\n"
              "       icad --version\n";
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

auto print_diagnostics(const std::filesystem::path& path,
                       const std::vector<icad::compiler::Diagnostic>& diagnostics) -> void {
    for (const auto& diagnostic : diagnostics) {
        std::cerr << path.string() << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": error[" << diagnostic.code
                  << "]: " << diagnostic.message << '\n';
    }
}

auto print_ast(const icad::compiler::ast::Program& program) -> void {
    for (const auto& requirement : program.requirements) {
        if (requirement.kind == icad::compiler::ast::RequirementKind::language_version) {
            std::cout << "Requires ICAD " << requirement.version_major << '.'
                      << requirement.version_minor << '\n';
        } else {
            std::cout << "Requires capability " << requirement.capability << '\n';
        }
    }
    std::cout << "Project " << program.project_name << "\n"
              << "  Units " << program.default_length_unit << '\n';
    for (const auto& parameter : program.parameters) {
        std::cout << "  Parameter " << parameter.name << " = ";
        if (!parameter.expression.source.empty()) {
            std::cout << parameter.expression.source;
        } else {
            std::cout << parameter.value.value << ' ' << parameter.value.unit;
        }
        std::cout << '\n';
    }
    for (const auto& material : program.materials) {
        std::cout << "  Material " << material.name << " : " << material.preset << '\n';
    }
    for (const auto& profile : program.profiles) {
        std::cout << "  Profile " << profile.name << " : " << profile.points.size() << " points\n";
    }
    for (const auto& body : program.bodies) {
        std::cout << "  Body " << body.name << '\n';
        if (!body.material.empty()) {
            std::cout << "    Material " << body.material << '\n';
        }
        for (const auto& sketch : body.sketches) {
            std::cout << "    Sketch " << sketch.name;
            if (sketch.support_feature.empty()) {
                std::cout << " on plane " << sketch.plane;
            } else {
                std::cout << " on face " << sketch.support_feature << ' '
                          << sketch.support_face;
            }
            if (!sketch.shapes.empty())
                std::cout << " : " << sketch.shapes.size() << " shapes, "
                          << sketch.regions.size() << " regions\n";
            else if (sketch.circle)
                std::cout << " : circle\n";
            else
                std::cout << " : " << sketch.points.size() << " points, "
                          << sketch.entities.size() << " explicit entities\n";
        }
        for (const auto& feature : body.features) {
            std::cout << "    " << (feature.source_keyword == "FEATURE" ? "Feature"
                                                                           : feature.source_keyword)
                      << ' ' << feature.name << " : " << feature.type;
            if (!feature.region.empty())
                std::cout << " from region " << feature.region;
            else if (!feature.profile.empty())
                std::cout << " from " << feature.profile;
            std::cout << '\n';
            for (const auto& property : feature.properties) {
                std::cout << "      " << property.name << " = ";
                if (!property.expression.source.empty())
                    std::cout << property.expression.source;
                else if (!property.parameter_reference.empty())
                    std::cout << property.parameter_reference;
                else
                    std::cout << property.value.value << ' ' << property.value.unit;
                std::cout << '\n';
            }
        }
    }
    for (const auto& scene : program.scenes) {
        std::cout << "  Scene " << scene.name << " : " << scene.tracks.size() << " tracks\n";
    }
}

auto print_ir(const icad::compiler::ir::Project& project) -> void {
    std::size_t feature_count = 0;
    std::size_t property_count = 0;
    std::size_t profile_segment_count = 0;
    std::size_t curved_profile_segment_count = 0;
    std::size_t track_count = 0;
    std::size_t keyframe_count = 0;
    for (const auto& body : project.bodies) {
        feature_count += body.features.size();
        for (const auto& feature : body.features) {
            property_count += feature.properties.size();
        }
    }
    for (const auto& scene : project.scenes) {
        track_count += scene.tracks.size();
        for (const auto& track : scene.tracks) {
            keyframe_count += track.keyframes.size();
        }
    }
    for (const auto& profile : project.profiles) {
        profile_segment_count += profile.segments.size();
        curved_profile_segment_count += static_cast<std::size_t>(std::ranges::count(
            profile.segments, icad::compiler::ir::ProfileSegmentKind::circular_arc,
            &icad::compiler::ir::ProfileSegment::kind));
    }
    std::cout << "PROJECT " << project.name << '\n'
              << "CANONICAL_LENGTH_UNIT " << project.canonical_length_unit << '\n'
              << "PARAMETERS " << project.parameters.size() << '\n'
              << "ANGLES " << project.angles.size() << '\n'
              << "POINTS3 " << project.points.size() << '\n'
              << "VECTORS " << project.vectors.size() << '\n'
              << "POSES " << project.poses.size() << '\n'
              << "INSTANCES " << project.instances.size() << '\n'
              << "JOINTS " << project.joints.size() << '\n'
              << "MATERIALS " << project.materials.size() << '\n'
              << "PROFILES " << project.profiles.size() << '\n'
              << "PROFILE_SEGMENTS " << profile_segment_count << '\n'
              << "CURVED_PROFILE_SEGMENTS " << curved_profile_segment_count << '\n'
              << "BODIES " << project.bodies.size() << '\n'
              << "FEATURES " << feature_count << '\n'
              << "PROPERTIES " << property_count << '\n'
              << "CONSTRAINTS " << project.constraints.size() << '\n'
              << "MATES " << project.mates.size() << '\n'
              << "SCENES " << project.scenes.size() << '\n'
              << "ANIMATION_TRACKS " << track_count << '\n'
              << "KEYFRAMES " << keyframe_count << '\n';
}

[[nodiscard]] auto output_directory(int argc, char** argv) -> std::optional<std::filesystem::path> {
    if (argc == 3) {
        return std::filesystem::current_path();
    }
    if (argc == 5 && std::string_view{argv[3]} == "--output-dir") {
        return std::filesystem::path{argv[4]};
    }
    return std::nullopt;
}

[[nodiscard]] auto parse_double(std::string_view source) -> std::optional<double> {
    double value{};
    std::istringstream stream{std::string{source}};
    stream.imbue(std::locale::classic());
    if (!(stream >> value))
        return std::nullopt;
    char trailing{};
    if (stream >> trailing)
        return std::nullopt;
    return value;
}

auto export_outputs(const icad::compiler::ir::Project& project,
                    const std::filesystem::path& source_path,
                    const std::filesystem::path& directory) -> int {
    const auto result = icad::project::build(project, directory, source_path.stem().string());
    if (!result.success) {
        std::cerr << "icad: build failed: " << result.message << '\n';
        return 1;
    }
    for (const auto& artifact : result.artifacts) {
        std::cout << artifact.path.string() << ": " << artifact.kind << ", " << artifact.bytes
                  << " bytes\n";
    }
    std::cout << "BUILD components=" << result.components << " solids=" << result.solids
              << " vertices=" << result.vertices << " triangles=" << result.triangles
              << " materials=" << result.materials << " scenes=" << result.scenes
              << " keyframes=" << result.keyframes << '\n';
    return 0;
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "icad " << version << '\n';
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "materials") {
        for (const auto& material : icad::materials::all()) {
            std::cout << material.name << " metallic=" << material.metallic
                      << " roughness=" << material.roughness << " texture=" << material.texture
                      << '\n';
        }
        return 0;
    }
    if ((argc == 2 || argc == 4) && std::string_view{argv[1]} == "materials-json") {
        if (argc == 4 && std::string_view{argv[2]} != "--class") {
            print_usage(std::cerr);
            return 2;
        }
        const auto material_class = argc == 4
                                        ? std::optional<std::string_view>{argv[3]}
                                        : std::optional<std::string_view>{};
        std::cout << icad::materials::catalog_json(material_class) << '\n';
        return 0;
    }
    if (argc == 3 && std::string_view{argv[1]} == "material-json") {
        if (icad::materials::find_profile(argv[2]) == nullptr) {
            std::cerr << "icad: unknown engineering material profile: " << argv[2] << '\n';
            return 1;
        }
        std::cout << icad::materials::catalog_json(std::nullopt, std::string_view{argv[2]})
                  << '\n';
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "language") {
        std::cout << "ICAD_LANGUAGE " << icad::compiler::language::version << '\n';
        for (const auto capability : icad::compiler::language::capabilities()) {
            std::cout << "CAPABILITY " << capability << '\n';
        }
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "lsp") {
        return icad::lsp::run(std::cin, std::cout);
    }
    if (argc == 2 && std::string_view{argv[1]} == "mcp") {
        return icad::mcp::run(std::cin, std::cout, std::filesystem::current_path());
    }
    if (argc == 4 && std::string_view{argv[1]} == "mcp" &&
        std::string_view{argv[2]} == "--workspace") {
        return icad::mcp::run(std::cin, std::cout, std::filesystem::path{argv[3]});
    }
    if (argc < 3) {
        print_usage(std::cerr);
        return 2;
    }

    const std::string_view command{argv[1]};
    const std::filesystem::path source_path{argv[2]};
    if (command == "agent-concept") {
        if (argc != 3) {
            print_usage(std::cerr);
            return 2;
        }
        std::cout << icad::agent::conceptualize_json(argv[2]) << '\n';
        return 0;
    }
    if (command == "agent-bootstrap") {
        if (argc != 3 && argc != 5) {
            print_usage(std::cerr);
            return 2;
        }
        const auto result = icad::agent::bootstrap(argv[2]);
        if (argc == 5) {
            if (std::string_view{argv[3]} != "--source-out") {
                print_usage(std::cerr);
                return 2;
            }
            std::ofstream output{argv[4], std::ios::binary | std::ios::trunc};
            if (!output || !(output << result.source)) {
                std::cerr << "icad: cannot write agent scaffold: " << argv[4] << '\n';
                return 1;
            }
        }
        std::cout << result.json << '\n';
        return 0;
    }
    if (command == "agent-create") {
        if (argc != 7 || std::string_view{argv[3]} != "--source-out" ||
            std::string_view{argv[5]} != "--output-dir") {
            print_usage(std::cerr);
            return 2;
        }
        const auto result = icad::agent::bootstrap(argv[2]);
        const auto review = icad::agent::review_json(result.source);
        if (!review.contains("\"ready\":true")) {
            std::cerr << review << '\n';
            return 1;
        }
        const std::filesystem::path generated_source{argv[4]};
        std::error_code error;
        if (!generated_source.parent_path().empty()) {
            std::filesystem::create_directories(generated_source.parent_path(), error);
        }
        std::ofstream output{generated_source, std::ios::binary | std::ios::trunc};
        if (error || !output || !(output << result.source)) {
            std::cerr << "icad: cannot write agent design: " << generated_source.string() << '\n';
            return 1;
        }
        output.close();
        const auto compilation = icad::compiler::compile(result.source);
        if (!compilation.ok()) {
            print_diagnostics(generated_source, compilation.diagnostics);
            return 1;
        }
        std::cout << result.json << '\n' << review << '\n';
        return export_outputs(*compilation.ir_project, generated_source,
                              std::filesystem::path{argv[6]});
    }
    if (command == "inspect-step") {
        const auto inspection = icad::exchange::inspect_step(source_path);
        if (!inspection.success) {
            std::cerr << "icad: STEP inspection failed: " << inspection.message << '\n';
            return 1;
        }
        std::cout << "STEP_ROOTS " << inspection.roots << '\n'
                  << "STEP_SOLIDS " << inspection.solids << '\n'
                  << "STEP_ASSEMBLY_COMPONENTS " << inspection.assembly_components << '\n';
        return 0;
    }
    if (command == "inspect-stl") {
        const auto inspection = icad::exchange::inspect_stl(source_path);
        if (!inspection.success) {
            std::cerr << "icad: STL inspection failed: " << inspection.message << '\n';
            return 1;
        }
        std::cout << "STL_SOLIDS " << inspection.solids << '\n'
                  << "STL_FACETS " << inspection.facets << '\n';
        return 0;
    }
    if (command == "inspect-gltf") {
        const auto inspection = icad::exchange::inspect_gltf(source_path);
        if (!inspection.success) {
            std::cerr << "icad: glTF inspection failed: " << inspection.message << '\n';
            return 1;
        }
        std::cout << "GLTF_OBJECTS " << inspection.objects << '\n';
        return 0;
    }
    if (command == "inspect-3mf") {
        const auto inspection = icad::exchange::inspect_3mf(source_path);
        if (!inspection.success) {
            std::cerr << "icad: 3MF inspection failed: " << inspection.message << '\n';
            return 1;
        }
        std::cout << "THREEMF_OBJECTS " << inspection.objects << '\n';
        return 0;
    }
    if (command == "inspect-dxf") {
        const auto inspection = icad::drawings::inspect_dxf(source_path);
        if (!inspection.success) {
            std::cerr << "icad: DXF inspection failed: " << inspection.message << '\n';
            return 1;
        }
        std::cout << "DXF_VALID 1\n";
        return 0;
    }
    if (command == "compare-json") {
        if (argc != 4) {
            print_usage(std::cerr);
            return 2;
        }
        const std::filesystem::path second_path{argv[3]};
        const auto first_source = read_file(source_path);
        const auto second_source = read_file(second_path);
        if (!first_source || !second_source) {
            std::cerr << "icad: cannot read comparison source file\n";
            return 2;
        }
        const icad::compiler::CompileOptions first_options{
            .build_topology = true,
            .imports = {.source_path = source_path, .project_root = source_path.parent_path()}};
        const icad::compiler::CompileOptions second_options{
            .build_topology = true,
            .imports = {.source_path = second_path, .project_root = second_path.parent_path()}};
        const auto first = icad::compiler::compile(*first_source, first_options);
        const auto second = icad::compiler::compile(*second_source, second_options);
        if (!first.ok())
            print_diagnostics(source_path, first.diagnostics);
        if (!second.ok())
            print_diagnostics(second_path, second.diagnostics);
        if (!first.ok() || !second.ok())
            return 1;
        std::cout << icad::ai::comparison_json(*first.ir_project, *second.ir_project) << '\n';
        return 0;
    }
    const auto source = read_file(source_path);
    if (!source) {
        std::cerr << "icad: cannot read source file: " << source_path.string() << '\n';
        return 2;
    }

    if (command == "agent-review") {
        const auto review = icad::agent::review_json(*source);
        std::cout << review << '\n';
        return review.contains("\"ready\":true") ? 0 : 1;
    }

    const bool evidence_command = command == "evidence-json" ||
                                  command == "compliance-json" ||
                                  command == "compliance-report";
    const icad::compiler::CompileOptions file_options{
        .build_topology = !evidence_command,
        .imports = {.source_path = source_path, .project_root = source_path.parent_path()}};

    if (command == "tokens") {
        const auto imported = icad::compiler::expand_imports(*source, file_options.imports);
        if (!imported.ok()) {
            print_diagnostics(source_path, imported.diagnostics);
            return 1;
        }
        const auto result = icad::compiler::lex(imported.source);
        if (!result.ok()) {
            print_diagnostics(source_path, result.diagnostics);
            return 1;
        }
        for (const auto& token : result.tokens) {
            if (token.kind == icad::compiler::TokenKind::newline ||
                token.kind == icad::compiler::TokenKind::end_of_file) {
                continue;
            }
            std::cout << token.location.line << ':' << token.location.column << ' '
                      << icad::compiler::token_kind_name(token.kind) << ' ' << token.lexeme << '\n';
        }
        return 0;
    }

    if (command == "diagnostics-json") {
        const auto diagnostics = icad::compiler::compile(*source, file_options);
        std::cout << icad::ai::diagnostics_json(*source) << '\n';
        return diagnostics.ok() ? 0 : 1;
    }

    const auto result = icad::compiler::compile(*source, file_options);
    if (!result.ok()) {
        print_diagnostics(source_path, result.diagnostics);
        return 1;
    }
    if (command == "check") {
        std::cout << source_path.string() << ": compile check passed\n";
        return 0;
    }
    if (command == "bom-json") {
        if (argc != 3) {
            print_usage(std::cerr);
            return 2;
        }
        std::cout << icad::document::bom_json(*result.ir_project) << '\n';
        return 0;
    }
    if (command == "evidence-json" || command == "compliance-json" ||
        command == "compliance-report") {
        const bool evidence_only = command == "evidence-json";
        const bool report = command == "compliance-report";
        const bool valid_shape = evidence_only ? argc == 5
                                 : report      ? (argc == 7 || argc == 9)
                                               : argc == 7;
        if (!valid_shape || std::string_view{argv[3]} != "--manifest") {
            print_usage(std::cerr);
            return 2;
        }
        const std::filesystem::path manifest_path{argv[4]};
        const auto manifest = read_file(manifest_path);
        if (!manifest) {
            std::cerr << "icad: cannot read evidence manifest: " << manifest_path.string() << '\n';
            return 2;
        }
        std::string_view basis;
        if (!evidence_only) {
            if (report) {
                if (std::string_view{argv[5]} != "--format" ||
                    std::string_view{argv[6]} != "html") {
                    print_usage(std::cerr);
                    return 2;
                }
            } else {
                if (std::string_view{argv[5]} != "--basis") {
                    print_usage(std::cerr);
                    return 2;
                }
                basis = argv[6];
            }
        }
        const auto evaluation = icad::evidence::evaluate(
            *source, *result.ir_project, *manifest, manifest_path, basis);
        if (evidence_only) {
            std::cout << icad::evidence::evidence_json(evaluation) << '\n';
        } else if (!report) {
            std::cout << icad::evidence::compliance_json(evaluation) << '\n';
        } else {
            std::filesystem::path output = manifest_path.parent_path() / "compliance-report.html";
            if (argc == 9) {
                if (std::string_view{argv[7]} != "--output") {
                    print_usage(std::cerr);
                    return 2;
                }
                output = argv[8];
            }
            std::ofstream html{output, std::ios::binary | std::ios::trunc};
            if (!html || !(html << icad::evidence::compliance_html(evaluation))) {
                std::cerr << "icad: cannot write compliance report: " << output.string() << '\n';
                return 1;
            }
            std::cout << output.string() << ": icad.compliance.v1 html\n";
        }
        for (const auto& issue : evaluation.issues) {
            if (issue.severity == icad::evidence::Severity::error)
                std::cerr << issue.path << ':' << issue.line << ':' << issue.column
                          << ": error[" << issue.code << "]: " << issue.message << '\n';
        }
        return evaluation.manifest_valid ? 0 : 1;
    }
    if (command == "ast") {
        print_ast(*result.program);
        return 0;
    }
    if (command == "inspect") {
        print_ir(*result.ir_project);
        return 0;
    }
    if (command == "inspect-json") {
        std::cout << icad::ai::project_json(*result.ir_project) << '\n';
        return 0;
    }
    if (command == "visual-json") {
        std::cout << icad::ai::visual_snapshot_json(*result.ir_project) << '\n';
        return 0;
    }
    if (command == "topology-json") {
        std::cout << icad::ai::topology_json(*result.ir_project) << '\n';
        return 0;
    }
    if (command == "measure") {
        const auto analysis = icad::cad::analyze(*result.ir_project);
        std::cout << "PARTS " << analysis.parts.size() << '\n'
                  << "SURFACE_AREA_MM2 " << analysis.surface_area_mm2 << '\n'
                  << "VOLUME_MM3 " << analysis.volume_mm3 << '\n'
                  << "BOUNDS_MIN " << analysis.bounds.minimum[0] << ' '
                  << analysis.bounds.minimum[1] << ' ' << analysis.bounds.minimum[2] << '\n'
                  << "BOUNDS_MAX " << analysis.bounds.maximum[0] << ' '
                  << analysis.bounds.maximum[1] << ' ' << analysis.bounds.maximum[2] << '\n';
        return 0;
    }
    if (command == "distance-json") {
        if (argc != 5) {
            print_usage(std::cerr);
            return 2;
        }
        const auto query = icad::cad::exact_polyhedral_distance(*result.ir_project, argv[3], argv[4]);
        if (!query.found) {
            std::cerr << "icad: distance query requires two different existing bodies\n";
            return 1;
        }
        std::cout << std::setprecision(17)
                  << "{\"schema\":\"icad.distance.v1\",\"representation\":\""
                  << query.representation << "\",\"firstBody\":\"" << query.first_body
                  << "\",\"secondBody\":\"" << query.second_body << "\",\"distanceMm\":"
                  << query.distance_mm << ",\"firstPointMm\":[" << query.first_point.x << ','
                  << query.first_point.y << ',' << query.first_point.z << "],\"secondPointMm\":["
                  << query.second_point.x << ',' << query.second_point.y << ','
                  << query.second_point.z << "]}\n";
        return 0;
    }
    if (command == "interference-json") {
        const auto analysis = icad::cad::analyze_intersections(
            *result.ir_project, result.ir_project->tolerance.linear_mm);
        std::cout << "{\"schema\":\"icad.interference.v1\",\"representation\":"
                     "\"polyhedralSolidClassification\",\"penetratingPartPairs\":"
                  << analysis.penetrating_part_pairs << ",\"containedPartPairs\":"
                  << analysis.contained_part_pairs << ",\"surfaceContactOnlyPartPairs\":"
                  << analysis.surface_contact_only_part_pairs
                  << ",\"declaredEngagementPartPairs\":"
                  << analysis.declared_engagement_part_pairs
                  << ",\"unintendedPenetratingPartPairs\":"
                  << analysis.unintended_penetrating_part_pairs << ",\"bodyPairs\":[";
        for (std::size_t index = 0; index < analysis.body_contacts.size(); ++index) {
            if (index != 0)
                std::cout << ',';
            const auto& contact = analysis.body_contacts[index];
            std::cout << "{\"firstBody\":\"" << contact.first_body
                      << "\",\"secondBody\":\"" << contact.second_body
                      << "\",\"penetratingPartPairs\":" << contact.penetrating_part_pairs
                      << ",\"containedPartPairs\":" << contact.contained_part_pairs
                      << ",\"surfaceContactOnlyPartPairs\":"
                      << contact.surface_contact_only_part_pairs
                      << ",\"witnessPointMm\":[" << contact.witness_point.x << ','
                      << contact.witness_point.y << ',' << contact.witness_point.z << ']'
                      << ",\"declaredConnection\":"
                      << (contact.declared_connection ? "true" : "false");
            if (contact.declared_connection) {
                std::cout << ",\"connection\":\"" << contact.connection_name
                          << "\",\"method\":\"" << contact.connection_method
                          << "\",\"standard\":\"" << contact.connection_standard << '"';
            }
            std::cout << '}';
        }
        std::cout << "]}\n";
        return 0;
    }
    if (command == "section-json") {
        if (argc != 9 && argc != 10) {
            print_usage(std::cerr);
            return 2;
        }
        std::array<double, 6> values{};
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto parsed = parse_double(argv[index + 3]);
            if (!parsed) {
                std::cerr << "icad: section plane values must be finite numbers\n";
                return 2;
            }
            values[index] = *parsed;
        }
        const auto query = icad::cad::section(
            *result.ir_project,
            {{values[0], values[1], values[2]}, {values[3], values[4], values[5]}},
            argc == 10 ? std::string_view{argv[9]} : std::string_view{});
        std::cout << std::setprecision(17)
                  << "{\"schema\":\"icad.section.v1\",\"representation\":\""
                  << query.representation << "\",\"toleranceMm\":" << query.tolerance_mm
                  << ",\"segments\":[";
        for (std::size_t index = 0; index < query.segments.size(); ++index) {
            if (index != 0)
                std::cout << ',';
            const auto& segment = query.segments[index];
            std::cout << "{\"body\":\"" << segment.body << "\",\"part\":\"" << segment.part
                      << "\",\"startMm\":[" << segment.segment.start.x << ','
                      << segment.segment.start.y << ',' << segment.segment.start.z
                      << "],\"endMm\":[" << segment.segment.end.x << ',' << segment.segment.end.y
                      << ',' << segment.segment.end.z << "]}";
        }
        std::cout << "]}\n";
        return 0;
    }
    if (command == "validate") {
        const auto constraints = icad::constraints::validate(*result.ir_project);
        for (const auto& constraint : constraints) {
            std::cout << "CONSTRAINT " << constraint.name << ' '
                      << (constraint.passed ? "PASS" : "FAIL")
                      << " required=" << constraint.required_mm << constraint.unit
                      << " actual=" << constraint.actual_mm << constraint.unit << '\n';
        }
        if (!icad::constraints::all_passed(constraints)) {
            return 1;
        }
        std::cout << source_path.string() << ": geometry validation passed\n";
        return 0;
    }
    if (command == "manufacturing") {
        const auto report = icad::manufacturing::validate(*result.ir_project);
        for (const auto& issue : report.issues) {
            std::cout << issue.code << ' '
                      << (issue.severity == icad::manufacturing::Severity::error     ? "ERROR"
                          : issue.severity == icad::manufacturing::Severity::warning ? "WARNING"
                                                                                     : "INFO")
                      << ' ' << issue.subject << ": " << issue.message << '\n';
        }
        std::cout << "MANUFACTURING " << (report.passed ? "PASS" : "FAIL") << '\n';
        return report.passed ? 0 : 1;
    }
    if (command == "drawing-svg" || command == "drawing-dxf") {
        if (argc != 4) {
            print_usage(std::cerr);
            return 2;
        }
        const std::filesystem::path output{argv[3]};
        const auto exported = command == "drawing-svg"
                                  ? icad::drawings::write_svg(*result.ir_project, output)
                                  : icad::drawings::write_dxf(*result.ir_project, output);
        if (!exported.success) {
            std::cerr << "icad: drawing export failed: " << exported.message << '\n';
            return 1;
        }
        std::cout << output.string() << ": " << command << '\n';
        return 0;
    }
    if (command == "build") {
        const auto directory = output_directory(argc, argv);
        if (!directory) {
            print_usage(std::cerr);
            return 2;
        }
        return export_outputs(*result.ir_project, source_path, *directory);
    }

    std::cerr << "icad: unknown command: " << command << '\n';
    print_usage(std::cerr);
    return 2;
}
