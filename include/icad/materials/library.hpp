#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace icad::materials {

struct Preset {
    std::string_view name;
    std::array<double, 4> base_color;
    double metallic;
    double roughness;
    std::string_view texture;
    unsigned int texture_seed;
};

// A datasheet value is deliberately not collapsed to one scalar. Supplier
// documents distinguish guaranteed limits, typical values, and nominal values;
// engineering consumers must preserve that distinction.
struct NumericValue {
    std::optional<double> minimum;
    std::optional<double> typical;
    std::optional<double> maximum;
    std::string unit;
    std::string qualifier;
};

struct CompositionEntry {
    std::string constituent;
    NumericValue mass_percent;
    bool balance{};
    std::string note;
};

struct Property {
    std::string name;
    NumericValue value;
    std::optional<double> temperature_c;
    std::string condition;
    std::string test_method;
};

struct ProductForm {
    std::string form;
    std::string condition;
    std::string specification;
    std::string size_scope;
    std::string note;
};

struct Source {
    std::string publisher;
    std::string title;
    std::string document_id;
    std::string revision;
    std::string url;
    std::string accessed_on;
    std::string location;
    std::string source_type;
};

struct Profile {
    std::string id;
    std::string display_name;
    std::string material_class;
    std::string material_subclass;
    std::string grade;
    std::string condition;
    std::vector<std::string> aliases;
    std::string appearance_preset;
    std::string composition_disclosure;
    std::vector<CompositionEntry> composition;
    std::vector<Property> properties;
    std::vector<ProductForm> product_forms;
    std::vector<Source> sources;
    std::string limitations;
};

[[nodiscard]] auto find(std::string_view name) -> std::optional<Preset>;
[[nodiscard]] auto all() -> std::span<const Preset>;
[[nodiscard]] auto find_profile(std::string_view id_or_alias) -> const Profile*;
[[nodiscard]] auto all_profiles() -> std::span<const Profile>;
[[nodiscard]] auto profile_classes() -> std::vector<std::string_view>;
[[nodiscard]] auto profile_subclasses() -> std::vector<std::string_view>;
[[nodiscard]] auto catalog_json(std::optional<std::string_view> material_class = std::nullopt,
                                std::optional<std::string_view> id = std::nullopt) -> std::string;
[[nodiscard]] auto catalog_revision() noexcept -> std::string_view;

} // namespace icad::materials
