#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/ComApartment.h"

using FileScoutNative::Shell::ComApartment;

namespace
{
    struct ApartmentState
    {
        HRESULT hr = E_FAIL;
        APTTYPE type = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;

        // Explicitly joined MTA, not the implicit MTA a thread without CoInitializeEx falls into.
        [[nodiscard]] bool IsExplicitMta() const
        {
            return hr == S_OK && type == APTTYPE_MTA && qualifier != APTTYPEQUALIFIER_IMPLICIT_MTA;
        }
    };

    ApartmentState QueryApartment()
    {
        ApartmentState state;
        state.hr = CoGetApartmentType(&state.type, &state.qualifier);
        return state;
    }

    template <typename F>
    void RunOnFreshThread(F&& body)
    {
        std::thread worker{ std::forward<F>(body) };
        worker.join();
    }
}

// S-01: background COM calls ran on threads that never called CoInitializeEx.
TEST(ComApartmentTests, FreshThreadJoinsMtaInsideGuardAndLeavesAfter)
{
    ApartmentState before, inside, after;
    bool usable = false;
    RunOnFreshThread([&]
    {
        before = QueryApartment();
        {
            ComApartment apartment;
            usable = apartment.IsUsable();
            inside = QueryApartment();
        }
        after = QueryApartment();
    });

    EXPECT_FALSE(before.IsExplicitMta());
    EXPECT_TRUE(usable);
    EXPECT_TRUE(inside.IsExplicitMta()) << std::hex << inside.hr << " type " << inside.type;
    EXPECT_FALSE(after.IsExplicitMta());
}

TEST(ComApartmentTests, NestedGuardsAreSafe)
{
    ApartmentState afterInner, afterOuter;
    bool innerUsable = false;
    RunOnFreshThread([&]
    {
        {
            ComApartment outer;
            {
                ComApartment inner;
                innerUsable = inner.IsUsable();
            }
            afterInner = QueryApartment();
        }
        afterOuter = QueryApartment();
    });

    EXPECT_TRUE(innerUsable);
    EXPECT_TRUE(afterInner.IsExplicitMta()) << "inner guard must not tear down the outer apartment";
    EXPECT_FALSE(afterOuter.IsExplicitMta());
}

TEST(ComApartmentTests, StaThreadKeepsItsApartment)
{
    ApartmentState inside, after;
    bool usable = false;
    RunOnFreshThread([&]
    {
        ASSERT_EQ(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), S_OK);
        {
            ComApartment apartment;
            usable = apartment.IsUsable();
            inside = QueryApartment();
        }
        after = QueryApartment();
        CoUninitialize();
    });

    EXPECT_TRUE(usable) << "an STA thread can still make COM calls";
    // The first STA of a process reports APTTYPE_MAINSTA.
    auto const isSta = [](APTTYPE type) { return type == APTTYPE_STA || type == APTTYPE_MAINSTA; };
    EXPECT_TRUE(isSta(inside.type)) << inside.type;
    EXPECT_EQ(after.hr, S_OK);
    EXPECT_TRUE(isSta(after.type)) << "guard must not uninitialize an apartment it did not join";
}
