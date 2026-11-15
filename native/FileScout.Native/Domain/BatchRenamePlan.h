#pragma once

#include <cwctype>
#include <unordered_set>

#include "FileNameValidation.h"
#include "ShellItemRef.h"

namespace FileScoutNative::Domain
{
    struct BatchRenamePlanEntry
    {
        ShellItemRef source_item;
        std::wstring target_name;
        std::filesystem::path target_path;
    };

    struct BatchRenamePlan
    {
        std::vector<BatchRenamePlanEntry> entries;
        std::optional<std::wstring> error;
    };

    struct BatchRenameOptions
    {
        std::wstring base_name;
        uint32_t start_number{ 1 };
        uint32_t padding_width{ 0 };
    };

    enum class BatchRenameCaseTransform
    {
        Lowercase = 0,
        Uppercase = 1,
        TitleCase = 2,
    };

    // The batch-rename dialog's case combo lists the transforms in enum order and opens on Title
    // Case. Any other index (-1 = nothing selected) gets Title Case too, the same fallback
    // detail::ApplyCaseTransform uses for an unknown transform.
    [[nodiscard]] inline BatchRenameCaseTransform BatchRenameCaseTransformFromIndex(
        int32_t const selectedIndex) noexcept
    {
        switch (selectedIndex)
        {
        case 0:
            return BatchRenameCaseTransform::Lowercase;
        case 1:
            return BatchRenameCaseTransform::Uppercase;
        case 2:
        default:
            return BatchRenameCaseTransform::TitleCase;
        }
    }

    namespace detail
    {
        inline std::wstring ToLowerInvariant(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t const ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            return value;
        }

