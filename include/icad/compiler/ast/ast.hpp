#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace icad::compiler::ast {

enum class RequirementKind { language_version, capability };

struct RequirementDecl {
    RequirementKind kind{RequirementKind::language_version};
    std::size_t version_major{};
    std::size_t version_minor{};
    std::string capability;
    SourceLocation location;
};

struct QuantityLiteral {
    double value{};
    std::string unit;
    SourceLocation location;
};

enum class ScalarExpressionOp {
    literal,
    reference,
    unary_plus,
    unary_minus,
    add,
    subtract,
    multiply,
    divide,
};

struct ScalarExpressionNode {
    ScalarExpressionOp operation{ScalarExpressionOp::literal};
    double literal{};
    std::string symbol;
    std::string unit;
    SourceLocation location;
};

struct ScalarExpression {
    std::vector<ScalarExpressionNode> postfix;
    std::vector<std::string> references;
    std::string source;
    SourceLocation location;

    [[nodiscard]] auto empty() const noexcept -> bool { return postfix.empty(); }
};

struct ParameterDecl {
    std::string name;
    QuantityLiteral value;
    SourceLocation location;
    ScalarExpression expression;
};

struct ValueDecl {
    QuantityLiteral literal;
    std::string parameter_reference;
    SourceLocation location;
    ScalarExpression expression;
};

struct AngleDecl {
    std::string name;
    ValueDecl value;
    SourceLocation location;
};

struct ToleranceDecl {
    ValueDecl linear;
    ValueDecl angular;
    bool declared{};
    SourceLocation location;
};

struct Point3Decl {
    std::string name;
    std::array<ValueDecl, 3> coordinates;
    std::string base_point;
    std::string direction;
    ValueDecl distance;
    bool derived{};
    SourceLocation location;
};

struct VectorDecl {
    std::string name;
    std::array<double, 3> components{};
    std::string from_point;
    std::string to_point;
    std::string source_vector;
    std::string around_axis;
    ValueDecl angle;
    bool derived{};
    bool rotated{};
    SourceLocation location;
};

struct PoseDecl {
    std::string body;
    std::string point;
    std::array<ValueDecl, 3> rotation;
    SourceLocation location;
};

struct InstanceDecl {
    std::string name;
    std::string body;
    std::string point;
    std::array<ValueDecl, 3> rotation;
    SourceLocation location;
};

struct JointDecl {
    std::string name;
    std::string kind;
    std::string parent_body;
    std::string child_body;
    std::string point;
    std::string axis;
    ValueDecl value;
    ValueDecl lower_limit;
    ValueDecl upper_limit;
    bool driven{};
    SourceLocation location;
};

struct InterfaceDecl {
    std::string name;
    std::string occurrence;
    std::string point;
    std::string axis;
    std::string kind;
    ValueDecl size;
    bool has_size{};
    SourceLocation location;
};

struct ConnectionDecl {
    std::string name;
    std::string first_interface;
    std::string second_interface;
    std::string method;
    std::string standard;
    std::string fastener;
    std::string fit;
    std::string filler;
    std::string weld_process;
    ValueDecl clearance;
    ValueDecl weld_size;
    ValueDecl weld_length;
    ValueDecl filler_diameter;
    ValueDecl stock_length;
    std::size_t quantity{1};
    double deposition_efficiency{};
    bool quantity_explicit{};
    bool has_weld_size{};
    bool has_weld_length{};
    bool has_filler_diameter{};
    bool has_stock_length{};
    bool has_deposition_efficiency{};
    bool automatic{};
    SourceLocation location;
};

struct MaterialDecl {
    std::string name;
    std::string profile;
    std::string preset;
    std::array<double, 4> base_color{};
    double metallic{};
    double roughness{};
    ValueDecl texture_scale;
    std::string uv_mode;
    bool has_base_color{};
    bool has_metallic{};
    bool has_roughness{};
    bool has_texture_scale{};
    SourceLocation location;
};

struct PropertyDecl {
    std::string name;
    QuantityLiteral value;
    std::string parameter_reference;
    SourceLocation location;
    ScalarExpression expression;
};

struct Point2Decl {
    QuantityLiteral x;
    QuantityLiteral y;
    SourceLocation location;
};

enum class ProfileMode { unset, points, path, circle };
enum class PathSegmentKind { line, circular_arc };

struct PathSegmentDecl {
    PathSegmentKind kind{PathSegmentKind::line};
    Point2Decl end;
    Point2Decl center;
    bool counterclockwise{true};
    SourceLocation location;
};

struct ProfileDecl {
    std::string name;
    ProfileMode mode{ProfileMode::unset};
    std::vector<Point2Decl> points;
    Point2Decl path_start;
    std::vector<PathSegmentDecl> path_segments;
    bool path_closed{};
    Point2Decl circle_center;
    QuantityLiteral circle_radius;
    SourceLocation location;
};

struct SketchPointDecl {
    std::string name;
    ValueDecl x;
    ValueDecl y;
    bool fixed{};
    SourceLocation location;
};

struct SketchConstraintDecl {
    std::string name;
    std::string kind;
    std::vector<std::string> references;
    ValueDecl target;
    SourceLocation location;
};

enum class SketchEntityKind { line, circular_arc, circle };

