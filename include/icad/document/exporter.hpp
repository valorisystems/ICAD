#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <filesystem>
#include <string>

namespace icad::document {

struct ExportResult {
    bool success{false};
    std::string message;
};

[[nodiscard]] auto bom_json(const compiler::ir::Project& project) -> std::string;
[[nodiscard]] auto write_bom(const compiler::ir::Project& project,
                             const std::filesystem::path& output) -> ExportResult;

} // namespace icad::document
