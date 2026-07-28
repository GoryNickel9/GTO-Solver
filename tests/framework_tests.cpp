#include "gtosd/core/cards.hpp"
#include "gtosd/version.hpp"

#include <gtest/gtest.h>

#include <string_view>

TEST(Phase0Infrastructure, GoogleTestRunnerLinksProductionCore) {
  EXPECT_EQ(gtosd::short_deck().size(), 36U);
  EXPECT_EQ(gtosd::api_version_major, 0U);
  EXPECT_EQ(gtosd::api_version_minor, 6U);
  EXPECT_EQ(gtosd::api_version_patch, 0U);
  EXPECT_EQ(std::string_view(gtosd::api_version_string), "0.6.0");
}

TEST(Phase0Infrastructure, FileVersionsHaveExplicitMajors) {
  EXPECT_EQ(gtosd::public_state_format_major, 1U);
  EXPECT_EQ(gtosd::isomorphism_format_major, 1U);
  EXPECT_EQ(gtosd::solution_format_major, 1U);
  EXPECT_EQ(gtosd::checkpoint_format_major, 1U);
}
