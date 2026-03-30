#include <cassert>
#include <string>

#include "../../src/InfluxBufferCodec.h"

static void test_parse_valid_line_with_three_fields() {
  ParsedInfluxBufferLine parsed;
  const bool ok = parseInfluxBufferLine(
      "battery_data,battery=3,voltage=28.4,current=1.2,temperature=24.5",
      parsed);

  assert(ok);
  assert(parsed.measurement == "battery_data");
  assert(parsed.tag.key == "battery");
  assert(parsed.tag.value == "3");
  assert(parsed.fields.size() == 3);
  assert(parsed.fields[0].key == "voltage");
  assert(parsed.fields[0].value == "28.4");
  assert(parsed.fields[1].key == "current");
  assert(parsed.fields[1].value == "1.2");
  assert(parsed.fields[2].key == "temperature");
  assert(parsed.fields[2].value == "24.5");
}

static void test_parse_rejects_malformed_tag_segment() {
  ParsedInfluxBufferLine parsed;
  const bool ok = parseInfluxBufferLine(
      "battery_data,battery,voltage=28.4,current=1.2", parsed);
  assert(!ok);
}

static void test_parse_rejects_malformed_field_segment() {
  ParsedInfluxBufferLine parsed;
  const bool ok = parseInfluxBufferLine(
      "battery_data,battery=3,voltage=28.4,current", parsed);
  assert(!ok);
}

static void test_parse_rejects_empty_or_whitespace_line() {
  ParsedInfluxBufferLine parsed;
  assert(!parseInfluxBufferLine("", parsed));
  assert(!parseInfluxBufferLine("   \t  ", parsed));
}

static void test_parse_accepts_trimmed_line() {
  ParsedInfluxBufferLine parsed;
  const bool ok = parseInfluxBufferLine(
      "  battery_data,battery=9,voltage=27.0,current=-0.6  ", parsed);

  assert(ok);
  assert(parsed.measurement == "battery_data");
  assert(parsed.tag.key == "battery");
  assert(parsed.tag.value == "9");
  assert(parsed.fields.size() == 2);
}

int main() {
  test_parse_valid_line_with_three_fields();
  test_parse_rejects_malformed_tag_segment();
  test_parse_rejects_malformed_field_segment();
  test_parse_rejects_empty_or_whitespace_line();
  test_parse_accepts_trimmed_line();
  return 0;
}
