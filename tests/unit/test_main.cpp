#include "tests/test_helpers.h"

void run_types_tests();
void run_lexer_tests();
void run_parser_tests();
void run_csv_reader_tests();
void run_binder_tests();
void run_operators_tests();
void run_m2_out_of_core_tests();

int main() {
    std::cout << "Running Axiom Unit Tests..." << std::endl;

    try {
        run_types_tests();
        run_lexer_tests();
        run_parser_tests();
        run_csv_reader_tests();
        run_binder_tests();
        run_operators_tests();
        run_m2_out_of_core_tests();
    } catch (const std::exception& e) {
        std::cerr << "FATAL UNCAUGHT EXCEPTION: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "FATAL UNKNOWN EXCEPTION!" << std::endl;
        return 1;
    }

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

