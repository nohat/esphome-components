#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"

TEST_GROUP(AllTests)
{
};

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}