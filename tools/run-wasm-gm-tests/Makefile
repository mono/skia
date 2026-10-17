.PHONY: test run_local

# Node.js 18.13+; uses only the existing express/body-parser dependencies, not Chromium.
# Keep the .disabled npm manifests disabled. NODE_PATH may point to an isolated dependency install.
test:
	node --test wasm-gm-server.test.js

run_local:
	rm -rf /tmp/wasm-gmtests/
	mkdir /tmp/wasm-gmtests/
	touch /tmp/wasm-gmtests/empty.txt
	node run-wasm-gm-tests --js_file ../../modules/canvaskit/build/wasm_gm_tests.js \
	--wasm_file ../../modules/canvaskit/build/wasm_gm_tests.wasm \
	--resources ../../resources \
	--known_hashes /tmp/wasm-gmtests/empty.txt \
	--output /tmp/wasm-gmtests/ \
	--use_gpu --timeout 600 \
	--manual_mode
