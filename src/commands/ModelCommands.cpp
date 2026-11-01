#include "ModelCommands.hpp"
#include "editing/EditPipeline.hpp"
#include "editor/SaveController.hpp"
#include "platform/FileDigest.hpp"
#include "platform/Log.hpp"
#include "render/SoftwareRenderer.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <streambuf>
namespace pmxer {
namespace {
std::string pathUtf8(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
// libmmd diagnostics use cout. Keep the CLI JSON channel free of library
// progress messages; WARN and ERROR continue to use stderr.
class QuietLibraryOutput : private std::streambuf {
    std::streambuf *previous_{};
    int_type overflow(int_type c) override { return traits_type::not_eof(c); }

  public:
    explicit QuietLibraryOutput(bool quiet) {
        if (quiet)
            previous_ = std::cout.rdbuf(this);
    }
    ~QuietLibraryOutput() override {
        if (previous_)
            std::cout.rdbuf(previous_);
    }
};
} // namespace
int runModelCommand(const ModelCommand &c) {
    QuietLibraryOutput quiet(c.json);
    using Json = nlohmann::json;
    auto emit = [&](const Json &j) {
        std::puts(j.dump(c.json ? -1 : 2, ' ', false, Json::error_handler_t::replace).c_str());
    };
    try {
        if (c.schema) {
            emit(editing::editSchema());
            return 0;
        }
        const auto sha = sha256File(c.model);
        auto model = mmd::pmx::load(c.model);
        if (sha256File(c.model) != sha)
            throw editing::PlanError("stale_source", "Source changed during loading");
        if (c.action == "inspect" || c.action == "query") {
            editing::Query q;
            q.kind = c.kind;
            q.name = c.name;
            q.material = c.material;
            q.bone = c.bone;
            q.minimum = c.minimum;
            q.maximum = c.maximum;
            q.offset = c.offset;
            q.limit = c.limit;
            q.includeBones = c.includeBones;
            q.includeWeights = c.includeWeights;
            auto report = editing::inspect(model, q);
            report["source_sha256"] = sha;
            emit(report);
            return 0;
        }
        if (c.action == "render") {
#if PMXER_HAS_SOFTWARE_RENDERER
            SoftwareRenderOptions options;
            options.output = c.output;
            options.motion = c.motion;
            options.views = c.views;
            options.overlay = c.overlay;
            options.morphs = c.morphs;
            options.width = c.width;
            options.height = c.height;
            options.frame = c.frame;
            emit(renderSoftwareViews(model, options));
            return 0;
#else
            throw editing::PlanError("unsupported_backend",
                                     "This build disables software rendering");
#endif
        }
        if (!mmd::pmx::validate(model).valid())
            throw editing::PlanError("invalid_model", "Source model failed validation");
        std::ifstream file(c.plan, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot open plan");
        std::string text(65537, '\0');
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(file.gcount()));
        if (text.size() > 65536)
            throw editing::PlanError("invalid_plan", "Plan exceeds 64 KiB");
        DocumentSession session(std::move(model), c.model);
        const auto plan = Json::parse(text);
        std::string outputSha;
        const bool hasOutputHash =
            plan.contains("preconditions") && plan["preconditions"].contains("output_sha256");
        if (!c.output.empty()) {
            if (std::filesystem::weakly_canonical(c.output) ==
                    std::filesystem::weakly_canonical(c.model) ||
                (std::filesystem::exists(c.output) &&
                 std::filesystem::equivalent(c.output, c.model)))
                throw editing::PlanError("source_overwrite", "Output must differ from input");
            const bool exists = std::filesystem::exists(std::filesystem::symlink_status(c.output));
            if (exists && !c.overwrite)
                throw editing::PlanError("output_exists",
                                         "Output already exists; use --overwrite to replace it");
            if (exists && hasOutputHash)
                outputSha = sha256File(c.output);
        }
        auto prepared = editing::prepareEdit(session, plan, sha, outputSha);
        const bool write = c.action == "apply" && !c.dryRun;
        prepared.report["dry_run"] = !write;
        if (hasOutputHash)
            prepared.report["output_precondition_atomic"] = false;
        if (write) {
            if (std::filesystem::weakly_canonical(c.output) ==
                    std::filesystem::weakly_canonical(c.model) ||
                (std::filesystem::exists(c.output) &&
                 std::filesystem::equivalent(c.output, c.model)))
                throw editing::PlanError("source_overwrite", "Output must differ from input");
            if (sha256File(c.model) != sha)
                throw editing::PlanError("stale_source", "Source changed before commit");
            if (hasOutputHash && sha256File(c.output) != outputSha)
                throw editing::PlanError("stale_output", "Output changed before commit");
            const auto accepted =
                ai::acceptEditPreview(session, prepared.region, *prepared.preview);
            if (!accepted.success)
                throw editing::PlanError("apply_failed", accepted.message);
            SaveOptions options;
            options.quietSuccess = true;
            options.preserveRecoveryFiles = true;
            options.failIfDestinationExists = !c.overwrite;
            options.copyResources = c.copyResources;
            options.expectedDestinationSha256 = outputSha;
            const auto saved = saveDocument(session, c.output, options);
            if (!saved.success) {
                if (saved.destinationChanged)
                    throw editing::PlanError("stale_output", saved.message);
                if (!c.overwrite &&
                    std::filesystem::exists(std::filesystem::symlink_status(c.output)))
                    throw editing::PlanError("output_exists",
                                             "Output was created before publication");
                throw std::runtime_error(saved.message);
            }
            prepared.report["output"] = pathUtf8(c.output);
            prepared.report["resources"] = Json::array();
            for (const auto &resource : saved.resources)
                prepared.report["resources"].push_back(
                    {{"texture_index", resource.index},
                     {"source", pathUtf8(resource.source)},
                     {"destination", pathUtf8(resource.destination)},
                     {"exists", resource.exists},
                     {"copied", resource.copied}});
        }
        emit(prepared.report);
        return 0;
    } catch (const editing::PlanError &e) {
        emit({{"error", {{"code", e.code}, {"message", e.what()}, {"details", e.details}}}});
        log::error(e.what());
        return e.code == "invalid_model" ? 1 : 2;
    } catch (const Json::exception &e) {
        emit({{"error", {{"code", "invalid_plan"}, {"message", e.what()}}}});
        log::error(e.what());
        return 2;
    } catch (const std::exception &e) {
        emit({{"error", {{"code", "operation_failed"}, {"message", e.what()}}}});
        log::error(e.what());
        return 3;
    }
}
} // namespace pmxer
