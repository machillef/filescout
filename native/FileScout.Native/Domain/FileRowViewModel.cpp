#include "pch.h"
#include "Domain/FileRowViewModel.h"
#include "Domain/RowFormatting.h"
#if __has_include("FileRowViewModel.g.cpp")
#include "FileRowViewModel.g.cpp"
#endif

namespace winrt::FileScoutNative::implementation
{
    void FileRowViewModel::RaisePropertyChanged(std::wstring_view propertyName)
    {
        if (!m_propertyChanged)
        {
            return;
        }

        m_propertyChanged(*this,
            winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{
                winrt::hstring{ propertyName } });
    }

    winrt::event_token FileRowViewModel::PropertyChanged(
        winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void FileRowViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void FileRowViewModel::IsSelected(bool value) noexcept
    {
        if (m_isSelected == value)
        {
            return;
        }

        m_isSelected = value;
        RaisePropertyChanged(L"IsSelected");
        RaisePropertyChanged(L"SelectionOutlineOpacity");
        RaisePropertyChanged(L"SelectionFillOpacity");
    }

    void FileRowViewModel::IsCut(bool value) noexcept
    {
        if (m_isCut == value)
        {
            return;
        }

        m_isCut = value;
        RaisePropertyChanged(L"IsCut");
        RaisePropertyChanged(L"CutOpacity");
    }

    void FileRowViewModel::IconSource(winrt::Microsoft::UI::Xaml::Media::ImageSource const& value)
    {
        m_iconSource = value;
        RaisePropertyChanged(L"IconSource");
    }
}

winrt::FileScoutNative::FileRowViewModel MakeFileRowViewModel(
    ::FileScoutNative::Domain::ShellItemRef const& item)
{
    auto vm = winrt::make_self<winrt::FileScoutNative::implementation::FileRowViewModel>();
    vm->SetName(item.display_name);
    vm->SetTypeLabel(item.kind_label);
    vm->SetSizeText(item.size_available
        ? ::FileScoutNative::Domain::FormatFileSize(item.size_bytes)
        : L"");
    vm->SetDateText(item.date_modified_available ? item.date_modified_str : L"");
    vm->SetDetailText(
        ::FileScoutNative::Domain::BuildDetailText(
            item.kind_label, item.size_bytes, item.size_available));
    vm->SetIsFolder(item.is_folder);
    return vm.as<winrt::FileScoutNative::FileRowViewModel>();
}
