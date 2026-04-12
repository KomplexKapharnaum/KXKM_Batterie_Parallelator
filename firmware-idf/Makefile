.PHONY: hil-auto hil-manual hil-bench hil-validate

hil-auto:
	cd tests/hil && uv run pytest -v test_tb01_boot.py test_tb02_i2c.py test_tb03_mqtt.py test_tb11_ble.py test_tb12_replay.py

hil-manual:
	cd tests/hil && uv run pytest -v -s test_tb04_10_manual.py

hil-bench:
	cd tests/hil && BMU_BENCH_HOURS=72 uv run pytest -v -s test_tb13_bench_72h.py

hil-validate: hil-auto
	@echo ""
	@echo "Automated TBs done. Next steps:"
	@echo "  make hil-manual    (operator required)"
	@echo "  make hil-bench     (72h wall clock)"
