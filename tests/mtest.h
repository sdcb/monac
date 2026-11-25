#ifndef MONAC_TEST_MTEST_H
#define MONAC_TEST_MTEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int mtest_failures;
static int mtest_passed;

#define MT_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            ++mtest_failures; \
        } \
    } while (0)

#define MT_ASSERT_EQ(expected, actual) \
    do { \
        long long mt_expected_ = (long long)(expected); \
        long long mt_actual_ = (long long)(actual); \
        if (mt_expected_ != mt_actual_) { \
            fprintf(stderr, "%s:%d: expected %lld, got %lld (%s vs %s)\n", \
                    __FILE__, __LINE__, mt_expected_, mt_actual_, #expected, #actual); \
            ++mtest_failures; \
        } \
    } while (0)

#define MT_ASSERT_STR_EQ(expected, actual) \
    do { \
        const char *mt_e_ = (expected); \
        const char *mt_a_ = (actual); \
        if ((mt_e_ == NULL) != (mt_a_ == NULL) || \
            (mt_e_ != NULL && strcmp(mt_e_, mt_a_) != 0)) { \
            fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", \
                    __FILE__, __LINE__, mt_e_ ? mt_e_ : "(null)", mt_a_ ? mt_a_ : "(null)"); \
            ++mtest_failures; \
        } \
    } while (0)

#define MT_ASSERT_WSTR_EQ(expected, actual) \
    do { \
        const wchar_t *mt_e_ = (expected); \
        const wchar_t *mt_a_ = (actual); \
        if ((mt_e_ == NULL) != (mt_a_ == NULL) || \
            (mt_e_ != NULL && wcscmp(mt_e_, mt_a_) != 0)) { \
            fprintf(stderr, "%s:%d: wide string mismatch\n", __FILE__, __LINE__); \
            ++mtest_failures; \
        } \
    } while (0)

#define MT_ASSERT_NEAR(expected, actual, eps) \
    do { \
        double mt_e_ = (double)(expected); \
        double mt_a_ = (double)(actual); \
        if (fabs(mt_e_ - mt_a_) > (double)(eps)) { \
            fprintf(stderr, "%s:%d: expected %f +- %f, got %f\n", \
                    __FILE__, __LINE__, mt_e_, (double)(eps), mt_a_); \
            ++mtest_failures; \
        } \
    } while (0)

#define MT_RUN(test_fn) \
    do { \
        int mt_before_ = mtest_failures; \
        test_fn(); \
        if (mtest_failures == mt_before_) ++mtest_passed; \
    } while (0)

#define MT_FINISH() \
    (mtest_failures == 0 ? 0 : 1)

#endif
