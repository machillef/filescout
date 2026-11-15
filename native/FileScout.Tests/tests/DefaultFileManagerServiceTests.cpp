#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/DefaultFileManagerService.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Shell::detail;

namespace
{
    // The real build output name (TargetName = RootNamespace). The old tests hard-coded
    // "FileScout.Native.exe", which never matched the shipped binary (spec-shell S-13).
    constexpr wchar_t k_exe[] = L"C:\\Program Files\\FileScout\\FileScoutNative.exe";

    std::optional<std::wstring> Read(std::wstring const& path, wchar_t const* name = nullptr)
    {
        wchar_t buffer[1024]{};
        DWORD bytes = sizeof(buffer);
        if (::RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        return std::wstring{ buffer };
    }

    void Write(std::wstring const& path, wchar_t const* name, std::wstring const& data)
    {
        HKEY key{};
        ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr), ERROR_SUCCESS);
        ::RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<BYTE const*>(data.c_str()),
            static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(key);
    }

    bool Exists(std::wstring const& path)
    {
        HKEY key{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        ::RegCloseKey(key);
        return true;
    }

    // Recursive text dump of a subtree, used to prove "no traces" after a restore.
    std::wstring Dump(std::wstring const& path)
    {
        std::wstring out;
        HKEY key{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return L"<missing>";
        }
        wchar_t name[512];
        BYTE data[2048];
        for (DWORD i = 0;; ++i)
        {
            DWORD nameLength = 512;
            DWORD dataLength = sizeof(data);
            DWORD type = 0;
            if (::RegEnumValueW(key, i, name, &nameLength, nullptr, &type, data, &dataLength) != ERROR_SUCCESS)
            {
                break;
            }
            out += L"V " + std::wstring{ name } + L"=" + std::wstring{ reinterpret_cast<wchar_t const*>(data) } + L"\n";
        }
        std::vector<std::wstring> children;
        for (DWORD i = 0;; ++i)
        {
            DWORD nameLength = 512;
            if (::RegEnumKeyExW(key, i, name, &nameLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            {
                break;
            }
            children.emplace_back(name);
        }
        ::RegCloseKey(key);
        for (auto const& child : children)
        {
            out += L"K " + child + L"\n" + Dump(path + L"\\" + child);
        }
        return out;
    }

    class DefaultFileManagerRegistry : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GUID guid{};
            ASSERT_EQ(::CoCreateGuid(&guid), S_OK);
            wchar_t text[64]{};
            ::StringFromGUID2(guid, text, 64);
            m_root = std::wstring{ L"Software\\FileScoutTests\\" } + text;
            m_classes = m_root + L"\\Classes";
            m_manifest = m_root + L"\\FileScout\\DefaultFileManager";
            HKEY key{};
            ASSERT_EQ(::RegCreateKeyExW(HKEY_CURRENT_USER, m_classes.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr), ERROR_SUCCESS);
            ::RegCloseKey(key);
        }

        void TearDown() override
        {
            ::RegDeleteTreeW(HKEY_CURRENT_USER, m_root.c_str());
            ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\FileScoutTests");
        }

        DefaultFileManagerService Service(std::wstring const& exe = k_exe) const
        {
            return DefaultFileManagerService{ m_classes, m_manifest, exe };
        }

        std::wstring m_root;
        std::wstring m_classes;
        std::wstring m_manifest;
    };

    std::wstring const k_winE = L"\\CLSID\\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\\shell\\opennewwindow\\command";
}

// --- Command values ---

TEST(DefaultFileManagerService, BuildFolderCommandValueQuotesPathAndAppendsPercentOne)
{
    EXPECT_EQ(BuildFolderCommandValue(k_exe), L"\"C:\\Program Files\\FileScout\\FileScoutNative.exe\" \"%1\"");
}

TEST(DefaultFileManagerService, BuildFolderCommandValueHandlesSimplePath)
{
    EXPECT_EQ(BuildFolderCommandValue(L"C:\\FileScout.exe"), L"\"C:\\FileScout.exe\" \"%1\"");
}

TEST(DefaultFileManagerService, BuildWinECommandValueQuotesPathWithoutPercentOne)
{
    EXPECT_EQ(BuildWinECommandValue(k_exe), L"\"C:\\Program Files\\FileScout\\FileScoutNative.exe\"");
}

// --- ValuePointsToExecutable (S-13: compare with the running exe, not a hard-coded name) ---

TEST(DefaultFileManagerService, ValuePointsToExecutableMatchesQuotedCommand)
{
    EXPECT_TRUE(ValuePointsToExecutable(BuildFolderCommandValue(k_exe), k_exe));
}

