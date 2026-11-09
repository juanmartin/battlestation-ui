.PHONY: ci ci-quick host firmware asan device device-soak clean

ci:
	./tools/ci.sh

ci-quick:
	./tools/ci.sh --quick

host:
	./tools/ci.sh --host-only

firmware:
	./tools/ci.sh --firmware-only

asan:
	./tools/ci.sh --host-only --asan

device:
	./tools/ci.sh --device --soak-seconds 60

device-soak:
	./tools/ci.sh --device --soak-seconds 615

clean:
	rm -rf build-host-ci build-host-asan build-ci build-ci-wifi build-ci-device sdkconfig.ci sdkconfig.ci-wifi sdkconfig.ci-device
	rm -f build-ci.log build-ci-wifi.log build-ci-device.log build-ci-device-flash.log build-ci-device-serial.log
