#include <math.h>
#include <stdlib.h>

#include <twiddle/hyperloglog/hyperloglog.h>

#include "test.h"

#include "../src/twiddle/hyperloglog/hyperloglog_simd.c"
#include "../src/twiddle/macrology.h"

bool estimate_within_error(double estimate, double real)
{
  const double diff = fabs(estimate - real);
  const double margin = real * 0.1;
  return diff < 10 * margin;
}

START_TEST(test_hyperloglog_basic)
{
  DESCRIBE_TEST;
  for (uint8_t p = TW_HLL_MIN_PRECISION; p <= TW_HLL_MAX_PRECISION; ++p) {
    const uint32_t n_registers = 1 << p;
    struct tw_hyperloglog *hll = tw_hyperloglog_new(p);
    ck_assert_ptr_ne(hll, NULL);

    double n_elems = 0.0;

    /** test linear_count */
    for (size_t k = 0; k < n_registers; ++k) {
      if (k % 2) {
        tw_hyperloglog_add(hll, (void *)&k, sizeof(k));
        n_elems += 1.0;
      }
    }
    bool within_error =
        estimate_within_error(tw_hyperloglog_count(hll), n_elems);
    ck_assert(within_error);

    /** test loglog */
    n_elems = 0;
    for (size_t k = 0; k < 10 * n_registers; ++k) {
      if (k % 2) {
        tw_hyperloglog_add(hll, (void *)&k, sizeof(k));
        n_elems += 1.0;
      }
    }
    within_error = estimate_within_error(tw_hyperloglog_count(hll), n_elems);
    ck_assert_msg(within_error, "estimate %f not within bounds",
                  tw_hyperloglog_count(hll));
    tw_hyperloglog_free(hll);
  }
}
END_TEST

START_TEST(test_hyperloglog_copy_and_clone)
{
  DESCRIBE_TEST;
  for (uint8_t p = TW_HLL_MIN_PRECISION; p <= TW_HLL_MAX_PRECISION; ++p) {
    const uint32_t n_registers = 1 << p;
    struct tw_hyperloglog *hll = tw_hyperloglog_new(p);
    struct tw_hyperloglog *copy = tw_hyperloglog_new(p);

    /** test linear_count */
    for (size_t k = 0; k < n_registers; ++k) {
      if (k % 2) {
        tw_hyperloglog_add(hll, (void *)&k, sizeof(k));
      }
    }

    ck_assert_ptr_ne(tw_hyperloglog_copy(hll, copy), NULL);
    ck_assert(
        tw_almost_equal(tw_hyperloglog_count(hll), tw_hyperloglog_count(copy)));

    struct tw_hyperloglog *clone = tw_hyperloglog_clone(copy);
    ck_assert_ptr_ne(clone, NULL);
    ck_assert(tw_hyperloglog_equal(hll, clone));
    ck_assert(tw_almost_equal(tw_hyperloglog_count(hll),
                              tw_hyperloglog_count(clone)));

    tw_hyperloglog_free(clone);
    tw_hyperloglog_free(copy);
    tw_hyperloglog_free(hll);
  }
}
END_TEST

START_TEST(test_hyperloglog_merge)
{
  DESCRIBE_TEST;
  for (uint8_t p = TW_HLL_MIN_PRECISION; p <= TW_HLL_MAX_PRECISION; ++p) {
    const uint32_t n_registers = 1 << p;
    struct tw_hyperloglog *src = tw_hyperloglog_new(p);
    struct tw_hyperloglog *dst = tw_hyperloglog_new(p);
    struct tw_hyperloglog *prev;

    const int times = 100;
    /** test linear_count */
    for (size_t k = 0; k < times * n_registers; ++k) {
      if (k % 2) {
        tw_hyperloglog_add(src, (void *)&k, sizeof(k));
      } else {
        tw_hyperloglog_add(dst, (void *)&k, sizeof(k));
      }
    }

    prev = tw_hyperloglog_clone(dst);

    ck_assert_ptr_ne(tw_hyperloglog_merge(src, dst), NULL);

    // merge should guarantee size increase
    ck_assert(tw_hyperloglog_count(src) <= tw_hyperloglog_count(dst));
    ck_assert(tw_hyperloglog_count(prev) <= tw_hyperloglog_count(dst));

    double estimate = tw_hyperloglog_count(dst);
    bool within_bound =
        estimate_within_error(estimate, (double)times * n_registers);

    ck_assert_msg(within_bound, "%d not within bounds",
                  tw_hyperloglog_count(dst));

    tw_hyperloglog_free(dst);
    tw_hyperloglog_free(src);
    tw_hyperloglog_free(prev);
  }
}
END_TEST

