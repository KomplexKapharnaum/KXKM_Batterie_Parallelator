#include "InfluxBufferCodec.h"

#include <cctype>

namespace {
std::string trimCopy(const std::string &in) {
  size_t first = 0;
  while (first < in.size() && std::isspace(static_cast<unsigned char>(in[first]))) {
    ++first;
  }
  if (first >= in.size()) {
    return "";
  }
  size_t last = in.size() - 1;
  while (last > first && std::isspace(static_cast<unsigned char>(in[last]))) {
    --last;
  }
  return in.substr(first, last - first + 1);
}

bool parseKv(const std::string &kv, std::string &key, std::string &value) {
  const size_t eq = kv.find('=');
  if (eq == std::string::npos || eq == 0 || eq == kv.size() - 1) {
    return false;
  }
  key = kv.substr(0, eq);
  value = kv.substr(eq + 1);
  return !key.empty() && !value.empty();
}
} // namespace

bool parseInfluxBufferLine(const std::string &line, ParsedInfluxBufferLine &out) {
  const std::string trimmed = trimCopy(line);
  if (trimmed.empty()) {
    return false;
  }

  const size_t firstComma = trimmed.find(',');
  if (firstComma == std::string::npos || firstComma == 0 || firstComma >= trimmed.size() - 1) {
    return false;
  }

  const size_t secondComma = trimmed.find(',', firstComma + 1);
  if (secondComma == std::string::npos || secondComma <= firstComma + 1 || secondComma >= trimmed.size() - 1) {
    return false;
  }

  out.measurement = trimmed.substr(0, firstComma);
  out.fields.clear();

  const std::string tagPart = trimmed.substr(firstComma + 1, secondComma - firstComma - 1);
  if (!parseKv(tagPart, out.tag.key, out.tag.value)) {
    return false;
  }

  const std::string fieldsPart = trimmed.substr(secondComma + 1);
  size_t start = 0;
  while (start < fieldsPart.size()) {
    const size_t comma = fieldsPart.find(',', start);
    const size_t end = (comma == std::string::npos) ? fieldsPart.size() : comma;
    const std::string field = fieldsPart.substr(start, end - start);
    std::string key;
    std::string value;
    if (!parseKv(field, key, value)) {
      return false;
    }
    out.fields.push_back({key, value});

    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }

  return !out.measurement.empty() && !out.fields.empty();
}
