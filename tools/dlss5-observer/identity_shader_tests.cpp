#include "effect_parser.hpp"
#include "effect_codegen.hpp"
#include "effect_preprocessor.hpp"
#include <cstdio>
#include <memory>

int main()
{
    reshadefx::preprocessor pp;
    pp.add_include_path(IDENTITY_SHADER_INCLUDE);
    for (const auto &macro : {std::pair{"__RESHADE__", "60800"}, {"__RENDERER__", "0x10000"},
        {"BUFFER_WIDTH", "1920"}, {"BUFFER_HEIGHT", "1111"},
        {"BUFFER_RCP_WIDTH", "(1.0 / 1920)"}, {"BUFFER_RCP_HEIGHT", "(1.0 / 1111)"}})
        pp.add_macro_definition(macro.first, macro.second);
    if (!pp.append_file(IDENTITY_SHADER_SOURCE)) { std::fprintf(stderr, "%s", pp.errors().c_str()); return 1; }
    std::unique_ptr<reshadefx::codegen> backend(reshadefx::create_codegen_glsl(false, false, false, false));
    reshadefx::parser parser;
    if (!parser.parse(pp.output(), backend.get())) { std::fprintf(stderr, "%s", parser.errors().c_str()); return 1; }
    const auto &module = backend->module();
    if (module.techniques.size() != 1 || module.techniques[0].name != "FS_P1Identity" || module.techniques[0].passes.size() != 1) return 1;
    const auto &pass = module.techniques[0].passes[0];
    if (pass.srgb_write_enable || pass.stencil_enable || pass.blend_enable[0] || !pass.storage_bindings.empty()) return 1;
    if (backend->finalize_code().empty()) return 1;
    std::puts("Dedicated identity FX parses/generates GLSL with one unblended non-sRGB pass and no storage writes.");
    return 0;
}
