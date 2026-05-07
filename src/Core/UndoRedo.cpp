#include "UndoRedo.h"

UndoRedoManager::UndoRedoManager(size_t maxHistorySize)
    : m_maxHistorySize(maxHistorySize)
{
}

void UndoRedoManager::ExecuteCommand(std::unique_ptr<ICommand> command) {
    if (!command) {
        return;
    }

    // Execute the command
    command->Execute();

    // Add to undo stack
    m_undoStack.push_back(std::move(command));

    // Clear redo stack when new command is executed
    m_redoStack.clear();

    // Trim undo stack if it exceeds max size
    TrimUndoStack();
}

bool UndoRedoManager::Undo() {
    if (m_undoStack.empty()) {
        return false;
    }

    // Get command from undo stack
    std::unique_ptr<ICommand> command = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    // Undo the command
    command->Undo();

    // Move to redo stack
    m_redoStack.push_back(std::move(command));

    return true;
}

bool UndoRedoManager::Redo() {
    if (m_redoStack.empty()) {
        return false;
    }

    // Get command from redo stack
    std::unique_ptr<ICommand> command = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    // Re-execute the command
    command->Execute();

    // Move back to undo stack
    m_undoStack.push_back(std::move(command));

    return true;
}

bool UndoRedoManager::CanUndo() const {
    return !m_undoStack.empty();
}

bool UndoRedoManager::CanRedo() const {
    return !m_redoStack.empty();
}

std::string UndoRedoManager::GetUndoDescription() const {
    if (m_undoStack.empty()) {
        return "";
    }
    return m_undoStack.back()->GetDescription();
}

std::string UndoRedoManager::GetRedoDescription() const {
    if (m_redoStack.empty()) {
        return "";
    }
    return m_redoStack.back()->GetDescription();
}

void UndoRedoManager::Clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}

size_t UndoRedoManager::GetUndoCount() const {
    return m_undoStack.size();
}

size_t UndoRedoManager::GetRedoCount() const {
    return m_redoStack.size();
}

void UndoRedoManager::TrimUndoStack() {
    while (m_undoStack.size() > m_maxHistorySize) {
        m_undoStack.pop_front();
    }
}
