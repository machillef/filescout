#include "pch.h"
#include <gtest/gtest.h>

// Sources are BOM-less UTF-8. Without /utf-8, MSVC reads them as the ANSI code page (cp1252), so a
// raw Greek wide literal becomes mojibake and silently differs from the same text written with
// \u escapes (a test comparing Greek names could pass or fail for the wrong reason).
TEST(SourceEncoding, RawUtf8WideLiteralMatchesEscapedText)
{
    std::wstring const raw = L"Αρχείο";
    std::wstring const escaped = L"\u0391\u03c1\u03c7\u03b5\u03af\u03bf";

    EXPECT_EQ(raw, escaped);
    EXPECT_EQ(raw.size(), 6u);
}
