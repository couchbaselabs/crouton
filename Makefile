# Makefile! Just a launcher for scripts...

all: debug release

clean:
	rm -r build_cmake

clean_cache:
	# Clear CMake caches. May be necessary after upgrading Xcode.
	rm -rf build_cmake/*/CMake*


debug:
	mkdir -p build_cmake/debug/
	cd build_cmake/debug && cmake -DCMAKE_BUILD_TYPE=Debug ../..
	cd build_cmake/debug && cmake --build .

test: debug
	build_cmake/debug/CroutonTests -r consoleplus

release:
	mkdir -p build_cmake/release/
	cd build_cmake/release && cmake -DCMAKE_BUILD_TYPE=MinSizeRel ../..
	cd build_cmake/release && cmake --build . --target LibCrouton --target uv_a --target mbedtls

xcode_deps: debug release
	mkdir -p build_cmake/debug/xcodedeps
	cp build_cmake/debug/vendor/libuv/libuv*.a build_cmake/debug/xcodedeps/
	cp build_cmake/debug/vendor/mbedtls/library/libmbed*.a build_cmake/debug/xcodedeps/
	mkdir -p build_cmake/release/xcodedeps
	cp build_cmake/release/vendor/libuv/libuv*.a build_cmake/release/xcodedeps/
	cp build_cmake/release/vendor/mbedtls/library/libmbed*.a build_cmake/release/xcodedeps/

esp:
	cd tests/ESP32 && idf.py build

esptest:
	cd tests/ESP32 && idf.py build flash monitor
