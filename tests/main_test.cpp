#include "test_framework.hpp"
#include <iostream>

int main() {
    std::cout << "Starting DigiDAW Automated Test Suite...\n";
    int result = digidaw::test::TestRunner::instance().run_all();
    return result;
}
