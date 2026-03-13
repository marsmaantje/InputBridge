#include <gtest/gtest.h>
#include "Core/UndoRedo.h"

#include <string>
#include <vector>

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Builds a LambdaCommand that appends to two vectors so tests can verify
/// the exact execution and undo sequence.
static std::unique_ptr<ICommand> MakeCmd(
    const std::string&    desc,
    std::vector<std::string>& execLog,
    std::vector<std::string>& undoLog)
{
    return std::make_unique<LambdaCommand>(
        desc,
        [desc, &execLog]() { execLog.push_back(desc + ":exec"); },
        [desc, &undoLog]() { undoLog.push_back(desc + ":undo"); }
    );
}

// ═════════════════════════════════════════════════════════════════════════════
// Initial state
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, InitiallyNothingToUndoOrRedo) {
    UndoRedoManager mgr;
    EXPECT_FALSE(mgr.CanUndo());
    EXPECT_FALSE(mgr.CanRedo());
    EXPECT_EQ(mgr.GetUndoCount(), 0u);
    EXPECT_EQ(mgr.GetRedoCount(), 0u);
}

TEST(UndoRedoManager, DescriptionsEmptyWhenStacksEmpty) {
    UndoRedoManager mgr;
    EXPECT_TRUE(mgr.GetUndoDescription().empty());
    EXPECT_TRUE(mgr.GetRedoDescription().empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ExecuteCommand
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, ExecuteRunsTheCommand) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    ASSERT_EQ(exec.size(), 1u);
    EXPECT_EQ(exec[0], "A:exec");
}

TEST(UndoRedoManager, ExecuteAddsToUndoStack) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    EXPECT_TRUE(mgr.CanUndo());
    EXPECT_EQ(mgr.GetUndoCount(), 1u);
}

TEST(UndoRedoManager, ExecuteDoesNotPopulateRedoStack) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    EXPECT_FALSE(mgr.CanRedo());
}

TEST(UndoRedoManager, ExecuteNullCommandIsIgnored) {
    UndoRedoManager mgr;
    mgr.ExecuteCommand(nullptr);   // must not crash
    EXPECT_FALSE(mgr.CanUndo());
}

TEST(UndoRedoManager, ExecuteClearsRedoStack) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.Undo();
    EXPECT_TRUE(mgr.CanRedo());

    // A new execute should wipe the redo stack
    mgr.ExecuteCommand(MakeCmd("B", exec, undo));
    EXPECT_FALSE(mgr.CanRedo());
}

// ═════════════════════════════════════════════════════════════════════════════
// Undo
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, UndoCallsUndoFunction) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.Undo();
    ASSERT_EQ(undo.size(), 1u);
    EXPECT_EQ(undo[0], "A:undo");
}

TEST(UndoRedoManager, UndoReturnsTrueWhenAvailable) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    EXPECT_TRUE(mgr.Undo());
}

TEST(UndoRedoManager, UndoReturnsFalseWhenEmpty) {
    UndoRedoManager mgr;
    EXPECT_FALSE(mgr.Undo());
}

TEST(UndoRedoManager, UndoMovesCommandToRedoStack) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.Undo();
    EXPECT_FALSE(mgr.CanUndo());
    EXPECT_TRUE(mgr.CanRedo());
    EXPECT_EQ(mgr.GetRedoCount(), 1u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Redo
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, RedoReExecutesCommand) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.Undo();
    exec.clear();
    mgr.Redo();
    ASSERT_EQ(exec.size(), 1u);
    EXPECT_EQ(exec[0], "A:exec");
}

TEST(UndoRedoManager, RedoReturnsTrueWhenAvailable) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.Undo();
    EXPECT_TRUE(mgr.Redo());
}

TEST(UndoRedoManager, RedoReturnsFalseWhenEmpty) {
    UndoRedoManager mgr;
    EXPECT_FALSE(mgr.Redo());
}

TEST(UndoRedoManager, RedoMovesCommandBackToUndoStack) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.Undo();
    mgr.Redo();
    EXPECT_TRUE(mgr.CanUndo());
    EXPECT_FALSE(mgr.CanRedo());
}

