#!/bin/sh

set -e

echo "Configuring MiniKV development environment..."

git config --local core.hooksPath .githooks

chmod +x .githooks/*

echo "Git Hooks configured successfully!"
