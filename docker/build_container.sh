#!/bin/sh

DIR=$(dirname "$(readlink -f "$0")")

docker build "$DIR" --tag bpi-cross-compile:1
