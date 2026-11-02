#include "Test.h"
#include <cstdio>

int main()
{
    int failed = 0;

    for (auto const& test : Test::Registry())
    {
        try
        {
            test.Body();
        }
        catch (Test::Failure const& failure)
        {
            ++failed;
            std::printf("FAIL  %s\n      %s\n", test.Name, failure.Message.c_str());
        }
        catch (std::exception const& ex)
        {
            ++failed;
            std::printf("FAIL  %s\n      threw: %s\n", test.Name, ex.what());
        }
    }

    std::printf("\n%zu tests, %d failed\n", Test::Registry().size(), failed);
    return failed;
}
