#!/bin/bash
# Builds tools/tlsprobe/tlsprobe.3dsx in the devkitARM container (same image as build/docker.sh).
set -e
export PATH=/opt/homebrew/bin:$PATH
HERE="$(cd "$(dirname "$0")" && pwd)"
exec docker run --rm -v /Users/arnoud/coding_local:/Users/arnoud/coding_local -w "$HERE" devkitpro/devkitarm:latest make
