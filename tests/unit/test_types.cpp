#include "tests/test_helpers.h"
#include "axiom/types/value.h"

void run_types_tests() {
    using namespace axiom;

    // 1. Basic Value creation
    Value v_null = Value::make_null();
    ASSERT_TRUE(v_null.is_null());
    ASSERT_EQ(v_null.kind(), TypeKind::Null);
    ASSERT_EQ(v_null.to_string(), "NULL");

    Value v_int = Value::make_bigint(42);
    ASSERT_FALSE(v_int.is_null());
    ASSERT_EQ(v_int.kind(), TypeKind::BigInt);
    ASSERT_EQ(v_int.get_bigint(), 42);
    ASSERT_EQ(v_int.to_string(), "42");

    Value v_dbl = Value::make_double(3.14);
    ASSERT_FALSE(v_dbl.is_null());
    ASSERT_EQ(v_dbl.kind(), TypeKind::Double);
    ASSERT_TRUE(std::abs(v_dbl.get_double() - 3.14) < 1e-6);

    Value v_str = Value::make_varchar("hello");
    ASSERT_FALSE(v_str.is_null());
    ASSERT_EQ(v_str.kind(), TypeKind::VarChar);
    ASSERT_EQ(v_str.get_varchar(), "hello");

    Value v_bool_t = Value::make_bool(true);
    Value v_bool_f = Value::make_bool(false);
    ASSERT_TRUE(v_bool_t.get_bool());
    ASSERT_FALSE(v_bool_f.get_bool());

    // 2. Three-valued Boolean logic
    // AND
    ASSERT_TRUE(v_bool_t.sql_and(v_bool_t).get_bool());
    ASSERT_FALSE(v_bool_t.sql_and(v_bool_f).get_bool());
    ASSERT_FALSE(v_bool_f.sql_and(v_null).get_bool()); // FALSE AND NULL -> FALSE
    ASSERT_TRUE(v_bool_t.sql_and(v_null).is_null());   // TRUE AND NULL -> NULL
    ASSERT_TRUE(v_null.sql_and(v_null).is_null());     // NULL AND NULL -> NULL

    // OR
    ASSERT_TRUE(v_bool_t.sql_or(v_bool_f).get_bool());
    ASSERT_TRUE(v_bool_t.sql_or(v_null).get_bool());   // TRUE OR NULL -> TRUE
    ASSERT_FALSE(v_bool_f.sql_or(v_bool_f).get_bool());
    ASSERT_TRUE(v_bool_f.sql_or(v_null).is_null());    // FALSE OR NULL -> NULL
    ASSERT_TRUE(v_null.sql_or(v_null).is_null());      // NULL OR NULL -> NULL

    // NOT
    ASSERT_FALSE(v_bool_t.sql_not().get_bool());
    ASSERT_TRUE(v_bool_f.sql_not().get_bool());
    ASSERT_TRUE(v_null.sql_not().is_null());           // NOT NULL -> NULL

    // IS NULL / IS NOT NULL
    ASSERT_TRUE(v_null.sql_is_null().get_bool());
    ASSERT_FALSE(v_null.sql_is_not_null().get_bool());
    ASSERT_FALSE(v_int.sql_is_null().get_bool());
    ASSERT_TRUE(v_int.sql_is_not_null().get_bool());

    // 3. Comparisons
    Value v10 = Value::make_bigint(10);
    Value v20 = Value::make_bigint(20);
    Value v10_dbl = Value::make_double(10.0);

    ASSERT_TRUE(v10.sql_equals(v10).get_bool());
    ASSERT_FALSE(v10.sql_equals(v20).get_bool());
    ASSERT_TRUE(v10.sql_less_than(v20).get_bool());
    ASSERT_TRUE(v10.sql_less_than_or_equal(v20).get_bool());
    ASSERT_TRUE(v20.sql_greater_than(v10).get_bool());
    ASSERT_TRUE(v10.sql_equals(v10_dbl).get_bool()); // Int-Double cross comparison

    // Null comparisons return NULL
    ASSERT_TRUE(v10.sql_equals(v_null).is_null());
    ASSERT_TRUE(v_null.sql_equals(v_null).is_null());
    ASSERT_TRUE(v10.sql_less_than(v_null).is_null());

    // String comparisons
    Value s1 = Value::make_varchar("apple");
    Value s2 = Value::make_varchar("banana");
    ASSERT_TRUE(s1.sql_less_than(s2).get_bool());
    ASSERT_TRUE(s1.sql_not_equals(s2).get_bool());

    // 4. Arithmetic
    Value v5 = Value::make_bigint(5);
    Value v2 = Value::make_bigint(2);
    Value v0 = Value::make_bigint(0);

    ASSERT_EQ(v5.add(v2).get_bigint(), 7);
    ASSERT_EQ(v5.subtract(v2).get_bigint(), 3);
    ASSERT_EQ(v5.multiply(v2).get_bigint(), 10);
    ASSERT_EQ(v5.divide(v2).get_bigint(), 2);
    ASSERT_EQ(v5.modulo(v2).get_bigint(), 1);
    ASSERT_EQ(v5.negate().get_bigint(), -5);

    // Division by zero throws
    ASSERT_THROWS(v5.divide(v0), AxiomException);
    ASSERT_THROWS(v5.modulo(v0), AxiomException);

    // Mixed arithmetic promotes to Double
    Value d2 = Value::make_double(2.5);
    Value v_add_mix = v5.add(d2);
    ASSERT_EQ(v_add_mix.kind(), TypeKind::Double);
    ASSERT_TRUE(std::abs(v_add_mix.get_double() - 7.5) < 1e-6);

    // Arithmetic with NULL yields NULL
    ASSERT_TRUE(v5.add(v_null).is_null());
    ASSERT_TRUE(v5.divide(v_null).is_null());

    // 5. Casts
    Value s_num = Value::make_varchar("12345");
    Value c_int = s_num.cast_to(TypeKind::BigInt);
    ASSERT_EQ(c_int.get_bigint(), 12345);

    Value s_float = Value::make_varchar("12.5");
    Value c_dbl = s_float.cast_to(TypeKind::Double);
    ASSERT_TRUE(std::abs(c_dbl.get_double() - 12.5) < 1e-6);

    Value s_bad = Value::make_varchar("not_a_number");
    ASSERT_THROWS(s_bad.cast_to(TypeKind::BigInt), AxiomException);

    // 6. Exact equality and hash
    ASSERT_TRUE(v_null.exact_equal(v_null));
    ASSERT_FALSE(v_null.exact_equal(v10));
    ASSERT_TRUE(v10.exact_equal(Value::make_bigint(10)));
}

