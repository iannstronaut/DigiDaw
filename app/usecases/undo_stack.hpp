#pragma once

#include <memory>
#include <vector>
#include <string>
#include <utility>

namespace digidaw::app {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    [[nodiscard]] virtual std::string label() const = 0;
    [[nodiscard]] virtual std::string merge_key() const { return ""; }
};

class UndoStack {
public:
    explicit UndoStack(size_t max_history = 100) : max_history_(max_history) {}

    void push_and_execute(std::unique_ptr<ICommand> cmd) {
        if (!cmd) return;

        // Check if command can be merged with top of stack (e.g. knob dragging)
        if (!cmd->merge_key().empty() && !undo_history_.empty()) {
            if (undo_history_.back()->merge_key() == cmd->merge_key()) {
                cmd->execute();
                // Replace top with newer executed command or maintain initial baseline
                undo_history_.pop_back();
                undo_history_.push_back(std::move(cmd));
                redo_history_.clear();
                return;
            }
        }

        cmd->execute();
        undo_history_.push_back(std::move(cmd));
        redo_history_.clear();

        // Enforce history cap
        if (undo_history_.size() > max_history_) {
            undo_history_.erase(undo_history_.begin());
        }
    }

    bool undo() {
        if (undo_history_.empty()) return false;
        auto cmd = std::move(undo_history_.back());
        undo_history_.pop_back();
        cmd->undo();
        redo_history_.push_back(std::move(cmd));
        return true;
    }

    bool redo() {
        if (redo_history_.empty()) return false;
        auto cmd = std::move(redo_history_.back());
        redo_history_.pop_back();
        cmd->execute();
        undo_history_.push_back(std::move(cmd));
        return true;
    }

    [[nodiscard]] bool can_undo() const noexcept { return !undo_history_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_history_.empty(); }

    [[nodiscard]] std::string last_undo_label() const {
        if (!undo_history_.empty()) {
            return undo_history_.back()->label();
        }
        return "";
    }

    [[nodiscard]] std::string last_redo_label() const {
        if (!redo_history_.empty()) {
            return redo_history_.back()->label();
        }
        return "";
    }

    void clear() noexcept {
        undo_history_.clear();
        redo_history_.clear();
    }

private:
    size_t max_history_{100};
    std::vector<std::unique_ptr<ICommand>> undo_history_;
    std::vector<std::unique_ptr<ICommand>> redo_history_;
};

} // namespace digidaw::app
