#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>

/* ------------------------------------------------------------------ */
/*  Lightweight helpers — MQTT JSON payload and InfluxDB line protocol */
/*  Implemented here (no Arduino deps) to test string codec logic.    */
/* ------------------------------------------------------------------ */

struct MqttBatteryPayload {
  int bat;
  double v_mv;
  double i_a;
  double ah_d;
  double ah_c;
  std::string state;
};

// Minimal JSON builder matching firmware MQTT publish format
static std::string buildMqttBatteryJson(const MqttBatteryPayload &p) {
  std::ostringstream os;
  os << "{\"bat\":" << p.bat
     << ",\"v_mv\":" << p.v_mv
     << ",\"i_a\":" << p.i_a
     << ",\"ah_d\":" << p.ah_d
     << ",\"ah_c\":" << p.ah_c
     << ",\"state\":\"" << p.state << "\"}";
  return os.str();
}

// Minimal JSON field extractor — finds "key":value or "key":"value"
static std::string jsonExtract(const std::string &json, const std::string &key) {
  std::string needle = "\"" + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return "";
  pos += needle.size();
  if (json[pos] == '"') {
    // String value
    size_t end = json.find('"', pos + 1);
    return json.substr(pos + 1, end - pos - 1);
  }
  // Numeric value — up to next comma or closing brace
  size_t end = json.find_first_of(",}", pos);
  return json.substr(pos, end - pos);
}

// Extract battery index from MQTT topic like "bmu/battery/3" or "bmu/kxkm-01/battery/3"
static int parseBatteryIndexFromTopic(const std::string &topic) {
  // Find "battery/" and parse the integer after it
  const std::string marker = "battery/";
  size_t pos = topic.find(marker);
  if (pos == std::string::npos) return -1;
  pos += marker.size();
  if (pos >= topic.size()) return -1;
  // Parse integer
  int idx = 0;
  bool found = false;
  while (pos < topic.size() && topic[pos] >= '0' && topic[pos] <= '9') {
    idx = idx * 10 + (topic[pos] - '0');
    found = true;
    pos++;
  }
  return found ? idx : -1;
}

// Build InfluxDB line protocol string for a battery measurement
static std::string buildInfluxLineBattery(const std::string &device,
                                          int batIndex,
                                          int64_t voltage_mv,
                                          int64_t current_ma,
                                          const std::string &state,
                                          uint64_t timestamp_ns) {
  std::ostringstream os;
  os << "battery,device=" << device << ",bat=" << batIndex
     << " voltage_mv=" << voltage_mv << "i"
     << ",current_ma=" << current_ma << "i"
     << ",state=\"" << state << "\""
     << " " << timestamp_ns;
  return os.str();
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_mqtt_battery_payload_format() {
  MqttBatteryPayload p;
  p.bat = 1;
  p.v_mv = 27500.0;
  p.i_a = 1.5;
  p.ah_d = 5.0;
  p.ah_c = 2.0;
  p.state = "connected";

  std::string json = buildMqttBatteryJson(p);

  // Verify JSON contains all expected fields
  assert(json.find("\"bat\":1") != std::string::npos);
  assert(json.find("\"v_mv\":27500") != std::string::npos);
  assert(json.find("\"i_a\":1.5") != std::string::npos);
  assert(json.find("\"ah_d\":5") != std::string::npos);
  assert(json.find("\"ah_c\":2") != std::string::npos);
  assert(json.find("\"state\":\"connected\"") != std::string::npos);

  // Parse fields back and verify
  assert(jsonExtract(json, "bat") == "1");
  assert(jsonExtract(json, "state") == "connected");

  // Numeric extraction
  double v_mv = std::stod(jsonExtract(json, "v_mv"));
  assert(std::fabs(v_mv - 27500.0) < 0.01);

  double i_a = std::stod(jsonExtract(json, "i_a"));
  assert(std::fabs(i_a - 1.5) < 0.01);

  double ah_d = std::stod(jsonExtract(json, "ah_d"));
  assert(std::fabs(ah_d - 5.0) < 0.01);

  double ah_c = std::stod(jsonExtract(json, "ah_c"));
  assert(std::fabs(ah_c - 2.0) < 0.01);
}

static void test_mqtt_topic_parsing() {
  // Simple topic: bmu/battery/1
  assert(parseBatteryIndexFromTopic("bmu/battery/1") == 1);

  // Namespaced topic: bmu/kxkm-01/battery/3
  assert(parseBatteryIndexFromTopic("bmu/kxkm-01/battery/3") == 3);

  // Zero index
  assert(parseBatteryIndexFromTopic("bmu/battery/0") == 0);

  // Double-digit index
  assert(parseBatteryIndexFromTopic("bmu/battery/15") == 15);

  // Invalid: no battery segment
  assert(parseBatteryIndexFromTopic("bmu/status") == -1);

  // Invalid: battery/ with no number
  assert(parseBatteryIndexFromTopic("bmu/battery/") == -1);

  // Trailing path after index — still parses the number
  assert(parseBatteryIndexFromTopic("bmu/battery/7/status") == 7);
}

static void test_influx_line_protocol_battery() {
  std::string line = buildInfluxLineBattery(
      "bmu-01", 0, 27500, 1500, "connected", 1712500000000000000ULL);

  // Verify measurement name
  assert(line.substr(0, 8) == "battery,");

  // Verify tags
  assert(line.find("device=bmu-01") != std::string::npos);
  assert(line.find("bat=0") != std::string::npos);

  // Verify fields with integer suffix
  assert(line.find("voltage_mv=27500i") != std::string::npos);
  assert(line.find("current_ma=1500i") != std::string::npos);

  // Verify string field (quoted)
  assert(line.find("state=\"connected\"") != std::string::npos);

  // Verify timestamp at end
  assert(line.find("1712500000000000000") != std::string::npos);

  // Verify overall structure: measurement,tags fields timestamp
  // Tags section ends at first space, fields section ends at second space
  size_t firstSpace = line.find(' ');
  assert(firstSpace != std::string::npos);
  std::string tagSection = line.substr(0, firstSpace);
  assert(tagSection.find("battery,") == 0);
  assert(tagSection.find("device=") != std::string::npos);
  assert(tagSection.find("bat=") != std::string::npos);

  size_t secondSpace = line.find(' ', firstSpace + 1);
  assert(secondSpace != std::string::npos);
  std::string fieldSection = line.substr(firstSpace + 1,
                                         secondSpace - firstSpace - 1);
  assert(fieldSection.find("voltage_mv=") != std::string::npos);
  assert(fieldSection.find("current_ma=") != std::string::npos);
  assert(fieldSection.find("state=") != std::string::npos);

  std::string timestamp = line.substr(secondSpace + 1);
  assert(timestamp == "1712500000000000000");
}

int main() {
  test_mqtt_battery_payload_format();
  test_mqtt_topic_parsing();
  test_influx_line_protocol_battery();
  return 0;
}
