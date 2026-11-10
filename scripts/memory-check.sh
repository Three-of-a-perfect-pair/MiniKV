#!/bin/sh

echo "======================================"
echo " MiniKV - Memory Check"
echo "======================================"

echo "[CHECK] Running Valgrind..."

valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --errors-for-leak-kinds=definite,indirect \
    --error-exitcode=2 \
    ./program

status=$?

echo "======================================"

case $status in
    0)
        echo "[PASS] No memory errors detected."
        ;;
    1)
        echo "[WARN] Functional errors detected by MiniKV."
        echo "[PASS] No memory errors detected by Valgrind."
        ;;
    2)
        echo "[FAIL] Memory errors detected by Valgrind."
        exit 1
        ;;
    *)
        echo "[FAIL] Unexpected execution error (exit code: $status)."
        exit 1
        ;;
esac

echo "======================================"

exit 0