START_TEST(test_hyperloglog_simd)
{
  DESCRIBE_TEST;
  for (uint8_t p = TW_HLL_MIN_PRECISION; p <= TW_HLL_MAX_PRECISION; ++p) {
    const uint32_t n_registers = 1 << p;
    struct tw_hyperloglog *hll = tw_hyperloglog_new(p);
    ck_assert_ptr_ne(hll, NULL);

    double n_elems = 0.0;

    /** test linear_count */
    for (size_t k = 0; k < n_registers; ++k) {
      if (k % 2) {
        tw_hyperloglog_add(hll, (void *)&k, sizeof(k));
        n_elems += 1.0;
      }
    }
    bool within_error =
        estimate_within_error(tw_hyperloglog_count(hll), n_elems);
    ck_assert(within_error);

    /* Verify that the SIMD implementation computes the same
     * n_zeros/inverse_sum than the naive (correct) version */
    uint32_t n_zeros_1 = 0, n_zeros_2 = 0;
    float sum_1 = 0.0, sum_2 = 0.0;
    hyperloglog_count_port(hll->registers, n_registers, &sum_1, &n_zeros_1);
#ifdef USE_AVX2
    hyperloglog_count_avx2(hll->registers, n_registers, &sum_2, &n_zeros_2);
#elif defined USE_AVX
    hyperloglog_count_avx(hll->registers, n_registers, &sum_2, &n_zeros_2);
#else
    hyperloglog_count_port(hll->registers, n_registers, &sum_2, &n_zeros_2);
#endif
    ck_assert_uint32_t_eq(n_zeros_1, n_zeros_2);
    /* float sums is _not_ associative, thus it might differ a bit when using
     * SIMD operations */
    ck_assert(fabs(sum_1 - sum_2) < sum_1 * 0.00001);

    /** test loglog */
    n_elems = 0;
    for (size_t k = 0; k < 10 * n_registers; ++k) {
      if (k % 2) {
        tw_hyperloglog_add(hll, (void *)&k, sizeof(k));
        n_elems += 1.0;
      }
    }
    within_error = estimate_within_error(tw_hyperloglog_count(hll), n_elems);
    ck_assert_msg(within_error, "estimate %f not within bounds",
                  tw_hyperloglog_count(hll));

    n_zeros_1 = 0, n_zeros_2 = 0;
    sum_1 = 0.0, sum_2 = 0.0;
    hyperloglog_count_port(hll->registers, n_registers, &sum_1, &n_zeros_1);
#ifdef USE_AVX2
    hyperloglog_count_avx2(hll->registers, n_registers, &sum_2, &n_zeros_2);
#elif defined USE_AVX
    hyperloglog_count_avx(hll->registers, n_registers, &sum_2, &n_zeros_2);
#else
    hyperloglog_count_port(hll->registers, n_registers, &sum_2, &n_zeros_2);
#endif
    ck_assert_uint32_t_eq(n_zeros_1, n_zeros_2);
    /* float sums is _not_ associative, thus it might differ a bit when using
     * SIMD operations */
    ck_assert(fabs(sum_1 - sum_2) < sum_1 * 0.0001);

    tw_hyperloglog_free(hll);
  }
}
END_TEST

START_TEST(test_hyperloglog_errors)
{
  DESCRIBE_TEST;

  const uint8_t a_precision = TW_HLL_MIN_PRECISION,
                b_precision = TW_HLL_MIN_PRECISION + 1;

  struct tw_hyperloglog *a = tw_hyperloglog_new(a_precision);
  struct tw_hyperloglog *b = tw_hyperloglog_new(b_precision);

  ck_assert_ptr_eq(tw_hyperloglog_new(TW_HLL_MIN_PRECISION - 1), NULL);
  ck_assert_ptr_eq(tw_hyperloglog_new(TW_HLL_MAX_PRECISION + 1), NULL);

  ck_assert_ptr_eq(tw_hyperloglog_copy(a, b), NULL);
  ck_assert_ptr_eq(tw_hyperloglog_clone(NULL), NULL);

  tw_hyperloglog_add(NULL, NULL, 0);
  tw_hyperloglog_add(a, NULL, 1);
  tw_hyperloglog_add(a, &a_precision, 0);
  tw_hyperloglog_add(a, &a_precision, 1);

  tw_hyperloglog_count(NULL);

  ck_assert(!tw_hyperloglog_equal(a, b));
  ck_assert(!tw_hyperloglog_equal(NULL, b));
  ck_assert(!tw_hyperloglog_equal(a, NULL));

  ck_assert_ptr_eq(tw_hyperloglog_merge(a, b), NULL);
  ck_assert_ptr_eq(tw_hyperloglog_merge(a, NULL), NULL);
  ck_assert_ptr_eq(tw_hyperloglog_merge(NULL, b), NULL);

  tw_hyperloglog_free(NULL);
  tw_hyperloglog_free(b);
  tw_hyperloglog_free(a);
}
END_TEST

int run_tests()
{
  int number_failed;

  Suite *s = suite_create("hyperloglog");
  SRunner *runner = srunner_create(s);
  TCase *tc = tcase_create("basic");
  tcase_add_test(tc, test_hyperloglog_basic);
  tcase_add_test(tc, test_hyperloglog_copy_and_clone);
  tcase_add_test(tc, test_hyperloglog_merge);
  tcase_add_test(tc, test_hyperloglog_simd);
  tcase_add_test(tc, test_hyperloglog_errors);
  tcase_set_timeout(tc, 15);
  suite_add_tcase(s, tc);
  srunner_run_all(runner, CK_NORMAL);
  number_failed = srunner_ntests_failed(runner);
  srunner_free(runner);

  return number_failed;
}

int main() { return (run_tests() == 0) ? EXIT_SUCCESS : EXIT_FAILURE; }
