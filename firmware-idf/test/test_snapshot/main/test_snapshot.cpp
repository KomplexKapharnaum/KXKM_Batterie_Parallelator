#ifndef NATIVE_TEST
#define NATIVE_TEST
#endif
#include <unity.h>
#include <string.h>
#include "bmu_types.h"

void setUp(void) {}
void tearDown(void) {}

void test_snapshot_zero_init(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL_UINT8(0, snap.nb_batteries);
    TEST_ASSERT_EQUAL_UINT16(0, snap.cycle_count);
    TEST_ASSERT_FALSE(snap.topology_ok);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.fleet_max_mv);
}

void test_snapshot_populate_single_battery(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.nb_batteries = 1;
    snap.topology_ok = true;
    snap.cycle_count = 42;
    snap.timestamp_ms = 12345;

    snap.battery[0].voltage_mv = 27500.0f;
    snap.battery[0].current_a = 3.5f;
    snap.battery[0].state = BMU_STATE_CONNECTED;
    snap.battery[0].health_score = 100;
    snap.battery[0].nb_switches = 0;
    snap.battery[0].balancer_active = false;

    snap.fleet_max_mv = 27500.0f;
    snap.fleet_mean_mv = 27500.0f;

    TEST_ASSERT_EQUAL_UINT8(1, snap.nb_batteries);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27500.0f, snap.battery[0].voltage_mv);
    TEST_ASSERT_EQUAL(BMU_STATE_CONNECTED, snap.battery[0].state);
}

void test_snapshot_fleet_max_ignores_disconnected(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.nb_batteries = 3;

    snap.battery[0].voltage_mv = 27000.0f;
    snap.battery[0].state = BMU_STATE_CONNECTED;
    snap.battery[1].voltage_mv = 28000.0f;
    snap.battery[1].state = BMU_STATE_DISCONNECTED;
    snap.battery[2].voltage_mv = 27500.0f;
    snap.battery[2].state = BMU_STATE_CONNECTED;

    float max_v = 0;
    for (int i = 0; i < snap.nb_batteries; i++) {
        if (snap.battery[i].state == BMU_STATE_CONNECTED ||
            snap.battery[i].state == BMU_STATE_RECONNECTING) {
            if (snap.battery[i].voltage_mv > max_v)
                max_v = snap.battery[i].voltage_mv;
        }
    }
    snap.fleet_max_mv = max_v;

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27500.0f, snap.fleet_max_mv);
}

void test_snapshot_max_batteries_fits(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.nb_batteries = BMU_MAX_BATTERIES;

    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        snap.battery[i].voltage_mv = 27000.0f + (float)i;
        snap.battery[i].state = BMU_STATE_CONNECTED;
        snap.battery[i].health_score = 100;
    }

    TEST_ASSERT_EQUAL_UINT8(32, snap.nb_batteries);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27031.0f, snap.battery[31].voltage_mv);
}

void test_snapshot_cmd_topology_changed(void) {
    bmu_cmd_t cmd;
    cmd.type = CMD_TOPOLOGY_CHANGED;
    cmd.payload.topology.nb_ina = 16;
    cmd.payload.topology.nb_tca = 4;

    TEST_ASSERT_EQUAL(CMD_TOPOLOGY_CHANGED, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(16, cmd.payload.topology.nb_ina);
    TEST_ASSERT_EQUAL_UINT8(4, cmd.payload.topology.nb_tca);
}

void test_snapshot_cmd_balance_request(void) {
    bmu_cmd_t cmd;
    cmd.type = CMD_BALANCE_REQUEST;
    cmd.payload.balance_req.battery_idx = 7;
    cmd.payload.balance_req.on = false;

    TEST_ASSERT_EQUAL(CMD_BALANCE_REQUEST, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(7, cmd.payload.balance_req.battery_idx);
    TEST_ASSERT_FALSE(cmd.payload.balance_req.on);
}

void test_snapshot_cmd_web_switch(void) {
    bmu_cmd_t cmd;
    cmd.type = CMD_WEB_SWITCH;
    cmd.payload.web_switch.battery_idx = 3;
    cmd.payload.web_switch.on = true;

    TEST_ASSERT_EQUAL(CMD_WEB_SWITCH, cmd.type);
    TEST_ASSERT_TRUE(cmd.payload.web_switch.on);
}

void test_device_global_idx_mapping(void) {
    bmu_device_t dev;
    dev.bus_id = 1;
    dev.local_idx = 5;
    dev.global_idx = 16 + 5;

    TEST_ASSERT_EQUAL_UINT8(21, dev.global_idx);
    TEST_ASSERT_EQUAL_UINT8(1, dev.bus_id);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_zero_init);
    RUN_TEST(test_snapshot_populate_single_battery);
    RUN_TEST(test_snapshot_fleet_max_ignores_disconnected);
    RUN_TEST(test_snapshot_max_batteries_fits);
    RUN_TEST(test_snapshot_cmd_topology_changed);
    RUN_TEST(test_snapshot_cmd_balance_request);
    RUN_TEST(test_snapshot_cmd_web_switch);
    RUN_TEST(test_device_global_idx_mapping);
    return UNITY_END();
}
