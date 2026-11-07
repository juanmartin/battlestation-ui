.PHONY: ci ci-quick host firmware asan clean

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

clean:
	rm -rf build-host-ci build-host-asan build-ci build-ci-wifi sdkconfig.ci sdkconfig.ci-wifi
	rm -f build-ci.log build-ci-wifi.log
