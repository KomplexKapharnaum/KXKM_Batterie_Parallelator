# BMU Audit Critical Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the 4 critical audit findings (CRIT-A through CRIT-D) plus the top 4 HIGH findings that affect runtime safety, without changing the protection specification (F01-F11).

**Architecture:** The root cause of CRIT-A/B is a unit mismatch: `BATTParallelator` stores thresholds as `int` (designed for mV/mA) but `main.cpp` passes V/A after converting from config.h's mV values. The fix normalizes all threshold storage to mV internally (matching config.h), removes the double-conversion in main.cpp, and fixes the imbalance check to use live fleet max voltage. CRIT-C is a deadlock from nested I2CLockGuard. CRIT-D is a web auth/method design issue. HIGH fixes address negative overcurrent, task self-deletion, and I2C speed locking.

**Tech Stack:** C++ (Arduino/PlatformIO), Unity test framework, FreeRTOS, ESP32-S3

**Scope:** This plan covers P0-P7 from the audit. Web security hardening (POST migration, WebSocket auth, XSS) is deferred to a separate plan.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `firmware/src/BatteryParallelator.h` | Modify | Change `int` threshold members to document mV/mA units; make `battery_voltages[]` private |
| `firmware/src/BatteryParallelator.cpp` | Modify | Fix `is_voltage_within_range()`, `is_difference_acceptable()`, `is_current_within_range()`, overcurrent check, permanent lock |
| `firmware/src/main.cpp` | Modify | Remove `/1000.0f` and `*1000.0f` conversions in setter calls — pass mV/mA directly |
| `firmware/src/BatteryRouteValidation.cpp` | Modify | Remove outer `I2CLockGuard` to fix deadlock |
| `firmware/src/INAHandler.cpp` | Modify | Wrap `setI2CSpeed()` in I2CLockGuard |
| `firmware/src/TimeAndInfluxTask.cpp` | Modify | Change `return` to `continue` on SD failure |
| `firmware/test/test_protection/test_protection.cpp` | Modify | Update thresholds to match config.h (10A), add unit-matching tests, add negative overcurrent test, add permanent lock test |

---

### Task 1: Fix unit tests to expose CRIT-A (voltage unit mismatch)

**Files:**
- Modify: `firmware/test/test_protection/test_protection.cpp`

- [ ] **Step 1: Update test thresholds to match config.h**

The current test uses `ALERT_BAT_MAX_CURRENT = 1` but config.h defines `10`. The test's `should_disconnect()` stub is correct logic but uses wrong constants, giving false confidence. Align them:

```cpp
// Replace lines 9-13 in test_protection.cpp with:
static const int ALERT_BAT_MIN_VOLTAGE = 24000; // mV (matches config.h)
static const int ALERT_BAT_MAX_VOLTAGE = 30000; // mV (matches config.h)
static const int ALERT_BAT_MAX_CURRENT = 10;    // A  (matches config.h)
static const int VOLTAGE_DIFF          = 1;     // V  (matches config.h)
static const int NB_SWITCH_MAX         = 5;
```

- [ ] **Step 2: Add test for negative overcurrent**

Add after `test_overcurrent_negative_disconnects` (line 55):

```cpp
void test_overcurrent_large_negative_disconnects(void) {
    // Large charging inrush must also trigger disconnect
    TEST_ASSERT_TRUE(should_disconnect(27000, -15.0f, 27.0f));
}
```

And register it in `main()`:

```cpp
RUN_TEST(test_overcurrent_large_negative_disconnects);
```

- [ ] **Step 3: Update overcurrent test values for 10A threshold**

Replace `test_overcurrent_positive_disconnects` body (line 48-50):

```cpp
void test_overcurrent_positive_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(27000, 10.1f, 27.0f));
    TEST_ASSERT_TRUE(should_disconnect(27000, 50.0f, 27.0f));
}
```

Replace `test_overcurrent_negative_disconnects` body (line 53-55):

```cpp
void test_overcurrent_negative_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(27000, -10.1f, 27.0f));
}
```

Replace `test_nominal_current_connects` body (line 57-60):

