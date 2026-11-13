#include "tests/test_helpers.h"

void run_types_tests();
void run_lexer_tests();
void run_parser_tests();
void run_csv_reader_tests();
void run_binder_tests();
void run_operators_tests();

int main() {
    std::cout << "Running Axiom Unit Tests...\n";

    run_types_tests();
    run_lexer_tests();
    run_parser_tests();
    run_csv_reader_tests();
    run_binder_tests();
    run_operators_tests();

    std::cout << "\n=========================================\n";
    std::cout << "Unit Tests Finished: " << axiom::testing::g_test_count << " checks run.\n";
    if (axiom::testing::g_fail_count == 0) {
        std::cout << "ALL UNIT TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << "FAILURES: " << axiom::testing::g_fail_count << " checks failed!\n";
        return 1;
    }
}