        inline std::wstring ToUpperInvariant(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t const ch) { return static_cast<wchar_t>(std::towupper(ch)); });
            return value;
        }

        inline std::wstring BuildBatchRenameNumber(std::size_t const value, std::size_t const width)
        {
            auto number = std::to_wstring(value);
            if (number.size() >= width)
            {
                return number;
            }

            return std::wstring(width - number.size(), L'0') + number;
        }

        inline std::unordered_set<std::wstring> BuildSelectedPathSet(
            std::span<ShellItemRef const> const selectedItems)
        {
            std::unordered_set<std::wstring> selectedPaths;
            for (auto const& item : selectedItems)
            {
                selectedPaths.insert(ToLowerInvariant(item.path.lexically_normal().wstring()));
            }

            return selectedPaths;
        }

        inline std::unordered_set<std::wstring> BuildExistingNameSet(
            std::span<ShellItemRef const> const selectedItems,
            std::span<ShellItemRef const> const siblingItems)
        {
            auto const selectedPaths = BuildSelectedPathSet(selectedItems);

            std::unordered_set<std::wstring> existingNames;
            for (auto const& item : siblingItems)
            {
                auto const normalizedPath = ToLowerInvariant(item.path.lexically_normal().wstring());
                if (!selectedPaths.contains(normalizedPath))
                {
                    existingNames.insert(ToLowerInvariant(item.display_name));
                }
            }

            return existingNames;
        }

        inline bool TryAppendPlanEntry(
            BatchRenamePlan& plan,
            ShellItemRef const& item,
            std::wstring targetName,
            std::unordered_set<std::wstring>& generatedNames,
            std::unordered_set<std::wstring> const& existingNames)
        {
            if (auto const error = ValidateWindowsFileName(targetName); error.has_value())
            {
                plan.error = *error;
                plan.entries.clear();
                return false;
            }

            auto const targetKey = detail::ToLowerInvariant(targetName);
            if (!generatedNames.insert(targetKey).second)
            {
                plan.error = L"Batch rename generated duplicate names.";
                plan.entries.clear();
                return false;
            }

            if (existingNames.contains(targetKey))
            {
                plan.error = L"Batch rename would overwrite an existing sibling item.";
                plan.entries.clear();
                return false;
            }

            auto const targetPath = item.path.parent_path() / targetName;
            plan.entries.push_back(BatchRenamePlanEntry{
                .source_item = item,
                .target_name = std::move(targetName),
                .target_path = std::move(targetPath),
            });
            return true;
        }

        inline std::wstring BuildAffixRenameTargetName(
            ShellItemRef const& item,
            std::wstring const& affix,
            bool const prefixMode)
        {
            if (item.is_folder)
            {
                return prefixMode
                    ? affix + item.display_name
                    : item.display_name + affix;
            }

            auto const stem = item.path.stem().wstring();
            auto const extension = item.path.extension().wstring();
            return prefixMode
                ? affix + stem + extension
                : stem + affix + extension;
        }

        inline std::wstring ReplaceAllLiteralOccurrences(
            std::wstring value,
            std::wstring const& findText,
            std::wstring const& replaceText)
        {
            if (findText.empty())
            {
                return value;
            }

            std::size_t searchFrom = 0;
            while ((searchFrom = value.find(findText, searchFrom)) != std::wstring::npos)
            {
                value.replace(searchFrom, findText.size(), replaceText);
                searchFrom += replaceText.size();
            }

            return value;
        }

        inline std::wstring BuildFindReplaceTargetName(
            ShellItemRef const& item,
            std::wstring const& findText,
            std::wstring const& replaceText)
        {
            if (item.is_folder)
            {
                return ReplaceAllLiteralOccurrences(item.display_name, findText, replaceText);
            }

            auto const stem = ReplaceAllLiteralOccurrences(item.path.stem().wstring(), findText, replaceText);
            return stem + item.path.extension().wstring();
        }

        inline std::wstring ToTitleCaseInvariant(std::wstring value)
        {
            bool capitalizeNext = true;
            for (auto& ch : value)
            {
                if (std::iswalnum(ch) != 0)
                {
                    ch = capitalizeNext
                        ? static_cast<wchar_t>(std::towupper(ch))
                        : static_cast<wchar_t>(std::towlower(ch));
                    capitalizeNext = false;
                    continue;
                }

                capitalizeNext = true;
            }

            return value;
        }

        inline std::wstring ApplyCaseTransform(
            std::wstring value,
            BatchRenameCaseTransform const transform)
        {
            switch (transform)
            {
            case BatchRenameCaseTransform::Lowercase:
                return ToLowerInvariant(std::move(value));
            case BatchRenameCaseTransform::Uppercase:
                return ToUpperInvariant(std::move(value));
            case BatchRenameCaseTransform::TitleCase:
            default:
                return ToTitleCaseInvariant(std::move(value));
            }
        }

        inline std::wstring BuildCaseTransformTargetName(
            ShellItemRef const& item,
            BatchRenameCaseTransform const transform)
        {
            if (item.is_folder)
            {
                return ApplyCaseTransform(item.display_name, transform);
            }

            auto const stem = ApplyCaseTransform(item.path.stem().wstring(), transform);
            return stem + item.path.extension().wstring();
        }
    }

    inline BatchRenamePlan BuildSequentialBatchRenamePlan(
        std::span<ShellItemRef const> const selectedItems,
        std::span<ShellItemRef const> const siblingItems,
        BatchRenameOptions const& options)
    {
        BatchRenamePlan plan;
        if (selectedItems.empty())
        {
            plan.error = L"Batch rename needs at least one selected item.";
            return plan;
        }

        if (options.base_name.empty())
        {
            plan.error = L"Batch rename needs a base name.";
            return plan;
        }

        if (options.start_number == 0)
        {
            plan.error = L"Batch rename start number must be at least 1.";
            return plan;
        }

        auto const existingNames = detail::BuildExistingNameSet(selectedItems, siblingItems);
        std::unordered_set<std::wstring> generatedNames;
        auto const lastNumber =
            static_cast<std::size_t>(options.start_number) + selectedItems.size() - 1;
        auto const digits =
            options.padding_width == 0
                ? std::max<std::size_t>(2, std::to_wstring(lastNumber).size())
                : static_cast<std::size_t>(options.padding_width);

        plan.entries.reserve(selectedItems.size());
        for (std::size_t index = 0; index < selectedItems.size(); ++index)
        {
            auto const& item = selectedItems[index];
            auto targetName =
                options.base_name + L" " +
                detail::BuildBatchRenameNumber(static_cast<std::size_t>(options.start_number) + index, digits);
            if (!item.is_folder)
            {
                targetName += item.path.extension().wstring();
            }

            if (!detail::TryAppendPlanEntry(
                    plan,
                    item,
                    std::move(targetName),
                    generatedNames,
                    existingNames))
            {
                return plan;
            }
        }

        return plan;
    }

    inline BatchRenamePlan BuildSequentialBatchRenamePlan(
        std::span<ShellItemRef const> const selectedItems,
        std::span<ShellItemRef const> const siblingItems,
        std::wstring const& baseName)
    {
        return BuildSequentialBatchRenamePlan(
            selectedItems,
            siblingItems,
            BatchRenameOptions{
                .base_name = baseName,
            });
    }

    inline BatchRenamePlan BuildPrefixBatchRenamePlan(
        std::span<ShellItemRef const> const selectedItems,
        std::span<ShellItemRef const> const siblingItems,
        std::wstring const& prefix)
    {
        BatchRenamePlan plan;
        if (selectedItems.empty())
        {
            plan.error = L"Batch rename needs at least one selected item.";
            return plan;
        }

        if (prefix.empty())
        {
            plan.error = L"Batch rename prefix cannot be empty.";
            return plan;
        }

        auto const existingNames = detail::BuildExistingNameSet(selectedItems, siblingItems);
        std::unordered_set<std::wstring> generatedNames;
        plan.entries.reserve(selectedItems.size());
        for (auto const& item : selectedItems)
        {
            auto targetName = detail::BuildAffixRenameTargetName(item, prefix, true);
            if (!detail::TryAppendPlanEntry(
                    plan,
                    item,
                    std::move(targetName),
                    generatedNames,
                    existingNames))
            {
                return plan;
            }
        }

        return plan;
    }

    inline BatchRenamePlan BuildSuffixBatchRenamePlan(
        std::span<ShellItemRef const> const selectedItems,
        std::span<ShellItemRef const> const siblingItems,
        std::wstring const& suffix)
    {
        BatchRenamePlan plan;
        if (selectedItems.empty())
        {
            plan.error = L"Batch rename needs at least one selected item.";
            return plan;
        }

        if (suffix.empty())
        {
            plan.error = L"Batch rename suffix cannot be empty.";
            return plan;
        }

        auto const existingNames = detail::BuildExistingNameSet(selectedItems, siblingItems);
        std::unordered_set<std::wstring> generatedNames;
        plan.entries.reserve(selectedItems.size());
        for (auto const& item : selectedItems)
        {
            auto targetName = detail::BuildAffixRenameTargetName(item, suffix, false);
            if (!detail::TryAppendPlanEntry(
                    plan,
                    item,
                    std::move(targetName),
                    generatedNames,
                    existingNames))
            {
                return plan;
            }
        }

        return plan;
    }

    inline BatchRenamePlan BuildFindReplaceBatchRenamePlan(
        std::span<ShellItemRef const> const selectedItems,
        std::span<ShellItemRef const> const siblingItems,
        std::wstring const& findText,
        std::wstring const& replaceText)
    {
        BatchRenamePlan plan;
        if (selectedItems.empty())
        {
            plan.error = L"Batch rename needs at least one selected item.";
            return plan;
        }

        if (findText.empty())
        {
            plan.error = L"Find text cannot be empty.";
            return plan;
        }

        auto const existingNames = detail::BuildExistingNameSet(selectedItems, siblingItems);
        std::unordered_set<std::wstring> generatedNames;
        bool anyChanged = false;
        plan.entries.reserve(selectedItems.size());
        for (auto const& item : selectedItems)
        {
            auto targetName = detail::BuildFindReplaceTargetName(item, findText, replaceText);
            anyChanged = anyChanged || (targetName != item.display_name);
            if (!detail::TryAppendPlanEntry(
                    plan,
                    item,
                    std::move(targetName),
                    generatedNames,
                    existingNames))
            {
                return plan;
            }
        }

        if (!anyChanged)
        {
            plan.error = L"Find and replace did not change any selected item names.";
            plan.entries.clear();
        }

        return plan;
    }

    inline BatchRenamePlan BuildCaseTransformBatchRenamePlan(
        std::span<ShellItemRef const> const selectedItems,
        std::span<ShellItemRef const> const siblingItems,
        BatchRenameCaseTransform const transform)
    {
        BatchRenamePlan plan;
        if (selectedItems.empty())
        {
            plan.error = L"Batch rename needs at least one selected item.";
            return plan;
        }

        auto const existingNames = detail::BuildExistingNameSet(selectedItems, siblingItems);
        std::unordered_set<std::wstring> generatedNames;
        bool anyChanged = false;
        plan.entries.reserve(selectedItems.size());
        for (auto const& item : selectedItems)
        {
            auto targetName = detail::BuildCaseTransformTargetName(item, transform);
            anyChanged = anyChanged || (targetName != item.display_name);
            if (!detail::TryAppendPlanEntry(
                    plan,
                    item,
                    std::move(targetName),
                    generatedNames,
                    existingNames))
            {
                return plan;
            }
        }

        if (!anyChanged)
        {
            plan.error = L"Case transform did not change any selected item names.";
            plan.entries.clear();
        }

        return plan;
    }
}