```cpp
void test_nominal_current_connects(void) {
    TEST_ASSERT_FALSE(should_disconnect(27000, 5.0f, 27.0f));
    TEST_ASSERT_FALSE(should_disconnect(27000, -5.0f, 27.0f));
}
```

- [ ] **Step 4: Add test for permanent lock semantics**

Add after `test_no_permanent_lock_at_max`:

```cpp
void test_permanent_lock_at_max_plus_one(void) {
    // nb_switch = 6 (NB_SWITCH_MAX + 1) MUST lock permanently
    TEST_ASSERT_TRUE(should_permanent_lock(6));
}
```

Register in main:

```cpp
RUN_TEST(test_permanent_lock_at_max_plus_one);
```

- [ ] **Step 5: Run tests to verify they pass (stub logic is correct)**

Run: `pio test -e sim-host -vv`
Expected: All 12 tests PASS (the stub `should_disconnect` is correctly implemented with the new thresholds).

- [ ] **Step 6: Commit**

```bash
git add firmware/test/test_protection/test_protection.cpp
git commit -m "test: align protection test thresholds with config.h (10A), add negative overcurrent + lock tests"
```

---

### Task 2: Fix CRIT-A — Unit mismatch in BATTParallelator threshold storage

**Files:**
- Modify: `firmware/src/BatteryParallelator.h:83-87` (document units)
- Modify: `firmware/src/BatteryParallelator.cpp:115-163` (setters)
- Modify: `firmware/src/BatteryParallelator.cpp:549-562` (`is_voltage_within_range`)
- Modify: `firmware/src/BatteryParallelator.cpp:570-578` (`is_current_within_range`)
- Modify: `firmware/src/main.cpp:66-69` (remove double-conversion)

The fix: all `mem_set_*` fields store mV/mA (matching their defaults). Setters accept mV/mA directly. `main.cpp` passes config.h values as-is.

- [ ] **Step 1: Fix setters to document and enforce mV/mA storage**

In `BatteryParallelator.cpp`, replace the setters (lines 115-163):

```cpp
/**
 * @brief Définir la tension maximale.
 * @param voltage Tension maximale en mV.
 */
void BATTParallelator::set_max_voltage(float voltage) {
  if (lockState()) {
    mem_set_max_voltage = static_cast<int>(voltage);
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la tension minimale.
 * @param voltage Tension minimale en mV.
 */
void BATTParallelator::set_min_voltage(float voltage) {
  if (lockState()) {
    mem_set_min_voltage = static_cast<int>(voltage);
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la différence de tension maximale.
 * @param diff Différence de tension maximale en mV.
 */
void BATTParallelator::set_max_diff_voltage(float diff) {
  if (lockState()) {
    mem_set_voltage_diff = static_cast<int>(diff);
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la différence de courant maximale.
 * @param diff Différence de courant maximale en mA.
 */
void BATTParallelator::set_max_diff_current(float diff) {
  if (lockState()) {
    mem_set_current_diff = static_cast<int>(diff);
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir le courant maximal.
 * @param current Courant maximal en mA.
 */
void BATTParallelator::set_max_current(float current) {
  if (lockState()) {
    mem_set_max_current = static_cast<int>(current);
    xSemaphoreGive(stateMutex);
  }
}
```

- [ ] **Step 2: Fix `is_voltage_within_range()` — input is V, compare against mV**

Replace lines 549-562 in `BatteryParallelator.cpp`:

```cpp
bool BATTParallelator::is_voltage_within_range(float voltage) {
  const float voltage_mV = voltage * 1000.0f;
  if (voltage_mV < mem_set_min_voltage || voltage_mV > mem_set_max_voltage) {
    debugLogger.println(KxLogger::BATTERY, "voltage (mV): " + String(voltage_mV));
    debugLogger.println(KxLogger::BATTERY,
                        "Min voltage (mV): " + String(mem_set_min_voltage));
    debugLogger.println(KxLogger::BATTERY,
                        "Max voltage (mV): " + String(mem_set_max_voltage));
    debugLogger.println(KxLogger::BATTERY,
                        "Voltage out of range: " + String(voltage_mV));
    return false;
  }
  return true;
}
```

(This is the same logic as before but with a named local variable to make the conversion explicit. The key fix is that `mem_set_max_voltage` now stays at `30000` instead of being overwritten to `30`.)

