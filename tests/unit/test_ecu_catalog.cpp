#include <gtest/gtest.h>
#include "ecu/EcuCatalog.hpp"

using namespace ecu;

TEST(EcuCatalog, GetKnownEcuHasStage1Maps) {
    auto e = getEcu("edc16c34");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->id, "edc16c34");
    EXPECT_EQ(e->family, "EDC16");

    ASSERT_TRUE(e->stage1Maps.has_value());
    EXPECT_FALSE(e->stage1Maps->empty());

    // Every stage-1 map should carry a non-empty name.
    for (const auto& m : *e->stage1Maps)
        EXPECT_FALSE(m.name.empty());
}

TEST(EcuCatalog, ListEcusNonEmptyAndConsistent) {
    auto list = listEcus();
    ASSERT_FALSE(list.empty());

    bool foundEdc16c34 = false;
    for (const auto& s : list) {
        EXPECT_FALSE(s.id.empty());
        if (s.id == "edc16c34") {
            foundEdc16c34 = true;
            EXPECT_TRUE(s.hasStage1);
        }
    }
    EXPECT_TRUE(foundEdc16c34);
}

TEST(EcuCatalog, CatalogMatchesListSize) {
    EXPECT_EQ(catalog().size(), listEcus().size());
}

TEST(EcuCatalog, GetUnknownEcuReturnsNullopt) {
    EXPECT_FALSE(getEcu("does_not_exist").has_value());
    EXPECT_FALSE(getEcu("").has_value());
}
