//
// Created by vvass on 24-Jul-26.
//
#include "LldpUtils.h"
#include <gtest/gtest.h>

namespace RSCGroup {
namespace {

// ---------------------------------------------------------------------------
// isMacLike
// ---------------------------------------------------------------------------

TEST(LldpUtilsTest, isMacLike_ValidLowercase) {
    EXPECT_TRUE(isMacLike("aa:bb:cc:dd:ee:ff"));
}

TEST(LldpUtilsTest, isMacLike_ValidUppercase) {
    EXPECT_TRUE(isMacLike("AA:BB:CC:DD:EE:FF"));
}

TEST(LldpUtilsTest, isMacLike_ValidMixedCase) {
    EXPECT_TRUE(isMacLike("aA:bB:cC:dD:eE:fF"));
}

TEST(LldpUtilsTest, isMacLike_TooShort) {
    EXPECT_FALSE(isMacLike("aa:bb:cc:dd:ee"));
}

TEST(LldpUtilsTest, isMacLike_TooLong) {
    EXPECT_FALSE(isMacLike("aa:bb:cc:dd:ee:ff:00"));
}

TEST(LldpUtilsTest, isMacLike_NoColons) {
    EXPECT_FALSE(isMacLike("aabbccddeeff"));
}

TEST(LldpUtilsTest, isMacLike_HyphenSeparated) {
    EXPECT_FALSE(isMacLike("aa-bb-cc-dd-ee-ff"));
}

TEST(LldpUtilsTest, isMacLike_NonHexCharacters) {
    EXPECT_FALSE(isMacLike("zz:bb:cc:dd:ee:ff"));
}

TEST(LldpUtilsTest, isMacLike_Empty) {
    EXPECT_FALSE(isMacLike(""));
}

TEST(LldpUtilsTest, isMacLike_WrongSize) {
    EXPECT_FALSE(isMacLike("aa:bb:cc:dd"));
    EXPECT_FALSE(isMacLike("aa:bb:cc:dd:ee:ff:00:11"));
}

// ---------------------------------------------------------------------------
// normalizeMac
// ---------------------------------------------------------------------------

TEST(LldpUtilsTest, normalizeMac_LowercasePassthrough) {
    EXPECT_EQ(normalizeMac("aa:bb:cc:dd:ee:ff"), "aa:bb:cc:dd:ee:ff");
}

TEST(LldpUtilsTest, normalizeMac_UppercaseToLowercase) {
    EXPECT_EQ(normalizeMac("AA:BB:CC:DD:EE:FF"), "aa:bb:cc:dd:ee:ff");
}

TEST(LldpUtilsTest, normalizeMac_MixedCase) {
    EXPECT_EQ(normalizeMac("aA:Bb:cC:Dd:eE:Ff"), "aa:bb:cc:dd:ee:ff");
}

TEST(LldpUtilsTest, normalizeMac_NonHexPassesThrough) {
    // normalizeMac doesn't validate — it just lowercases
    EXPECT_EQ(normalizeMac("ZZ:BB:CC:DD:EE:FF"), "zz:bb:cc:dd:ee:ff");
}

// ---------------------------------------------------------------------------
// resolveLldpIdentity
// ---------------------------------------------------------------------------

TEST(LldpUtilsTest, resolveLldpIdentity_ChassisIdPreferred) {
    auto result = resolveLldpIdentity(
        std::optional<std::string>("aa:bb:cc:dd:ee:ff"),
        std::optional<std::string>("11:22:33:44:55:66"));
    EXPECT_EQ(result, "aa:bb:cc:dd:ee:ff");
}

TEST(LldpUtilsTest, resolveLldpIdentity_PortIdFallback) {
    auto result = resolveLldpIdentity(
        std::optional<std::string>("hostname123"),
        std::optional<std::string>("11:22:33:44:55:66"));
    EXPECT_EQ(result, "11:22:33:44:55:66");
}

TEST(LldpUtilsTest, resolveLldpIdentity_BothNonMac) {
    auto result = resolveLldpIdentity(
        std::optional<std::string>("hostname"),
        std::optional<std::string>("eth0"));
    EXPECT_TRUE(result.empty());
}

TEST(LldpUtilsTest, resolveLldpIdentity_BothNullopt) {
    auto result = resolveLldpIdentity(std::nullopt, std::nullopt);
    EXPECT_TRUE(result.empty());
}

TEST(LldpUtilsTest, resolveLldpIdentity_OnlyChassisNonNullopt) {
    auto result = resolveLldpIdentity(
        std::optional<std::string>("aa:bb:cc:dd:ee:ff"),
        std::nullopt);
    EXPECT_EQ(result, "aa:bb:cc:dd:ee:ff");
}

TEST(LldpUtilsTest, resolveLldpIdentity_OnlyPortNonNullopt) {
    auto result = resolveLldpIdentity(
        std::nullopt,
        std::optional<std::string>("11:22:33:44:55:66"));
    EXPECT_EQ(result, "11:22:33:44:55:66");
}

TEST(LldpUtilsTest, resolveLldpIdentity_NormalizesCase) {
    auto result = resolveLldpIdentity(
        std::optional<std::string>("AA:BB:CC:DD:EE:FF"),
        std::nullopt);
    EXPECT_EQ(result, "aa:bb:cc:dd:ee:ff");
}

} // namespace
} // namespace RSCGroup