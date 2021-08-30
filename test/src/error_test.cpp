#include "error.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(Errors, DefaultError)
{
    std::string errorMessage = "Test Message";
    cpool::error::error<> error;
    cpool::error::error<> errorWithMessage(cpool::error::generic_error_code::generic_error, errorMessage);

    EXPECT_EQ(error, cpool::error::no_error);
    EXPECT_FALSE(error);
    EXPECT_TRUE(!error);

    EXPECT_EQ(errorWithMessage.message(), errorWithMessage.what());
    EXPECT_EQ(errorWithMessage.message(), errorMessage);
    EXPECT_EQ(errorWithMessage.error_code(), cpool::error::generic_error_code::generic_error);
    EXPECT_TRUE(errorWithMessage);
    EXPECT_FALSE(!errorWithMessage);
}

TEST(Errors, CustomError)
{
    enum class CustomErrorCode {
        NoError = 0,
        BadError,
        ThisIsUnneccesary
    };
    std::string errorMessage = "Test Message";
    cpool::error::error<CustomErrorCode> error;
    cpool::error::error<CustomErrorCode> errorWithMessage(CustomErrorCode::BadError, errorMessage);

    EXPECT_EQ(error.error_code(), CustomErrorCode::NoError);
    EXPECT_FALSE(error);
    EXPECT_TRUE(!error);

    EXPECT_EQ(errorWithMessage.message(), errorWithMessage.what());
    EXPECT_EQ(errorWithMessage.message(), errorMessage);
    EXPECT_EQ(errorWithMessage.error_code(), CustomErrorCode::BadError);
    EXPECT_TRUE(errorWithMessage);
    EXPECT_FALSE(!errorWithMessage);
}

} // namespace