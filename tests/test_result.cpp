#include <gtest/gtest.h>
#include "Core/Result.h"

using namespace InputBridge;

// ═════════════════════════════════════════════════════════════════════════════
// Basic Ok / Err construction
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, OkIsOk) {
    auto r = Result<int, std::string>::Ok(42);
    EXPECT_TRUE(r.IsOk());
    EXPECT_FALSE(r.IsErr());
}

TEST(Result, ErrIsErr) {
    auto r = Result<int, std::string>::Err("oops");
    EXPECT_FALSE(r.IsOk());
    EXPECT_TRUE(r.IsErr());
}

TEST(Result, BoolOperatorTrueForOk) {
    auto r = Result<int, std::string>::Ok(1);
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(Result, BoolOperatorFalseForErr) {
    auto r = Result<int, std::string>::Err("fail");
    EXPECT_FALSE(static_cast<bool>(r));
}

// ═════════════════════════════════════════════════════════════════════════════
// Value / Error accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, ValueReturnsCorrectValue) {
    auto r = Result<int, std::string>::Ok(99);
    EXPECT_EQ(r.Value(), 99);
}

TEST(Result, ErrorReturnsCorrectError) {
    auto r = Result<int, std::string>::Err(std::string("bad"));
    EXPECT_EQ(r.Error(), "bad");
}

TEST(Result, ValueThrowsOnErrResult) {
    auto r = Result<int, std::string>::Err("bad");
    EXPECT_THROW(r.Value(), std::runtime_error);
}

TEST(Result, ErrorThrowsOnOkResult) {
    auto r = Result<int, std::string>::Ok(1);
    EXPECT_THROW(r.Error(), std::runtime_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// ValueOr
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, ValueOrReturnsValueWhenOk) {
    auto r = Result<int, std::string>::Ok(7);
    EXPECT_EQ(r.ValueOr(0), 7);
}

TEST(Result, ValueOrReturnsDefaultWhenErr) {
    auto r = Result<int, std::string>::Err("no");
    EXPECT_EQ(r.ValueOr(-1), -1);
}

// ═════════════════════════════════════════════════════════════════════════════
// Map
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, MapTransformsValueWhenOk) {
    auto r = Result<int, std::string>::Ok(5);
    auto doubled = r.Map([](int x) { return x * 2; });
    EXPECT_TRUE(doubled.IsOk());
    EXPECT_EQ(doubled.Value(), 10);
}

TEST(Result, MapPreservesErrUnchanged) {
    auto r = Result<int, std::string>::Err(std::string("err"));
    auto mapped = r.Map([](int x) { return x * 2; });
    EXPECT_TRUE(mapped.IsErr());
    EXPECT_EQ(mapped.Error(), "err");
}

TEST(Result, MapCanChangeValueType) {
    auto r = Result<int, std::string>::Ok(42);
    auto strResult = r.Map([](int x) { return std::to_string(x); });
    EXPECT_TRUE(strResult.IsOk());
    EXPECT_EQ(strResult.Value(), "42");
}

// ═════════════════════════════════════════════════════════════════════════════
// MapErr
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, MapErrTransformsErrorWhenErr) {
    auto r = Result<int, int>::Err(3);
    auto mapped = r.MapError([](int e) { return e * 10; });
    EXPECT_TRUE(mapped.IsErr());
    EXPECT_EQ(mapped.Error(), 30);
}

TEST(Result, MapErrPreservesOkUnchanged) {
    auto r = Result<int, int>::Ok(5);
    auto mapped = r.MapError([](int e) { return e * 10; });
    EXPECT_TRUE(mapped.IsOk());
    EXPECT_EQ(mapped.Value(), 5);
}

// ═════════════════════════════════════════════════════════════════════════════
// AndThen (monadic chain)
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, AndThenChainsOnOk) {
    auto r = Result<int, std::string>::Ok(10);
    auto chained = r.AndThen([](int x) {
        return Result<int, std::string>::Ok(x + 5);
    });
    EXPECT_TRUE(chained.IsOk());
    EXPECT_EQ(chained.Value(), 15);
}

