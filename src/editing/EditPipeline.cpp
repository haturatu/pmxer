#include "EditPipeline.hpp"
#include "EditGuards.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace pmxer::editing {
using Json = nlohmann::json;
PlanError::PlanError(std::string c, std::string m, Json d)
    : std::runtime_error(std::move(m)), code(std::move(c)), details(std::move(d)) {}
namespace {
void keys(const Json &j, std::initializer_list<const char *> allowed) {
    if (!j.is_object())
        throw PlanError("invalid_plan", "Expected an object");
    for (const auto &[k, v] : j.items()) {
        (void)v;
        if (std::none_of(allowed.begin(), allowed.end(), [&](auto a) { return k == a; }))
            throw PlanError("unknown_field", "Unknown field: " + k);
    }
}
Json operationSchema() {
    auto properties = ai::planSchema()["properties"]["operations"]["items"]["properties"];
    properties["pivot"] = {
        {"oneOf", Json::array({properties["pivot"],
                               Json{{"enum", {"center", "top-center", "bottom-center"}}}})}};
    properties["strength"] = {{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
    Json variants = Json::array();
    auto add = [&](const char *type, std::initializer_list<const char *> fields,
                   std::initializer_list<const char *> required) {
        Json branch = {{"type", "object"},
                       {"additionalProperties", false},
                       {"required", Json::array()},
                       {"properties", {{"type", {{"const", type}}}}}};
        branch["required"].push_back("type");
        for (auto field : fields)
            branch["properties"][field] = properties.at(field);
        for (auto field : required)
            branch["required"].push_back(field);
        variants.push_back(std::move(branch));
    };
    add("recolor", {"color"}, {"color"});
    add("transform", {"translation", "scale", "rotation", "pivot"}, {});
    variants.back()["anyOf"] =
        Json::array({Json{{"required", {"translation"}}}, Json{{"required", {"scale"}}},
                     Json{{"required", {"rotation"}}}});
    add("smooth", {"amount", "strength", "iterations"}, {});
    variants.back()["properties"]["amount"] = properties["strength"];
    variants.back()["oneOf"] =
        Json::array({Json{{"required", {"amount"}}}, Json{{"required", {"strength"}}}});
    add("inflate", {"amount", "iterations"}, {"amount"});
    add("subdivide", {"iterations"}, {});
    add("generate", {"shape", "color", "replace", "detail", "garment"},
        {"shape", "color", "replace"});
    variants.back()["properties"]["shape"]["enum"] = {"loafers", "shirt", "skirt", "dress",
                                                      "pants",   "boots", "coat"};
    return {{"oneOf", variants}};
}
void validateOperation(const Json &op) {
    if (!op.is_object() || !op.contains("type") || !op["type"].is_string())
        throw PlanError("invalid_operation", "Operation requires a string type");
    const auto schema = operationSchema();
    for (const auto &branch : schema["oneOf"])
        if (branch["properties"]["type"]["const"] == op["type"]) {
            for (const auto &[field, value] : op.items()) {
                (void)value;
                if (!branch["properties"].contains(field))
                    throw PlanError("unknown_field",
                                    "Field is not supported by this operation: " + field);
            }
            auto containsRequired = [&](const Json &rule) {
                for (const auto &field : rule["required"])
                    if (!op.contains(field.get<std::string>()))
                        return false;
                return true;
            };
            if (!containsRequired(branch))
                throw PlanError("invalid_operation", "Missing required operation parameter");
            for (const char *combination : {"anyOf", "oneOf"})
                if (branch.contains(combination)) {
                    std::size_t matches = 0;
                    for (const auto &rule : branch[combination])
                        if (containsRequired(rule))
                            ++matches;
                    if (matches == 0 || (std::string_view(combination) == "oneOf" && matches != 1))
                        throw PlanError("invalid_operation",
                                        "Specify the required operation parameters unambiguously");
                }
            return;
        }
    throw PlanError("invalid_operation", "Unsupported operation type");
}
} // namespace
Json editSchema() {
    auto result = Json::parse(R"({
      "$schema":"https://json-schema.org/draft/2020-12/schema",
      "type":"object", "additionalProperties":false,
      "required":["schema_version","selection","operations"],
      "properties":{
        "schema_version":{"const":1},
        "summary":{"type":"string","minLength":1,"maxLength":4096},
        "selection":{"type":"object","additionalProperties":false,
          "required":["type","expected_count"],
          "oneOf":[{"required":["names"]},{"required":["indices"]}],
          "properties":{
            "type":{"const":"material"},
            "names":{"type":"array","minItems":1,"maxItems":256,"items":{"type":"string"}},
            "indices":{"type":"array","minItems":1,"maxItems":256,"uniqueItems":true,"items":{"type":"integer","minimum":0}},
            "match":{"enum":["exact","glob"]},
            "expected_count":{"type":"integer","minimum":1}}},
        "operations":{"type":"array","minItems":1,"maxItems":8},
        "guards":{"type":"object","additionalProperties":false,"properties":{}},
        "preconditions":{"type":"object","additionalProperties":false,"properties":{
          "sha256":{"type":"string","pattern":"^[0-9a-f]{64}$"},
          "output_sha256":{"type":"string","pattern":"^[0-9a-f]{64}$"},
          "revision":{"type":"integer","minimum":0}}}
      }
    })");
    result["properties"]["operations"]["items"] = operationSchema();
    for (const char *name :
         {"preserve_unselected", "preserve_uv", "preserve_weights", "preserve_morphs"})
        result["properties"]["guards"]["properties"][name] = {{"type", "boolean"},
                                                              {"default", true}};
    return result;
}
PreparedEdit prepareEdit(const DocumentSession &source, const Json &root, std::string_view sha,
                         std::string_view outputSha) {
    if (!source.document.validate().valid())
        throw PlanError("invalid_model", "Source model failed validation");
    keys(root, {"schema_version", "summary", "selection", "operations", "guards", "preconditions"});
    if (!root.contains("schema_version") || !root["schema_version"].is_number_integer() ||
        root["schema_version"] != 1)
        throw PlanError("unsupported_version", "Expected schema_version 1");
    if (root.contains("preconditions")) {
        const auto &p = root.at("preconditions");
        keys(p, {"sha256", "output_sha256", "revision"});
        for (const char *field : {"sha256", "output_sha256"})
            if (p.contains(field)) {
                if (!p[field].is_string())
                    throw PlanError("invalid_plan", "SHA-256 must be a string");
                const auto hash = p[field].get<std::string>();
                if (hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                    }))
                    throw PlanError("invalid_plan",
                                    "SHA-256 must contain 64 lowercase hexadecimal digits");
            }
        if (p.contains("sha256") && (sha.empty() || p.at("sha256") != sha))
            throw PlanError("stale_source", "Source SHA-256 does not match");
        if (p.contains("output_sha256") &&
            (outputSha.empty() || p.at("output_sha256") != outputSha))
            throw PlanError("stale_output", "Output SHA-256 does not match");
        if (p.contains("revision") &&
            (!p["revision"].is_number_unsigned() && !p["revision"].is_number_integer()))
            throw PlanError("invalid_plan", "Revision must be an integer");
        if (p.contains("revision") && p.at("revision") < 0)
            throw PlanError("invalid_plan", "Revision must be nonnegative");
        if (p.contains("revision") && p.at("revision") != source.revision)
            throw PlanError("stale_revision", "Document revision does not match");
    }
    const auto &selection = root.at("selection");
    keys(selection, {"type", "names", "indices", "match", "expected_count"});
    if (selection.at("type") != "material")
        throw PlanError("invalid_selection", "Editing currently selects complete materials");
    const auto match = selection.value("match", std::string("exact"));
    if (match != "exact" && match != "glob")
        throw PlanError("invalid_selection", "Expected exact or glob matching");
    std::set<std::size_t> selected;
    if (selection.contains("names") == selection.contains("indices"))
        throw PlanError("invalid_selection", "Specify exactly one of names or indices");
    if (selection.contains("indices")) {
        const auto &indices = selection.at("indices");
        if (!indices.is_array() || indices.empty() || indices.size() > 256 ||
            selection.contains("match"))
            throw PlanError("invalid_selection",
                            "Expected 1 to 256 explicit indices without match");
        for (const auto &i : indices) {
            if (!i.is_number_integer() || i < 0 || i >= source.document.model().materials.size())
                throw PlanError("invalid_selection", "Material index is out of range");
            if (!selected.insert(i.get<std::size_t>()).second)
                throw PlanError("invalid_selection", "Duplicate material index");
        }
    } else {
        const auto &names = selection.at("names");
        if (!names.is_array() || names.empty() || names.size() > 256)
            throw PlanError("invalid_selection", "Expected 1 to 256 names");
        for (const auto &name : names) {
            const auto pattern = name.get<std::string>();
            for (std::size_t i = 0; i < source.document.model().materials.size(); ++i) {
                const auto &m = source.document.model().materials[i];
                if (match == "exact"
                        ? (m.name == pattern || m.englishName == pattern)
                        : (globMatch(pattern, m.name) || globMatch(pattern, m.englishName)))
                    selected.insert(i);
            }
        }
    }
    Json candidates = Json::array();
    for (auto i : selected)
        candidates.push_back({{"index", i}, {"name", source.document.model().materials[i].name}});
    if (!selection.at("expected_count").is_number_integer() || selection.at("expected_count") < 1 ||
        selection.at("expected_count") != selected.size())
        throw PlanError("selection_count_mismatch", "Selection count differs from expected_count",
                        {{"candidates", candidates}, {"actual_count", selected.size()}});
    Json guards = root.value("guards", Json::object());
    keys(guards, {"preserve_unselected", "preserve_uv", "preserve_weights", "preserve_morphs"});
    for (const auto &[k, v] : guards.items())
        if (!v.is_boolean())
            throw PlanError("invalid_guard", "Guard must be boolean: " + k);
    auto legacy = Json{{"version", 1},
                       {"summary", root.value("summary", std::string("Structured model edit"))},
                       {"operations", Json::array()}};
    const auto &ops = root.at("operations");
    if (!ops.is_array() || ops.empty() || ops.size() > 8)
        throw PlanError("invalid_plan", "Expected 1 to 8 operations");
    for (auto op : ops) {
        validateOperation(op);
        Json full = {
            {"type", op.at("type")},    {"shape", "none"},    {"color", {0.1, 0.1, 0.1, 1}},
            {"translation", {0, 0, 0}}, {"scale", {1, 1, 1}}, {"rotation", {0, 0, 0}},
            {"replace", true},          {"detail", 24}};
        if (op.contains("strength")) {
            if (op.at("type") != "smooth" || op.contains("amount"))
                throw PlanError("invalid_plan", "strength is only an alias for smooth amount");
            op["amount"] = op.at("strength");
            op.erase("strength");
        }
        if (op.contains("pivot") && op["pivot"].is_string()) {
            const auto pivot = op["pivot"].get<std::string>();
            if (pivot != "center" && pivot != "top-center" && pivot != "bottom-center")
                throw PlanError("invalid_plan", "Unknown pivot");
            op["pivot"] = {
                0.5, pivot == "top-center" ? 1.0 : (pivot == "bottom-center" ? 0.0 : 0.5), 0.5};
        }
        full.update(op);
        legacy["operations"].push_back(std::move(full));
    }
    ai::EditPlan plan;
    try {
        plan = ai::parsePlan(legacy.dump());
    } catch (const std::exception &e) {
        throw PlanError("invalid_operation", e.what());
    }
    const auto faces = materialFaces(source.document.model(), {selected.begin(), selected.end()});
    if (faces.empty())
        throw PlanError("empty_selection", "Selected materials contain no faces");
    auto region = ai::captureFaces(source, faces);
    ai::EditTrace trace;
    std::unique_ptr<DocumentSession> preview;
    try {
        preview = ai::makeEditPreview(source, region, plan, &trace);
    } catch (const std::exception &e) {
        throw PlanError("edit_failed", e.what());
    }
    const auto &before = source.document.model(), &after = preview->document.model();
    const bool sameTopology = sameConnectivity(source.document, preview->document);
    validateEditGuards(source, *preview, region, guards, trace);
    std::size_t moved = 0;
    double maxDistance = 0;
    for (std::size_t i = 0; i < std::min(before.vertices.size(), after.vertices.size()); ++i) {
        double d = 0;
        for (std::size_t a = 0; a < 3; ++a) {
            auto handle = source.document.vertexHandle(i);
            handle.domain = preview->document.domain();
            const auto *vertex = preview->document.resolve(handle);
            if (!vertex)
                continue;
            const double x =
                static_cast<double>(vertex->position[a]) - before.vertices[i].position[a];
            d += x * x;
        }
        if (d > 0)
            ++moved;
        maxDistance = std::max(maxDistance, std::sqrt(d));
    }
    Json changedMaterials = Json::array();
    for (std::size_t i = 0; i < after.materials.size(); ++i)
        if (i >= before.materials.size() || before.materials[i] != after.materials[i])
            changedMaterials.push_back(i);
    Json report = {{"topology_changed", !sameTopology},
                   {"changed_material_indices", changedMaterials},
                   {"morphs_changed", before.morphs != after.morphs},
                   {"schema_version", 1},
                   {"valid", true},
                   {"source_sha256", sha},
                   {"revision", source.revision},
                   {"selection", candidates},
                   {"faces", faces.size()},
                   {"vertices_before", before.vertices.size()},
                   {"vertices_after", after.vertices.size()},
                   {"moved_vertices", moved},
                   {"maximum_displacement", maxDistance},
                   {"undo_entries", 1}};
    return {std::move(region), std::move(preview), std::move(report)};
}
} // namespace pmxer::editing