// ═════════════════════════════════════════════════════════════════════════════
// Descriptions
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, GetUndoDescriptionReturnsTopCommandDescription) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("First",  exec, undo));
    mgr.ExecuteCommand(MakeCmd("Second", exec, undo));
    EXPECT_EQ(mgr.GetUndoDescription(), "Second");
}

TEST(UndoRedoManager, GetRedoDescriptionReturnsTopRedoDescription) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.ExecuteCommand(MakeCmd("B", exec, undo));
    mgr.Undo();
    EXPECT_EQ(mgr.GetRedoDescription(), "B");
}

// ═════════════════════════════════════════════════════════════════════════════
// History limit
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, HistoryLimitTrimOldestCommands) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr(3);

    mgr.ExecuteCommand(MakeCmd("1", exec, undo));
    mgr.ExecuteCommand(MakeCmd("2", exec, undo));
    mgr.ExecuteCommand(MakeCmd("3", exec, undo));
    mgr.ExecuteCommand(MakeCmd("4", exec, undo));  // pushes out "1"

    EXPECT_EQ(mgr.GetUndoCount(), 3u);
    // The top of the stack should be the most recent command
    EXPECT_EQ(mgr.GetUndoDescription(), "4");
}

TEST(UndoRedoManager, CustomHistorySizeRespected) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr(1);

    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.ExecuteCommand(MakeCmd("B", exec, undo));  // trims "A"

    EXPECT_EQ(mgr.GetUndoCount(), 1u);
    EXPECT_EQ(mgr.GetUndoDescription(), "B");
}

// ═════════════════════════════════════════════════════════════════════════════
// Clear
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, ClearEmptiesBothStacks) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;
    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.ExecuteCommand(MakeCmd("B", exec, undo));
    mgr.Undo();
    // undo stack: 1, redo stack: 1
    mgr.Clear();
    EXPECT_FALSE(mgr.CanUndo());
    EXPECT_FALSE(mgr.CanRedo());
    EXPECT_EQ(mgr.GetUndoCount(), 0u);
    EXPECT_EQ(mgr.GetRedoCount(), 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Complex sequence
// ═════════════════════════════════════════════════════════════════════════════

TEST(UndoRedoManager, ComplexUndoRedoSequence) {
    std::vector<std::string> exec, undo;
    UndoRedoManager mgr;

    mgr.ExecuteCommand(MakeCmd("A", exec, undo));
    mgr.ExecuteCommand(MakeCmd("B", exec, undo));
    mgr.ExecuteCommand(MakeCmd("C", exec, undo));

    // Undo C, B
    mgr.Undo();  // undo C
    mgr.Undo();  // undo B
    EXPECT_EQ(mgr.GetUndoCount(), 1u);
    EXPECT_EQ(mgr.GetRedoCount(), 2u);

    // Redo B
    mgr.Redo();
    EXPECT_EQ(mgr.GetUndoDescription(), "B");
    EXPECT_EQ(mgr.GetRedoCount(), 1u);

    // New command D wipes the redo stack (C is lost)
    mgr.ExecuteCommand(MakeCmd("D", exec, undo));
    EXPECT_FALSE(mgr.CanRedo());
    EXPECT_EQ(mgr.GetUndoDescription(), "D");
    EXPECT_EQ(mgr.GetUndoCount(), 3u);  // A, B, D
}

// ═════════════════════════════════════════════════════════════════════════════
// LambdaCommand
// ═════════════════════════════════════════════════════════════════════════════

TEST(LambdaCommand, GetDescriptionReturnsSuppliedString) {
    auto cmd = std::make_unique<LambdaCommand>(
        "rename field",
        []() {},
        []() {}
    );
    EXPECT_EQ(cmd->GetDescription(), "rename field");
}

TEST(LambdaCommand, NullFunctorsDoNotCrash) {
    // Passing null std::function objects — Execute/Undo must guard against them.
    LambdaCommand cmd("safe", nullptr, nullptr);
    EXPECT_NO_THROW(cmd.Execute());
    EXPECT_NO_THROW(cmd.Undo());
}
