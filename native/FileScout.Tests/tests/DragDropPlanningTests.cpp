#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/DragDropPlanning.h"

using namespace FileScoutNative::Domain;

TEST(DragDropPlanning, ExternalStorageUsesDeferredProvider)
{
    EXPECT_EQ(
        ResolveExternalDragStorageResolutionMode(),
        DragStorageResolutionMode::DeferredProvider);
}

TEST(DragDropPlanning, NonLocalFileStagesForExternalTargets)
{
    EXPECT_TRUE(ShouldStageExternalDragFile(
        std::filesystem::path{ LR"(\\wsl.localhost\Ubuntu\home\achmo\designs\mock.excalidraw)" },
        false,
        [](std::filesystem::path const&) { return false; }));
}

TEST(DragDropPlanning, LocalFileDoesNotStageForExternalTargets)
{
    EXPECT_FALSE(ShouldStageExternalDragFile(
        std::filesystem::path{ LR"(C:\Users\machi\Downloads\mock.excalidraw)" },
        false,
        [](std::filesystem::path const&) { return true; }));
}

TEST(DragDropPlanning, FoldersDoNotStageForExternalTargets)
{
    EXPECT_FALSE(ShouldStageExternalDragFile(
        std::filesystem::path{ LR"(\\wsl.localhost\Ubuntu\home\achmo\designs)" },
        true,
        [](std::filesystem::path const&) { return false; }));
}