TEST(DefaultFileManagerService, ValuePointsToExecutableIgnoresCase)
{
    EXPECT_TRUE(ValuePointsToExecutable(L"\"C:\\PROGRAM FILES\\FILESCOUT\\FILESCOUTNATIVE.EXE\" \"%1\"", k_exe));
}

TEST(DefaultFileManagerService, ValuePointsToExecutableMatchesUnquotedCommand)
{
    EXPECT_TRUE(ValuePointsToExecutable(L"C:\\Tools\\FileScoutNative.exe %1", L"C:\\Tools\\FileScoutNative.exe"));
}

TEST(DefaultFileManagerService, ValuePointsToExecutableRejectsExplorerEmptyAndOtherApps)
{
    EXPECT_FALSE(ValuePointsToExecutable(L"%SystemRoot%\\explorer.exe", k_exe));
    EXPECT_FALSE(ValuePointsToExecutable(L"", k_exe));
    EXPECT_FALSE(ValuePointsToExecutable(L"\"C:\\Tools\\TotalCommander.exe\" \"%1\"", k_exe));
}

TEST(DefaultFileManagerService, ValuePointsToExecutableRejectsAnotherCopyOfFileScout)
{
    // A different build of FileScout is not "this" FileScout: status must say NotDefault so the
    // user can re-point the verbs at the copy they are running.
    EXPECT_FALSE(ValuePointsToExecutable(L"\"D:\\old\\FileScoutNative.exe\" \"%1\"", k_exe));
}

// --- DeriveStatus ---

TEST(DefaultFileManagerService, DeriveStatusReturnsFullyDefaultWhenAllTrue)
{
    EXPECT_EQ(DeriveStatus(true, true, true), DefaultFileManagerStatus::FullyDefault);
}

TEST(DefaultFileManagerService, DeriveStatusReturnsNotDefaultWhenAllFalse)
{
    EXPECT_EQ(DeriveStatus(false, false, false), DefaultFileManagerStatus::NotDefault);
}

TEST(DefaultFileManagerService, DeriveStatusReturnsPartiallyDefaultWhenMixed)
{
    EXPECT_EQ(DeriveStatus(true, false, false), DefaultFileManagerStatus::PartiallyDefault);
    EXPECT_EQ(DeriveStatus(true, true, false), DefaultFileManagerStatus::PartiallyDefault);
    EXPECT_EQ(DeriveStatus(false, true, true), DefaultFileManagerStatus::PartiallyDefault);
}

// --- Manifest encoding ---

TEST(DefaultFileManagerService, ManifestEntryRoundTripsValueDataContainingSeparators)
{
    ManifestEntry entry{ false, L"Directory\\shell", L"", true, L"cmd /c \"a|b\" %1" };
    auto const decoded = DecodeManifestEntry(EncodeManifestEntry(entry));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->isKey);
    EXPECT_EQ(decoded->subKey, L"Directory\\shell");
    EXPECT_EQ(decoded->valueName, L"");
    EXPECT_TRUE(decoded->existed);
    EXPECT_EQ(decoded->oldData, L"cmd /c \"a|b\" %1");
}

TEST(DefaultFileManagerService, ManifestEntryRoundTripsCreatedKey)
{
    auto const decoded = DecodeManifestEntry(EncodeManifestEntry({ true, L"Drive\\shell", {}, false, {} }));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->isKey);
    EXPECT_EQ(decoded->subKey, L"Drive\\shell");
}

TEST(DefaultFileManagerService, ManifestDecodeRejectsGarbage)
{
    EXPECT_FALSE(DecodeManifestEntry(L"").has_value());
    EXPECT_FALSE(DecodeManifestEntry(L"X|foo").has_value());
    EXPECT_FALSE(DecodeManifestEntry(L"V|key|name").has_value());
    EXPECT_FALSE(DecodeManifestEntry(L"V|key|name|7|x").has_value());
}

// --- Registry round trips (isolated test hive under HKCU\Software\FileScoutTests) ---

TEST_F(DefaultFileManagerRegistry, FreshProfileIsNotDefault)
{
    EXPECT_EQ(Service().GetStatus(), DefaultFileManagerStatus::NotDefault);
}

