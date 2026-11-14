#include "TestFramework.hpp"

int main(int argc, char **argv) {
    return oxo::test::RunAllTests(argc > 1 ? argv[1] : nullptr);
}
