#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <chrono>

namespace digidaw::test {

struct TestCase {
    std::string name;
    std::string suite;
    std::function<void()> func;
};

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner r;
        return r;
    }

    void register_test(std::string suite, std::string name, std::function<void()> func) {
        tests_.push_back(TestCase{std::move(name), std::move(suite), std::move(func)});
    }

    int run_all() {
        size_t passed = 0;
        size_t failed = 0;

        std::cout << "\n=======================================================\n";
        std::cout << "  DigiDAW Automated Verification Suite                 \n";
        std::cout << "  Running " << tests_.size() << " test cases...        \n";
        std::cout << "=======================================================\n\n";

        auto suite_start = std::chrono::steady_clock::now();

        for (const auto& t : tests_) {
            std::cout << "  [" << t.suite << "] " << t.name << " ... ";
            current_test_failed_ = false;
            current_failure_msg_.clear();

            auto start = std::chrono::steady_clock::now();
            try {
                t.func();
            } catch (const std::exception& ex) {
                current_test_failed_ = true;
                current_failure_msg_ = std::string("Unhandled exception: ") + ex.what();
            } catch (...) {
                current_test_failed_ = true;
                current_failure_msg_ = "Unknown exception thrown";
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);

            if (!current_test_failed_) {
                std::cout << "\033[32mPASS\033[0m (" << elapsed.count() << " us)\n";
                passed++;
            } else {
                std::cout << "\033[31mFAIL\033[0m\n";
                std::cout << "    -> " << current_failure_msg_ << "\n";
                failed++;
            }
        }

        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - suite_start);

        std::cout << "\n-------------------------------------------------------\n";
        std::cout << "Summary: " << passed << " PASSED, " << failed << " FAILED ("
                  << total_elapsed.count() << " ms total)\n";
        std::cout << "Pass Rate: " << (tests_.empty() ? 100.0 : (passed * 100.0 / tests_.size())) << " %\n";
        std::cout << "=======================================================\n\n";

        return failed == 0 ? 0 : 1;
    }

    void fail(std::string msg) {
        current_test_failed_ = true;
        current_failure_msg_ = std::move(msg);
    }

    [[nodiscard]] bool has_current_failed() const noexcept {
        return current_test_failed_;
    }

private:
    std::vector<TestCase> tests_;
    bool current_test_failed_{false};
    std::string current_failure_msg_{""};
};

struct AutoRegisterTest {
    AutoRegisterTest(std::string suite, std::string name, std::function<void()> func) {
        TestRunner::instance().register_test(std::move(suite), std::move(name), std::move(func));
    }
};

#define TEST_CASE(suite, name) \
    static void test_func_##suite##_##name(); \
    static ::digidaw::test::AutoRegisterTest reg_##suite##_##name(#suite, #name, test_func_##suite##_##name); \
    static void test_func_##suite##_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            ::digidaw::test::TestRunner::instance().fail(std::string("Assertion failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            ::digidaw::test::TestRunner::instance().fail(std::string("Assertion failed: NOT ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            ::digidaw::test::TestRunner::instance().fail(std::string("Assertion failed: ") + #a + " == " + #b + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
            return; \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            ::digidaw::test::TestRunner::instance().fail(std::string("Assertion failed: |") + #a + " - " + #b + "| <= " + #eps + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
            return; \
        } \
    } while (0)

#define ASSERT_OK(res) \
    do { \
        if ((res).is_error()) { \
            ::digidaw::test::TestRunner::instance().fail(std::string("Expected OK, got error: ") + std::string((res).error().message()) + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
            return; \
        } \
    } while (0)

#define ASSERT_ERR(res, code) \
    do { \
        if ((res).is_ok() || (res).error_code() != (code)) { \
            ::digidaw::test::TestRunner::instance().fail(std::string("Expected error ") + #code + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
            return; \
        } \
    } while (0)

} // namespace digidaw::test
