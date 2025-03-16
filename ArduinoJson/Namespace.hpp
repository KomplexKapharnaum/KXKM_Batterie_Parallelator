// ...existing code...

#define ARDUINOJSON_CONCAT_(a, b) a##b
#define ARDUINOJSON_CONCAT(a, b) ARDUINOJSON_CONCAT_(a, b)

#define ARDUINOJSON_BEGIN_PUBLIC_NAMESPACE \
  namespace ARDUINOJSON_CONCAT(ARDUINOJSON_NAMESPACE, _NS) {

// ...existing code...
