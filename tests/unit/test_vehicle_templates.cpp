#include <gtest/gtest.h>
#include "ecu/VehicleTemplates.hpp"

#include <algorithm>

using namespace ecu;

TEST(VehicleTemplates, ListTemplatesForEcuNonEmpty) {
    auto list = listTemplatesForEcu("edc16c34");
    ASSERT_FALSE(list.empty());

    // Every returned template must declare edc16c34 in its appliesTo set.
    for (const auto& s : list) {
        EXPECT_FALSE(s.id.empty());
        EXPECT_NE(std::ranges::find(s.appliesTo, std::string("edc16c34")),
                  s.appliesTo.end());
    }
}

TEST(VehicleTemplates, ListTemplatesNonEmpty) {
    EXPECT_FALSE(listTemplates().empty());
}

TEST(VehicleTemplates, GetKnownTemplate) {
    auto t = getTemplate("psa_16hdi_75_stage1_safe");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->id, "psa_16hdi_75_stage1_safe");
    EXPECT_NE(std::ranges::find(t->appliesTo, std::string("edc16c34")),
              t->appliesTo.end());
}

TEST(VehicleTemplates, GetUnknownTemplateReturnsNullopt) {
    EXPECT_FALSE(getTemplate("no_such_template").has_value());
    EXPECT_FALSE(getTemplate("").has_value());
}

TEST(VehicleTemplates, ListTemplatesForUnknownEcuEmpty) {
    EXPECT_TRUE(listTemplatesForEcu("nonexistent_ecu").empty());
}
