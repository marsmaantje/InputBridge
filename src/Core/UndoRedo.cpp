#include "UndoRedo.h"
#include <algorithm>

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
    m_undoStack.push(std::move(command));
    
    // Clear redo stack when new command is executed
    while (!m_redoStack.empty()) {
        m_redoStack.pop();
    }
    
    // Trim undo stack if it exceeds max size
    TrimUndoStack();
}

bool UndoRedoManager::Undo() {
    if (m_undoStack.empty()) {
        return false;
    }
    
    // Get command from undo stack
    std::unique_ptr<ICommand> command = std::move(m_undoStack.top());
    m_undoStack.pop();
    
    // Undo the command
    command->Undo();
    
    // Move to redo stack
    m_redoStack.push(std::move(command));
    
    return true;
}

bool UndoRedoManager::Redo() {
    if (m_redoStack.empty()) {
        return false;
    }
    
    // Get command from redo stack
    std::unique_ptr<ICommand> command = std::move(m_redoStack.top());
    m_redoStack.pop();
    
    // Re-execute the command
    command->Execute();
    
    // Move back to undo stack
    m_undoStack.push(std::move(command));
    
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
    return m_undoStack.top()->GetDescription();
}

std::string UndoRedoManager::GetRedoDescription() const {
    if (m_redoStack.empty()) {
        return "";
    }
    return m_redoStack.top()->GetDescription();
}

void UndoRedoManager::Clear() {
    while (!m_undoStack.empty()) {
        m_undoStack.pop();
    }
    while (!m_redoStack.empty()) {
        m_redoStack.pop();
    }
}

size_t UndoRedoManager::GetUndoCount() const {
    return m_undoStack.size();
}

size_t UndoRedoManager::GetRedoCount() const {
    return m_redoStack.size();
}

void UndoRedoManager::TrimUndoStack() {
    if (m_undoStack.size() <= m_maxHistorySize) {
        return;
    }
    
    // Create a temporary stack to reverse order
    std::stack<std::unique_ptr<ICommand>> tempStack;
    
    // Move commands to temp stack (reversing order)
    while (!m_undoStack.empty()) {
        tempStack.push(std::move(m_undoStack.top()));
        m_undoStack.pop();
    }
    
    // Remove oldest commands
    size_t toRemove = tempStack.size() - m_maxHistorySize;
    for (size_t i = 0; i < toRemove; ++i) {
        tempStack.pop();
    }
    
    // Move back to undo stack (restoring order)
    while (!tempStack.empty()) {
        m_undoStack.push(std::move(tempStack.top()));
        tempStack.pop();
    }
}
