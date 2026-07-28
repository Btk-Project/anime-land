#include <gtest/gtest.h>

#include <nekoav/elements/pipeline.hpp>

TEST(NekoavDependency, ConstructsAnIdlePipelineFromSubmoduleSources) {
    const auto pipeline = std::make_shared<nekoav::Pipeline>("dependency-smoke");

    ASSERT_NE(pipeline, nullptr);
    EXPECT_EQ(pipeline->state(), nekoav::State::Null);
    EXPECT_EQ(pipeline->name(), "dependency-smoke");
}

#include "common/common_main.hpp.in"
