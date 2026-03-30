#ifndef INFLUX_BUFFER_CODEC_H
#define INFLUX_BUFFER_CODEC_H

#include <string>
#include <vector>

struct InfluxTagKV {
  std::string key;
  std::string value;
};

struct InfluxFieldKV {
  std::string key;
  std::string value;
};

struct ParsedInfluxBufferLine {
  std::string measurement;
  InfluxTagKV tag;
  std::vector<InfluxFieldKV> fields;
};

bool parseInfluxBufferLine(const std::string &line, ParsedInfluxBufferLine &out);

#endif // INFLUX_BUFFER_CODEC_H
