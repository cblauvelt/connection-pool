

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(Errors, DefaultError) {
    std::string errorMessage = "Test Message";
    cpool::error error;
    cpool::error errorWithMessage(errorMessage);

    EXPECT_EQ(error, cpool::no_error);
    EXPECT_FALSE(error);
    EXPECT_TRUE(!error);

    EXPECT_EQ(errorWithMessage.message(), errorWithMessage.what());
    EXPECT_EQ(errorWithMessage.message(), errorMessage);
    EXPECT_EQ(errorWithMessage.value(),
              (int)cpool::generic_error_code::generic_error);
    EXPECT_TRUE(errorWithMessage);
    EXPECT_FALSE(!errorWithMessage);
}

TEST(Errors, CustomError) {
    std::string errorMessage = "Test Message";
    cpool::error error;
    cpool::error errorWithMessage(
        std::error_code(cpool::generic_error_code::generic_error),
        errorMessage);

    EXPECT_EQ(error.value(), (int)cpool::generic_error_code::no_error);
    EXPECT_FALSE(error);
    EXPECT_TRUE(!error);

    EXPECT_EQ(errorWithMessage.message(), errorWithMessage.what());
    EXPECT_EQ(errorWithMessage.message(), errorMessage);
    EXPECT_EQ(errorWithMessage.value(),
              (int)cpool::generic_error_code::generic_error);
    EXPECT_TRUE(errorWithMessage);
    EXPECT_FALSE(!errorWithMessage);
}

} // namespace