#!/bin/sh
# Builds and runs the core tests with any C++17 compiler (no Geode needed).
set -e
cd "$(dirname "$0")"
python3 make_fixtures.py
${CXX:-g++} -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined -DFIXTURES="\"$(pwd)/fixtures.json\"" \
    test_core.cpp ../src/core/json.cpp ../src/core/ap_core.cpp -o /tmp/apgd_tests
/tmp/apgd_tests
