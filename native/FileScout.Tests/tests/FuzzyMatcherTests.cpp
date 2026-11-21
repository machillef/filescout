#include "pch.h"
#include <gtest/gtest.h>
#include "Search/FuzzyMatcher.h"

using namespace FileScoutNative::Search;

TEST(FuzzyMatcher, EmptyQueryReturnsNoMatch)
{
    EXPECT_EQ(-1, FuzzyScore(L"", L"document.txt"));
}

TEST(FuzzyMatcher, SingleCharQueryReturnsNoMatch)
{
    EXPECT_EQ(-1, FuzzyScore(L"d", L"document.txt"));
}

TEST(FuzzyMatcher, NoMatchWhenCharNotInFilename)
{
    EXPECT_EQ(-1, FuzzyScore(L"zzz", L"document.txt"));
}

TEST(FuzzyMatcher, PrefixScoresHigherThanMidWord)
{
    int prefixScore = FuzzyScore(L"doc", L"document.txt");
    int midScore    = FuzzyScore(L"doc", L"readme.doc");
    EXPECT_GT(prefixScore, midScore);
    EXPECT_GE(prefixScore, 0);
}

TEST(FuzzyMatcher, ConsecutiveScoresHigherThanScattered)
{
    int consecutive = FuzzyScore(L"abc", L"abcdef.txt");
    int scattered   = FuzzyScore(L"abc", L"axbxcx.txt");
    EXPECT_GT(consecutive, scattered);
    EXPECT_GE(consecutive, 0);
}

TEST(FuzzyMatcher, WildcardIsLiteralPositive)
{
    int score = FuzzyScore(L"a*b", L"a*b_file.txt");
    EXPECT_GE(score, 0);
}

TEST(FuzzyMatcher, WildcardIsLiteralNegative)
{
    EXPECT_EQ(-1, FuzzyScore(L"a*b", L"ab_file.txt"));
}
