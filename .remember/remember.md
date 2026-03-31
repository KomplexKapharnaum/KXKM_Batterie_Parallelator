# Handoff

## State
Migration Arduino→ESP-IDF complete (Phase 0-9 + P0 fixes). 16 composants ESP-IDF + 5 crates Rust, tous sur `main`. Build LVGL a un bug avec GCC 14.2 (ESP-IDF v5.4) — la version LVGL est pinnee a ~9.1.0 dans `idf_component.yml`. Le dernier build fonctionnel (1.52 MB) a boote et affiche le tabview sur BOX-3. Managed components doivent etre re-telecharges apres clean (`rm -rf build managed_components`).

## Next
1. Build + flash le firmware complet avec Phase 8+9 (chart V/I, solar tab, BLE NimBLE). Commande : `cd firmware-idf && rm -rf build managed_components && idf.py set-target esp32s3 && idf.py build`
2. Si LVGL build echoue encore, pinner la version exacte qui marchait ou utiliser le BSP esp-box-3 officiel sans override_path
3. Tests ESP-IDF : ecrire des tests Unity pour bmu_protection (couverture actuelle ~0%)

## Context
- ESP-IDF v5.4 est a `~/esp/esp-idf/`. Console USB-JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`).
- BOX-3 flash: BOOT+EN sequence si `No serial data received`. Port: `/dev/cu.usbmodem3101`.
- Seule `lv_font_montserrat_14` est activee dans sdkconfig.
- `.gitignore` mis a jour pour exclure `managed_components/`, `build/`, `sdkconfig`, `.cache/`, `external/`.
- SWOT analyse: score 6.5/10, faiblesse principale = 0% test coverage ESP-IDF.
- Open source survey: 30+ projets documentes dans `docs/research/2026-03-31-open-source-survey.md`.
