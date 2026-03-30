#pragma once
// credentials.h — private, do NOT commit to git.
// Copy credentials.h.example -> credentials.h and fill in your values.

// InfluxDB
const char *influxDBServerUrl = "https://us-east-1-1.aws.cloud2.influxdata.com";
const char *influxDBOrg = "YOUR_ORG_ID";
const char *influxDBBucket = "BATT_PARALLELATOR";
const char *influxDBToken = "YOUR_INFLUXDB_TOKEN";
// TLS MUST be enabled for cloud communication — never set to true in production
const bool influxDBInsecure = false;

// Grafana (optional)
// const char *grafanaToken = "YOUR_GRAFANA_TOKEN";
