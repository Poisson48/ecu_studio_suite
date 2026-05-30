#include <gtest/gtest.h>
#include "mpps/MppsDevice.hpp"

TEST(MppsSimulation, ConnectAndIdentify) {
    auto dev = mpps::MppsDevice::openSimulation();
    ASSERT_TRUE(dev);
    auto info = dev->connect();
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->chipType, "SIMULATION");
    auto ecuId = dev->identifyEcu();
    ASSERT_TRUE(ecuId.has_value());
    EXPECT_FALSE(ecuId->empty());
}

TEST(MppsSimulation, ReadWriteRom) {
    auto dev = mpps::MppsDevice::openSimulation();
    dev->connect();
    auto rom = dev->readBlock(0, 256);
    ASSERT_TRUE(rom.has_value());
    EXPECT_EQ(rom->size(), 256u);

    std::vector<uint8_t> patch(256, 0xAB);
    auto wr = dev->writeBlock(0, patch);
    ASSERT_TRUE(wr.has_value());

    auto verify = dev->readBlock(0, 256);
    ASSERT_TRUE(verify.has_value());
    EXPECT_EQ((*verify)[0], 0xAB);
}
