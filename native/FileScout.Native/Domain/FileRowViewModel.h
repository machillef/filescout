#pragma once
#include "FileRowViewModel.g.h"
#include "Domain/ShellItemRef.h"

namespace winrt::FileScoutNative::implementation
{
    struct FileRowViewModel : FileRowViewModelT<FileRowViewModel>
    {
        FileRowViewModel() = default;

        // INotifyPropertyChanged
        winrt::event_token PropertyChanged(
            winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // Property getters (IDL-exposed)
        winrt::hstring Name()      const noexcept { return m_name; }
        winrt::hstring TypeLabel() const noexcept { return m_typeLabel; }
        winrt::hstring SizeText()  const noexcept { return m_sizeText; }
        winrt::hstring DateText()  const noexcept { return m_dateText; }
        winrt::hstring DetailText()const noexcept { return m_detailText; }
        bool IsFolder()            const noexcept { return m_isFolder; }
        bool IsSelected()          const noexcept { return m_isSelected; }
        void IsSelected(bool value) noexcept;
        double SelectionOutlineOpacity() const noexcept { return m_isSelected ? 1.0 : 0.0; }
        double SelectionFillOpacity() const noexcept { return m_isSelected ? 0.28 : 0.0; }
        winrt::hstring GroupHeaderText() const noexcept { return m_groupHeaderText; }
        double GroupHeaderOpacity() const noexcept { return m_showGroupHeader ? 1.0 : 0.0; }
        double GroupHeaderHeight() const noexcept { return m_showGroupHeader ? 42.0 : 0.0; }

        winrt::Microsoft::UI::Xaml::Media::ImageSource IconSource() const noexcept
        {
            return m_iconSource;
        }

        // IconSource setter fires PropertyChanged so bound Image updates in-place.
        void IconSource(winrt::Microsoft::UI::Xaml::Media::ImageSource const& value);

        // Internal setters — not in IDL; used by MakeFileRowViewModel factory.
        void SetName(std::wstring_view value)       { m_name       = value; }
        void SetTypeLabel(std::wstring_view value)  { m_typeLabel  = value; }
        void SetSizeText(std::wstring_view value)   { m_sizeText   = value; }
        void SetDateText(std::wstring_view value)   { m_dateText   = value; }
        void SetDetailText(std::wstring_view value) { m_detailText = value; }
        void SetIsFolder(bool value) noexcept       { m_isFolder   = value; }
        void SetGroupHeader(std::wstring_view label, bool showHeader) noexcept
        {
            m_groupHeaderText = label;
            m_showGroupHeader = showHeader;
        }
        void RaisePropertyChanged(std::wstring_view propertyName);

    private:
        winrt::hstring m_name;
        winrt::hstring m_typeLabel;
        winrt::hstring m_sizeText;
        winrt::hstring m_dateText;
        winrt::hstring m_detailText;
        winrt::hstring m_groupHeaderText;
        bool m_isFolder{ false };
        bool m_isSelected{ false };
        bool m_showGroupHeader{ false };
        winrt::Microsoft::UI::Xaml::Media::ImageSource m_iconSource{ nullptr };
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}

namespace winrt::FileScoutNative::factory_implementation
{
    struct FileRowViewModel
        : FileRowViewModelT<FileRowViewModel, implementation::FileRowViewModel>
    {
    };
}

// Free function: construct a FileRowViewModel from a ShellItemRef.
// Not part of the IDL — used by MainWindow row-creation code (P3).
winrt::FileScoutNative::FileRowViewModel MakeFileRowViewModel(
    ::FileScoutNative::Domain::ShellItemRef const& item);
