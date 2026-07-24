/*
 * test_main.cpp — custom Catch2 runner with CRT leak detection
 */

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _MSC_VER
    // Redirect CRT debug reports to stderr (visible in test output)
    _CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    // Enable debug heap with boundary checks:
    //  ALLOC_MEM_DF         — use debug heap (tracks file/line per block)
    //  CHECK_ALWAYS_DF      — _CrtCheckMemory() on every alloc/free (catches
    //                          heap corruption at the exact call site)
    //  DELAY_FREE_MEM_DF    — fill freed blocks with 0xDD, keep in heap list
    //                          (catches use-after-free by crashing on access)
    //  LEAK_CHECK_DF        — auto dump leaks on exit
    int flags = _CRTDBG_ALLOC_MEM_DF
              | _CRTDBG_CHECK_ALWAYS_DF
              | _CRTDBG_DELAY_FREE_MEM_DF
              | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flags);
#endif
    Catch::Session session;
    int result = session.run(argc, argv);
    return result;
}
