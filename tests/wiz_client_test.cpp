#include "CppUTest/TestHarness.h"
#include "components/wiz_client.h"

TEST_GROUP(WizClient)
{
};

TEST(WizClient, TestCase1)
{
    CHECK(true);
}

TEST(WizClient, TestCase2)
{
    CHECK_EQUAL(1, 1);
}