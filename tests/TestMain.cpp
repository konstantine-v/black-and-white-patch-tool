// bw_tests.exe [--write-hook-manifest <file.csv>]
//
// Runs the unit tests. With --write-hook-manifest it instead writes every registered hook (address, name, calling
// convention, callee-popped bytes) for tools/check_hooks.py to verify against the original executable.

#include "Test.h"

#include "core/Hook.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace bwtest {

namespace {
int g_failures = 0;
}

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

void Fail(const char* file, int line, const std::string& message)
{
    ++g_failures;
    std::cerr << file << "(" << line << "): " << message << "\n";
}

}  // namespace bwtest

namespace {

int WriteHookManifest(const char* path)
{
    std::ofstream out(path);
    out << "address,name,convention,callee_stack_bytes\n";
    for (const bw::HookDescriptor& hook : bw::HookRegistry::All()) {
        out << std::format("0x{:08x},\"{}\",{},{}\n", hook.original, hook.name, bw::ToString(hook.convention),
                           hook.calleeStackBytes);
    }
    return out ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--write-hook-manifest") == 0) {
        return WriteHookManifest(argv[2]);
    }

    int failedTests = 0;
    for (const bwtest::TestCase& test : bwtest::Registry()) {
        const int before = bwtest::g_failures;
        test.body();
        const bool passed = bwtest::g_failures == before;
        failedTests += passed ? 0 : 1;
        std::cout << (passed ? "[pass] " : "[FAIL] ") << test.name << "\n";
    }
    std::cout << bwtest::Registry().size() - static_cast<std::size_t>(failedTests) << "/" << bwtest::Registry().size()
              << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
