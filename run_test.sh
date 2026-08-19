#!/bin/bash
cd /home/valou/leo/ecu_studio_suite/build/tests/unit
test_exe="./test_winols_parser"
if [ ! -f "$test_exe" ]; then
    echo "Test executable not found"
    exit 1
fi
# Try to check what's actually in the test
echo "Test binary info:"
file "$test_exe"
echo "Running tests:"
"$test_exe" --verbose 2>&1
