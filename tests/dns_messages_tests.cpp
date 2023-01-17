#include "gtest/gtest.h"

#include "nsblast/DnsMessages.h"


TEST(Gtest, Validate) {
    EXPECT_TRUE(true);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