TEST_F(DefaultFileManagerRegistry, SetAsDefaultWritesFolderDriveAndWinEVerbs)
{
    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());

    EXPECT_EQ(service.GetStatus(), DefaultFileManagerStatus::FullyDefault);
    EXPECT_EQ(Read(m_classes + L"\\Directory\\shell"), L"FileScout");
    EXPECT_EQ(Read(m_classes + L"\\Directory\\shell\\FileScout\\command"), BuildFolderCommandValue(k_exe));
    EXPECT_EQ(Read(m_classes + L"\\Drive\\shell"), L"FileScout");
    for (std::wstring const cls : { L"\\Directory", L"\\Drive" })
    {
        for (std::wstring const verb : { L"open", L"explore" })
        {
            auto const key = m_classes + cls + L"\\shell\\" + verb + L"\\command";
            EXPECT_EQ(Read(key), BuildFolderCommandValue(k_exe)) << key;
            EXPECT_EQ(Read(key, L"DelegateExecute"), L"") << key;
        }
    }
    EXPECT_EQ(Read(m_classes + L"\\Folder\\shell\\explore\\command"), BuildFolderCommandValue(k_exe));
    EXPECT_EQ(Read(m_classes + k_winE), BuildWinECommandValue(k_exe));
    EXPECT_EQ(Read(m_classes + k_winE, L"DelegateExecute"), L"");
}

TEST_F(DefaultFileManagerRegistry, RestoreOnFreshProfileLeavesNoTraces)
{
    auto const before = Dump(m_classes);
    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    ASSERT_TRUE(service.RestoreDefault());

    EXPECT_EQ(Dump(m_classes), before);
    EXPECT_FALSE(Exists(m_manifest));
    EXPECT_FALSE(Exists(m_root + L"\\FileScout"));
    EXPECT_EQ(service.GetStatus(), DefaultFileManagerStatus::NotDefault);
}

TEST_F(DefaultFileManagerRegistry, RestorePutsBackAnotherAppsVerbsAndKeepsUnrelatedKeys)
{
    Write(m_classes + L"\\Directory\\shell", nullptr, L"otherapp");
    Write(m_classes + L"\\Directory\\shell\\otherapp\\command", nullptr, L"\"C:\\Other\\other.exe\" \"%1\"");
    Write(m_classes + L"\\CLSID\\{00000000-1111-2222-3333-444444444444}", nullptr, L"unrelated");
    auto const before = Dump(m_classes);

    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    EXPECT_EQ(Read(m_classes + L"\\Directory\\shell"), L"FileScout");
    ASSERT_TRUE(service.RestoreDefault());

    EXPECT_EQ(Dump(m_classes), before);
}

TEST_F(DefaultFileManagerRegistry, SecondSetKeepsTheOriginalStateForRestore)
{
    auto const before = Dump(m_classes);
    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    ASSERT_TRUE(service.SetAsDefault());
    ASSERT_TRUE(service.RestoreDefault());

    EXPECT_EQ(Dump(m_classes), before);
}

TEST_F(DefaultFileManagerRegistry, RestoreWithoutSetIsANoOp)
{
    auto const before = Dump(m_classes);
    EXPECT_TRUE(Service().RestoreDefault());
    EXPECT_EQ(Dump(m_classes), before);
}

TEST_F(DefaultFileManagerRegistry, MissingWinEHandlerIsPartiallyDefault)
{
    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    ::RegDeleteTreeW(HKEY_CURRENT_USER, (m_classes + L"\\CLSID").c_str());

    EXPECT_EQ(service.GetStatus(), DefaultFileManagerStatus::PartiallyDefault);
}

TEST_F(DefaultFileManagerRegistry, MissingExploreVerbIsPartiallyDefault)
{
    // Edge's "Open downloads folder" uses the explore verb; without it that path opens Explorer.
    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    ::RegDeleteTreeW(HKEY_CURRENT_USER, (m_classes + L"\\Directory\\shell\\explore").c_str());

    EXPECT_EQ(service.GetStatus(), DefaultFileManagerStatus::PartiallyDefault);
}

TEST_F(DefaultFileManagerRegistry, MissingFolderExploreVerbIsPartiallyDefault)
{
    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    ::RegDeleteTreeW(HKEY_CURRENT_USER, (m_classes + L"\\Folder").c_str());

    EXPECT_EQ(service.GetStatus(), DefaultFileManagerStatus::PartiallyDefault);
}

TEST_F(DefaultFileManagerRegistry, RestoreKeepsAnotherAppsOpenVerb)
{
    Write(m_classes + L"\\Directory\\shell\\open\\command", nullptr, L"\"C:\\Other\\other.exe\" \"%1\"");
    auto const before = Dump(m_classes);

    auto service = Service();
    ASSERT_TRUE(service.SetAsDefault());
    ASSERT_TRUE(service.RestoreDefault());

    EXPECT_EQ(Dump(m_classes), before);
}

TEST_F(DefaultFileManagerRegistry, VerbsOfAnotherFileScoutCopyAreNotDefault)
{
    ASSERT_TRUE(Service(L"D:\\old\\FileScoutNative.exe").SetAsDefault());

    EXPECT_EQ(Service().GetStatus(), DefaultFileManagerStatus::NotDefault);
}