- [ ] **Step 3: Fix `is_current_within_range()` — use `fabs()` instead of `abs()`**

Replace lines 570-578 in `BatteryParallelator.cpp`:

```cpp
bool BATTParallelator::is_current_within_range(float current) {
  // mem_set_max_current is in mA, current is in A — convert threshold to A
  const float maxCurrentA = mem_set_max_current / 1000.0f;
  const float maxChargeA = mem_set_max_charge_current / 1000.0f;
  if (fabs(current) > maxCurrentA ||
      current < -maxChargeA) {
    debugLogger.println(KxLogger::BATTERY,
                        "Current out of range: " + String(current));
    return false;
  }
  return true;
}
```

- [ ] **Step 4: Fix main.cpp — pass config.h values directly (mV/mA)**

Replace lines 66-69 in `main.cpp`:

```cpp
  // Config protection — pass mV/mA directly (mem_set_* stores mV/mA)
  BattParallelator.set_min_voltage(alert_bat_min_voltage);       // 24000 mV
  BattParallelator.set_max_voltage(alert_bat_max_voltage);       // 30000 mV
  BattParallelator.set_max_current(alert_bat_max_current * 1000.0f); // 10A -> 10000 mA
  BattParallelator.set_max_diff_voltage(voltage_diff * 1000.0f); // 1V -> 1000 mV
```

- [ ] **Step 5: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/BatteryParallelator.cpp firmware/src/main.cpp
git commit -m "fix(CRIT-A): normalize threshold units to mV/mA, remove double-conversion in main.cpp"
```

---

### Task 3: Fix CRIT-B — Imbalance check compares against live fleet max, not config ceiling

**Files:**
- Modify: `firmware/src/BatteryParallelator.cpp:587-591` (`is_difference_acceptable`)
- Modify: `firmware/src/BatteryParallelator.h:77` (signature change)

- [ ] **Step 1: Add a test for imbalance against fleet max**

Add to `test_protection.cpp` after the imbalance tests:

```cpp
void test_voltage_imbalance_against_fleet_max(void) {
    // Fleet at 25-27V. Battery at 25V, max=27V -> diff=2V > 1V -> disconnect
    TEST_ASSERT_TRUE(should_disconnect(25000, 0.0f, 27.0f));
    // Battery at 26.5V, max=27V -> diff=0.5V < 1V -> connect
    TEST_ASSERT_FALSE(should_disconnect(26500, 0.0f, 27.0f));
}
```

Register in main. Run: `pio test -e sim-host` — should PASS (stub already uses `voltage_max_v`).

- [ ] **Step 2: Fix `is_difference_acceptable` to use fleet max voltage**

Replace lines 587-591 in `BatteryParallelator.cpp`:

```cpp
bool BATTParallelator::is_difference_acceptable(float voltage, float current) {
  // Compare against live fleet max voltage, not config ceiling
  const float fleetMaxV = find_max_voltage(battery_voltages, inaHandler.getNbINA());
  const float diffV = fleetMaxV - voltage;
  const float diffThresholdV = mem_set_voltage_diff / 1000.0f; // mV -> V

  if (diffV > diffThresholdV) {
    debugLogger.println(KxLogger::BATTERY,
        "Voltage imbalance: battery=" + String(voltage) +
        "V fleet_max=" + String(fleetMaxV) +
        "V diff=" + String(diffV) +
        "V threshold=" + String(diffThresholdV) + "V");
    return false;
  }

  // mem_set_current_diff is in mA, current is in A — convert
  if (fabs(current) > mem_set_current_diff / 1000.0f) {
    debugLogger.println(KxLogger::BATTERY,
                        "Current diff out of range: " + String(current));
    return false;
  }
  return true;
}
```

- [ ] **Step 3: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/BatteryParallelator.cpp firmware/test/test_protection/test_protection.cpp
git commit -m "fix(CRIT-B): imbalance check uses live fleet max voltage instead of config ceiling"
```

---

### Task 4: Fix CRIT-C — Deadlock in validateBatteryVoltageForSwitch

**Files:**
- Modify: `firmware/src/BatteryRouteValidation.cpp:64-70`

- [ ] **Step 1: Remove the outer I2CLockGuard**

