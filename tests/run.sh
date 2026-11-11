#!/bin/sh
# ===========================================================================
# MDS_Weather - https://home.marcspencer.com/mds-weather
#
# Copyright (c) 2026 Marc D. Spencer.
# All Rights Reserved under the applicable license.
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# ===========================================================================

set -eu
TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${ARDUINOJSON_SRC:?Set ARDUINOJSON_SRC to ArduinoJson 7 src}"
TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$TEST_BUILD"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -I"$TEST_DIR/host" -I"$TEST_DIR/../src" -I"$ARDUINOJSON_SRC" "$TEST_DIR/weather.cpp" "$TEST_DIR/../src/MDS_Weather.cpp" -o "$TEST_BUILD/weather"
"$TEST_BUILD/weather"

c++ -std=c++17 -Wall -Wextra -I"$TEST_DIR/host" -I"$TEST_DIR/../src" -I"$ARDUINOJSON_SRC" "$TEST_DIR/errors.cpp" "$TEST_DIR/../src/MDS_Weather.cpp" -o "$TEST_BUILD/errors"
"$TEST_BUILD/errors"

c++ -std=c++17 -Wall -Wextra -I"$TEST_DIR/host" -I"$TEST_DIR/../src" -I"$ARDUINOJSON_SRC" "$TEST_DIR/readiness.cpp" "$TEST_DIR/../src/MDS_Weather.cpp" -o "$TEST_BUILD/readiness"
"$TEST_BUILD/readiness"
