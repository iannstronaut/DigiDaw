#pragma once

#include "../../domain/project/project.hpp"
#include "../../domain/common/result.hpp"
#include "../../app/ports/project_repository.hpp"
#include "../../app/usecases/undo_stack.hpp"
#include <string>
#include <memory>
#include <filesystem>

namespace digidaw::app {

class ProjectSession {
public:
    explicit ProjectSession(std::shared_ptr<IProjectRepository> repo)
        : repo_(std::move(repo)), project_(std::make_unique<domain::Project>("Untitled")),
          undo_stack_(std::make_unique<UndoStack>()) {}

    [[nodiscard]] domain::Project& project() noexcept { return *project_; }
    [[nodiscard]] const domain::Project& project() const noexcept { return *project_; }

    [[nodiscard]] UndoStack& undo_stack() noexcept { return *undo_stack_; }

    [[nodiscard]] const std::string& current_filepath() const noexcept { return current_filepath_; }

    void new_project(std::string name = "Untitled") {
        project_ = std::make_unique<domain::Project>(std::move(name));
        undo_stack_->clear();
        current_filepath_.clear();
    }

    domain::Result<void> open_project(const std::string& filepath) {
        if (!repo_) return domain::Result<void>(domain::ErrorCode::InvalidArgument);

        auto load_res = repo_->load(filepath);
        if (load_res.is_error()) {
            return domain::Result<void>(load_res.error());
        }

        project_ = std::make_unique<domain::Project>(std::move(load_res.value()));
        undo_stack_->clear();
        current_filepath_ = filepath;
        return domain::Result<void>::ok();
    }

    domain::Result<void> save_project(const std::string& filepath) {
        if (!repo_) return domain::Result<void>(domain::ErrorCode::InvalidArgument);

        auto save_res = repo_->save(*project_, filepath);
        if (save_res.is_ok()) {
            project_->mark_clean();
            current_filepath_ = filepath;
        }
        return save_res;
    }

    // Periodic autosave & crash recovery (DAW-DATA-006, 04-data-design §7)
    domain::Result<std::string> trigger_autosave() {
        if (!repo_) return domain::Result<std::string>(domain::ErrorCode::InvalidArgument);

        std::string base_path = current_filepath_.empty() ? "autosave_temp.odp" : current_filepath_;
        std::string bak_path = base_path + ".bak";

        // Rotate existing backup (e.g. .bak -> .bak2 -> .bak3)
        std::error_code ec;
        if (std::filesystem::exists(bak_path + ".2", ec)) {
            std::filesystem::rename(bak_path + ".2", bak_path + ".3", ec);
        }
        if (std::filesystem::exists(bak_path, ec)) {
            std::filesystem::rename(bak_path, bak_path + ".2", ec);
        }

        auto res = repo_->save(*project_, bak_path);
        if (res.is_error()) {
            return domain::Result<std::string>(res.error());
        }

        return domain::Result<std::string>(bak_path);
    }

    [[nodiscard]] static bool has_recovery_file(const std::string& original_filepath) {
        std::error_code ec;
        return std::filesystem::exists(original_filepath + ".bak", ec);
    }

    domain::Result<void> recover_project(const std::string& original_filepath) {
        const std::string bak_path = original_filepath + ".bak";
        auto res = open_project(bak_path);
        if (res.is_ok()) {
            // Restore original name
            current_filepath_ = original_filepath;
            project_->mark_dirty();
        }
        return res;
    }

private:
    std::shared_ptr<IProjectRepository> repo_;
    std::unique_ptr<domain::Project> project_;
    std::unique_ptr<UndoStack> undo_stack_;
    std::string current_filepath_{""};
};

} // namespace digidaw::app
