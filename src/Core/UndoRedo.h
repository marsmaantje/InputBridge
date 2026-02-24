#pragma once

#include <memory>
#include <stack>
#include <string>
#include <functional>

/**
 * Command interface for undo/redo operations.
 * 
 * Uses the Command pattern to encapsulate operations that can be undone.
 * Each command stores the state needed to undo and redo the operation.
 */
class ICommand {
public:
    virtual ~ICommand() = default;
    
    /**
     * Execute the command.
     */
    virtual void Execute() = 0;
    
    /**
     * Undo the command, restoring previous state.
     */
    virtual void Undo() = 0;
    
    /**
     * Get a human-readable description of this command.
     */
    virtual std::string GetDescription() const = 0;
};

/**
 * Generic lambda-based command for simple operations.
 * 
 * Allows creating commands from lambdas without defining new classes.
 */
class LambdaCommand : public ICommand {
public:
    /**
     * Create a command from execute and undo functions.
     * 
     * @param description Human-readable command description
     * @param executeFunc Function to execute the command
     * @param undoFunc Function to undo the command
     */
    LambdaCommand(const std::string& description,
                  std::function<void()> executeFunc,
                  std::function<void()> undoFunc)
        : m_description(description)
        , m_executeFunc(std::move(executeFunc))
        , m_undoFunc(std::move(undoFunc))
    {}
    
    void Execute() override {
        if (m_executeFunc) {
            m_executeFunc();
        }
    }
    
    void Undo() override {
        if (m_undoFunc) {
            m_undoFunc();
        }
    }
    
    std::string GetDescription() const override {
        return m_description;
    }
    
private:
    std::string m_description;
    std::function<void()> m_executeFunc;
    std::function<void()> m_undoFunc;
};

/**
 * Manages undo/redo history with configurable stack size.
 * 
 * RAII-based with automatic memory management.
 * Thread-safe for single-threaded UI operations.
 */
class UndoRedoManager {
public:
    /**
     * Create undo/redo manager with maximum history size.
     * 
     * @param maxHistorySize Maximum number of undo operations (default: 50)
     */
    explicit UndoRedoManager(size_t maxHistorySize = 50);
    
    /**
     * Execute a command and add it to history.
     * Clears redo stack.
     * 
     * @param command Command to execute
     */
    void ExecuteCommand(std::unique_ptr<ICommand> command);
    
    /**
     * Undo the last command.
     * @return true if undo was performed, false if nothing to undo
     */
    bool Undo();
    
    /**
     * Redo the last undone command.
     * @return true if redo was performed, false if nothing to redo
     */
    bool Redo();
    
    /**
     * Check if undo is available.
     */
    bool CanUndo() const;
    
    /**
     * Check if redo is available.
     */
    bool CanRedo() const;
    
    /**
     * Get description of next undo operation.
     */
    std::string GetUndoDescription() const;
    
    /**
     * Get description of next redo operation.
     */
    std::string GetRedoDescription() const;
    
    /**
     * Clear all history.
     */
    void Clear();
    
    /**
     * Get number of operations in undo stack.
     */
    size_t GetUndoCount() const;
    
    /**
     * Get number of operations in redo stack.
     */
    size_t GetRedoCount() const;

private:
    std::stack<std::unique_ptr<ICommand>> m_undoStack;
    std::stack<std::unique_ptr<ICommand>> m_redoStack;
    size_t m_maxHistorySize;
    
    void TrimUndoStack();
};
