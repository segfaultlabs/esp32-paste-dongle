#!/usr/bin/env bash
# Integration test for the WiFi OTA /api/update endpoint.
# Run against a running PasteDongle at $DEVICE_IP (default 192.168.4.1).
#
# Expected behaviour after the fix:
#   - Empty body POST returns HTTP 500 and {"ok":false}
#   - Small (<8192 bytes) body POST returns HTTP 500 and {"ok":false}
#   - Valid firmware upload returns HTTP 200 and {"ok":true,"reboot":true}
#     (this test is optional; use the curl command in docs for real flashing)

set -euo pipefail

DEVICE_IP="${DEVICE_IP:-192.168.4.1}"
ENDPOINT="http://${DEVICE_IP}/api/update"

echo "Testing OTA endpoint: ${ENDPOINT}"
echo

fail_count=0

# Helper: send a request and check that the response contains ok:false and a non-2xx code.
expect_rejection() {
    local desc="$1"
    local extra_args="$2"
    local expected_code="$3"

    echo "[TEST] ${desc}"
    local http_code body
    body=$(curl -s -X POST ${extra_args} "${ENDPOINT}" -w "\n%{http_code}" || true)
    http_code=$(echo "${body}" | tail -n1)
    body=$(echo "${body}" | sed '$d')

    if [ "${http_code}" != "${expected_code}" ]; then
        echo "  FAIL: expected HTTP ${expected_code}, got ${http_code}"
        echo "  body: ${body}"
        fail_count=$((fail_count + 1))
        return
    fi

    if ! echo "${body}" | grep -q '"ok":false'; then
        echo "  FAIL: response did not reject the update. body: ${body}"
        fail_count=$((fail_count + 1))
        return
    fi

    echo "  PASS: HTTP ${http_code}, ${body}"
}

# 1. Empty body must be rejected (this used to reboot the device).
expect_rejection "Empty POST body" "--data-binary ''" "500"

# 2. Multipart file smaller than minimum firmware size must be rejected.
#    AsyncWebServer only invokes the upload handler for multipart/form-data,
#    so this is the only way to exercise the size check.
SMALL_FILE=$(mktemp /tmp/ota_small.XXXXXX)
head -c 100 /dev/zero >"${SMALL_FILE}"
expect_rejection "Firmware too small (<8192 bytes)" "-F file=@${SMALL_FILE}" "500"
rm -f "${SMALL_FILE}"

echo
if [ "${fail_count}" -eq 0 ]; then
    echo "All OTA rejection tests passed."
else
    echo "${fail_count} test(s) failed."
    exit 1
fi
