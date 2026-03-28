#include <cassert>

#include "../../src/BatteryRouteValidation.h"

static void test_parseBatteryIndex_valid_zero() {
  int index = -1;
  assert(parseBatteryIndex("0", 4, index));
  assert(index == 0);
}

static void test_parseBatteryIndex_valid_upper_bound_minus_one() {
  int index = -1;
  assert(parseBatteryIndex("15", 16, index));
  assert(index == 15);
}

static void test_parseBatteryIndex_reject_missing_or_empty() {
  int index = -1;
  assert(!parseBatteryIndex("", 4, index));
  assert(!parseBatteryIndex("   ", 4, index));
}

static void test_parseBatteryIndex_reject_non_numeric() {
  int index = -1;
  assert(!parseBatteryIndex("abc", 4, index));
  assert(!parseBatteryIndex("1x", 4, index));
}

static void test_parseBatteryIndex_reject_negative() {
  int index = -1;
  assert(!parseBatteryIndex("-1", 4, index));
}

static void test_parseBatteryIndex_reject_out_of_range() {
  int index = -1;
  assert(!parseBatteryIndex("4", 4, index));
  assert(!parseBatteryIndex("2147483647", 16, index));
}

static void test_parseBatteryIndex_reject_when_no_detected_battery() {
  int index = -1;
  assert(!parseBatteryIndex("0", 0, index));
}

int main() {
  test_parseBatteryIndex_valid_zero();
  test_parseBatteryIndex_valid_upper_bound_minus_one();
  test_parseBatteryIndex_reject_missing_or_empty();
  test_parseBatteryIndex_reject_non_numeric();
  test_parseBatteryIndex_reject_negative();
  test_parseBatteryIndex_reject_out_of_range();
  test_parseBatteryIndex_reject_when_no_detected_battery();
  return 0;
}