TEST(Result, AndThenShortCircuitsOnErr) {
    auto r = Result<int, std::string>::Err(std::string("initial error"));
    bool called = false;
    auto chained = r.AndThen([&called](int x) {
        called = true;
        return Result<int, std::string>::Ok(x + 5);
    });
    EXPECT_FALSE(called);
    EXPECT_TRUE(chained.IsErr());
}

TEST(Result, AndThenCanPropagateNewError) {
    auto r = Result<int, std::string>::Ok(0);
    auto chained = r.AndThen([](int x) {
        return Result<int, std::string>::Err(std::string("division by zero"));
    });
    EXPECT_TRUE(chained.IsErr());
    EXPECT_EQ(chained.Error(), "division by zero");
}

// ═════════════════════════════════════════════════════════════════════════════
// Works with project-specific error enums
// ═════════════════════════════════════════════════════════════════════════════

TEST(Result, WorksWithHapticErrorEnum) {
    auto ok = Result<bool, HapticError>::Ok(true);
    EXPECT_TRUE(ok.IsOk());
    EXPECT_TRUE(ok.Value());

    auto err = Result<bool, HapticError>::Err(HapticError::DeviceNotFound);
    EXPECT_TRUE(err.IsErr());
    EXPECT_EQ(err.Error(), HapticError::DeviceNotFound);
}

TEST(Result, WorksWithDeviceErrorEnum) {
    auto err = Result<int, DeviceError>::Err(DeviceError::OpenFailed);
    EXPECT_TRUE(err.IsErr());
    EXPECT_EQ(err.Error(), DeviceError::OpenFailed);
}

TEST(Result, WorksWithNetworkErrorEnum) {
    auto err = Result<bool, NetworkError>::Err(NetworkError::BindFailed);
    EXPECT_EQ(err.Error(), NetworkError::BindFailed);
}

// ═════════════════════════════════════════════════════════════════════════════
// ToString helpers for error enums
// ═════════════════════════════════════════════════════════════════════════════

TEST(HapticErrorToString, KnownValues) {
    EXPECT_STREQ(ToString(HapticError::DeviceNotFound),     "Device not found");
    EXPECT_STREQ(ToString(HapticError::InitializationFailed), "Initialization failed");
    EXPECT_STREQ(ToString(HapticError::UnsupportedEffect),  "Unsupported effect");
    EXPECT_STREQ(ToString(HapticError::EffectUploadFailed), "Effect upload failed");
    EXPECT_STREQ(ToString(HapticError::SDLError),           "SDL error");
    EXPECT_STREQ(ToString(HapticError::NotReady),           "Device not ready");
}

TEST(DeviceErrorToString, KnownValues) {
    EXPECT_STREQ(ToString(DeviceError::OpenFailed),        "Failed to open device");
    EXPECT_STREQ(ToString(DeviceError::InvalidInstanceID), "Invalid instance ID");
    EXPECT_STREQ(ToString(DeviceError::UnsupportedType),   "Unsupported device type");
    EXPECT_STREQ(ToString(DeviceError::AlreadyOpen),       "Device already open");
    EXPECT_STREQ(ToString(DeviceError::NotFound),          "Device not found");
}

TEST(NetworkErrorToString, KnownValues) {
    EXPECT_STREQ(ToString(NetworkError::BindFailed),       "Failed to bind");
    EXPECT_STREQ(ToString(NetworkError::ConnectionFailed), "Connection failed");
    EXPECT_STREQ(ToString(NetworkError::SendFailed),       "Send failed");
    EXPECT_STREQ(ToString(NetworkError::ReceiveFailed),    "Receive failed");
    EXPECT_STREQ(ToString(NetworkError::InvalidAddress),   "Invalid address");
    EXPECT_STREQ(ToString(NetworkError::Timeout),          "Operation timed out");
}
