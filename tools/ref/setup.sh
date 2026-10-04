#!/bin/bash
# Builds the helpers game.sh needs (macOS). Needs: brew install cliclick ffmpeg; pip3 install opencv-python numpy
cd "$(dirname "$0")" && mkdir -p bin && swiftc -O winb.swift -o bin/winb && echo "built tools/ref/bin/winb"