struct SketchEntityDecl {
    std::string name;
    SketchEntityKind kind{SketchEntityKind::line};
    std::string start;
    std::string end;
    std::string center;
    ValueDecl radius;
    bool counterclockwise{};
    SourceLocation location;
};

struct SketchShapeDecl {
    std::string name;
    std::string closure{"CLOSED"};
    std::string role{"STOCK"};
    std::vector<SketchPointDecl> points;
    std::vector<SketchEntityDecl> entities;
    SourceLocation location;
};

struct SketchRegionDecl {
    std::string name;
    std::string outer_shape;
    std::vector<std::string> hole_shapes;
    SourceLocation location;
};

struct FaceReferenceDecl {
    std::string name;
    std::string feature;
    std::string role;
    std::string topology_path;
    SourceLocation location;
};

struct TopologySelectionDecl {
    std::string name;
    std::string source_feature;
    std::string entity_kind{"EDGE_LOOP"};
    std::string geometry{"CIRCULAR"};
    std::string convexity;
    std::string adjacent_face;
    SourceLocation location;
};

struct SketchDecl {
    std::string name;
    std::string plane{"XY"};
    std::string support_feature;
    std::string support_face;
    std::string support_reference;
    std::string support_topology_path;
    std::vector<SketchPointDecl> points;
    std::vector<SketchEntityDecl> entities;
    std::vector<SketchShapeDecl> shapes;
    std::vector<SketchRegionDecl> regions;
    std::vector<SketchConstraintDecl> constraints;
    std::array<ValueDecl, 2> circle_center;
    ValueDecl circle_radius;
    bool circle{};
    std::string solve_requirement;
    SourceLocation location;
};

struct ConstraintDecl {
    std::string name;
    std::string kind;
    std::string first_body;
    std::string second_body;
    ValueDecl target;
    SourceLocation location;
};

struct MateDecl {
    std::string name;
    std::string kind;
    std::string first_occurrence;
    std::string first_selector;
    std::string second_occurrence;
    std::string second_selector;
    ValueDecl target;
    SourceLocation location;
};

struct FeatureDecl {
    std::string name;
    std::string source_keyword{"FEATURE"};
    std::string type;
    std::string profile;
    std::string region;
    std::string target_profile;
    std::string operation;
    std::string selected_edge_point;
    std::string selected_edge_location;
    std::string selected_edge_classification;
    std::string selected_edge_set;
    std::string direction;
    std::string plane_point;
    std::string plane_normal;
    std::string sketch_plane{"XY"};
    std::string support_feature;
    std::string support_face;
    std::string support_reference;
    std::string support_topology_path;
    std::vector<std::string> path_points;
    std::size_t count{};
    bool has_count{};
    std::vector<PropertyDecl> properties;
    SourceLocation location;
};

struct BodyDecl {
    std::string name;
    std::string material;
    std::vector<SketchDecl> sketches;
    std::vector<FeatureDecl> features;
    SourceLocation location;
    std::vector<FaceReferenceDecl> face_references;
    std::vector<TopologySelectionDecl> topology_selections;
};

struct KeyframeDecl {
    QuantityLiteral time;
    QuantityLiteral position_x;
    QuantityLiteral position_y;
    QuantityLiteral position_z;
    QuantityLiteral rotation_x;
    QuantityLiteral rotation_y;
    QuantityLiteral rotation_z;
    ValueDecl joint_value;
    bool value_only{};
    bool visible{true};
    bool visibility_only{};
    SourceLocation location;
};

struct TrackDecl {
    std::string name;
    std::string target_kind;
    std::string target;
    std::string easing{"LINEAR"};
    std::vector<KeyframeDecl> keyframes;
    SourceLocation location;
};

struct LightDecl {
    std::string name;
    std::string kind;
    std::array<double, 3> color{};
    double intensity{};
    std::string point;
    SourceLocation location;
};

struct SceneEventDecl {
    QuantityLiteral time;
    std::string name;
    SourceLocation location;
};

struct SceneDecl {
    std::string name;
    QuantityLiteral duration;
    double frames_per_second{};
    std::string background;
    std::size_t loop_count{1};
    std::vector<LightDecl> lights;
    std::vector<SceneEventDecl> events;
    std::vector<TrackDecl> tracks;
    SourceLocation location;
};

struct Program {
    std::string project_name;
    std::string default_length_unit;
    std::vector<RequirementDecl> requirements;
    ToleranceDecl tolerance;
    std::vector<ParameterDecl> parameters;
    std::vector<AngleDecl> angles;
    std::vector<Point3Decl> points;
    std::vector<VectorDecl> vectors;
    std::vector<PoseDecl> poses;
    std::vector<InstanceDecl> instances;
    std::vector<JointDecl> joints;
    std::vector<InterfaceDecl> interfaces;
    std::vector<ConnectionDecl> connections;
    std::vector<MaterialDecl> materials;
    std::vector<ProfileDecl> profiles;
    std::vector<SketchDecl> sketches;
    std::vector<BodyDecl> bodies;
    std::vector<ConstraintDecl> constraints;
    std::vector<MateDecl> mates;
    std::vector<SceneDecl> scenes;
    SourceLocation location;
};

} // namespace icad::compiler::ast