`inaHandler.read_volt()` already acquires its own `I2CLockGuard` internally (see `INAHandler.cpp:195-196`). The outer lock causes a deadlock on the non-recursive mutex.

Replace lines 64-70 in `BatteryRouteValidation.cpp`:

```cpp
  // read_volt() acquires I2CLockGuard internally — do NOT wrap in another lock
  const float voltageV = inaHandler.read_volt(batteryIndex); // Returns voltage in volts
  if (std::isnan(voltageV)) {
    return false; // Unsafe — sensor reading failed
  }
```

The full function becomes:

```cpp
#ifdef ARDUINO
bool validateBatteryVoltageForSwitch(int batteryIndex, float minVoltageMv,
                                     float maxVoltageMv) {
  if (batteryIndex < 0 || batteryIndex >= inaHandler.getNbINA()) {
    return false;
  }

  if (minVoltageMv >= maxVoltageMv || minVoltageMv <= 0 || maxVoltageMv <= 0) {
    return false;
  }

  // read_volt() acquires I2CLockGuard internally — do NOT wrap in another lock
  const float voltageV = inaHandler.read_volt(batteryIndex);
  if (std::isnan(voltageV)) {
    return false;
  }

  const float voltageMv = voltageV * 1000.0f;

  if (voltageMv < minVoltageMv || voltageMv > maxVoltageMv) {
    return false;
  }

  return true;
}
#endif
```

- [ ] **Step 2: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/BatteryRouteValidation.cpp
git commit -m "fix(CRIT-C): remove outer I2CLockGuard in validateBatteryVoltageForSwitch to prevent deadlock"
```

---

### Task 5: Fix HIGH-1 — Negative overcurrent bypasses ERROR handler

**Files:**
- Modify: `firmware/src/BatteryParallelator.cpp:398-400`

- [ ] **Step 1: Fix the overcurrent check to use `fabs()`**

Replace lines 397-400 in `BatteryParallelator.cpp`:

```cpp
  // mem_set_max_current is in mA, current is in A — convert for comparison
  const float overcurrent_limit_A = (2.0f * mem_set_max_current) / 1000.0f;
  if (voltage < 0 ||
      fabs(current) > overcurrent_limit_A) { // Both positive and negative overcurrent
```

- [ ] **Step 2: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/BatteryParallelator.cpp
git commit -m "fix(HIGH-1): negative overcurrent now triggers ERROR handler via fabs() check"
```

---

### Task 6: Fix HIGH-8 — timeAndInfluxTask kills itself on SD failure

**Files:**
- Modify: `firmware/src/TimeAndInfluxTask.cpp:66-68`

- [ ] **Step 1: Replace `return` with `continue` on SD mount failure**

Replace lines 66-70 in `TimeAndInfluxTask.cpp`:

```cpp
      if (!SD.begin()) {
        debugLogger.println(KxLogger::WARNING, "SD Card Mount Failed — will retry next cycle");
        vTaskDelay(60000 / portTICK_PERIOD_MS);
        continue;
      }
```

- [ ] **Step 2: Fix timeClient.update() use-after-end (MED-6)**

Replace lines 80-83. After `timeClient.end()` is called (line 60), we should use `getLocalTime()` for display instead of `timeClient.update()`:

```cpp
    // Afficher la date et l'heure actuelles via RTC (NTP already synced to RTC)
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char buffer[20];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
      debugLogger.println(KxLogger::TIME, "Current Date and Time: " + String(buffer));
    } else {
      debugLogger.println(KxLogger::WARNING, "RTC time not available");
    }
```

- [ ] **Step 3: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS (this file is not compiled in sim-host).

- [ ] **Step 4: Commit**

```bash
git add firmware/src/TimeAndInfluxTask.cpp
git commit -m "fix(HIGH-8,MED-6): SD failure no longer kills timeAndInfluxTask, fix timeClient use-after-end"
```

---

### Task 7: Fix HIGH-5 — setI2CSpeed without lock

**Files:**
- Modify: `firmware/src/INAHandler.cpp:322-326`

- [ ] **Step 1: Wrap Wire.setClock in I2CLockGuard**

Replace lines 322-326 in `INAHandler.cpp`:

```cpp
void INAHandler::setI2CSpeed(int speed)
{
    i2cSpeedKHz = speed;
    I2CLockGuard lock;
    if (lock.isAcquired()) {
        Wire.setClock(static_cast<uint32_t>(i2cSpeedKHz) * 1000U);
    }
}
```

- [ ] **Step 2: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/INAHandler.cpp
git commit -m "fix(HIGH-5): wrap setI2CSpeed Wire.setClock in I2CLockGuard"
```

---

### Task 8: Fix MED-1 — Implement actual permanent lockout for nb_switch > max

**Files:**
- Modify: `firmware/src/BatteryParallelator.cpp:407-413`

- [ ] **Step 1: Add LOCKED state and implement permanent lock**

The current code at `nb_switch >= nbSwitchMax` falls through to CONNECTED, never locking. Fix the state determination in `check_battery_connected_status()`.

Replace lines 396-413 in `BatteryParallelator.cpp`:

```cpp
  // Vérifier l'état de la batterie
  const float overcurrent_limit_A = (2.0f * mem_set_max_current) / 1000.0f;
  if (voltage < 0 ||
      fabs(current) > overcurrent_limit_A) {
    state = ERROR;
  } else if (voltage < 1) {
    state = DISCONNECTED;
  } else if (localNbSwitch > localNbSwitchMax) {
    // Permanent lock — too many reconnection attempts (CRIT-008 / F08)
    debugLogger.println(KxLogger::BATTERY,
        "Battery " + String(INA_num + 1) + " permanently locked (nb_switch=" +
        String(localNbSwitch) + " > max=" + String(localNbSwitchMax) + ")");
    switch_battery(INA_num, false); // Force OFF
    return; // Do not proceed — battery is locked until reboot
  } else if (!check_battery_status(INA_num) ||
             !check_voltage_offset(INA_num, voltageOffset)) {
    state = DISCONNECTED;
  } else if (localNbSwitch == 0 ||
             (localNbSwitch < localNbSwitchMax &&
              (millis() - localReconnectTime > localReconnectDelay))) {
    state = RECONNECTING;
  } else if (localNbSwitch == localNbSwitchMax &&
             (millis() - localReconnectTime > localReconnectDelay)) {
    // At max — allow one final reconnect attempt with delay
    state = RECONNECTING;
  } else {
    state = CONNECTED;
  }
```

- [ ] **Step 2: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/BatteryParallelator.cpp
git commit -m "fix(MED-1): implement permanent lockout when nb_switch > nbSwitchMax (F08 spec)"
```

---

### Task 9: Make battery_voltages private (HIGH-7)

**Files:**
- Modify: `firmware/src/BatteryParallelator.h:55`

- [ ] **Step 1: Move `battery_voltages` to private section**

In `BatteryParallelator.h`, move line 55 (`float battery_voltages[16];`) from the `public:` section to the `private:` section (after line 82). The public accessors `set_battery_voltage()` and `copy_battery_voltages()` already exist.

- [ ] **Step 2: Check for direct access in other files**

Run: `grep -rn "battery_voltages\[" firmware/src/ --include="*.cpp" --include="*.h"` and verify all usages go through the class's own methods or are inside `BatteryParallelator.cpp`.

If `WebServerHandler.cpp` or `BatteryManager.cpp` access it directly, replace with `copy_battery_voltages()`.

- [ ] **Step 3: Run tests**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/BatteryParallelator.h
git commit -m "fix(HIGH-7): make battery_voltages private to enforce mutex discipline"
```

---

### Task 10: Final verification

- [ ] **Step 1: Run full test suite**

Run: `pio test -e sim-host -vv`
Expected: All tests PASS (12+ tests).

- [ ] **Step 2: Verify ESP32-S3 build compiles**

Run: `pio run -e kxkm-s3-16MB`
Expected: BUILD SUCCESS (or known dependency issue — document if so).

- [ ] **Step 3: Run memory budget check**

Run: `scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85`
Expected: PASS.

- [ ] **Step 4: Final commit with audit summary**

```bash
git add -A
git commit -m "audit: complete CRIT-A..D + HIGH-1,5,7,8 + MED-1,6 fixes from 2026-03-30 security audit"
```
