#!/bin/sh

cp "$1"/*memory*        ../../docs/_static/benchmarks/memory.svg
cp "$1"/*found_items*   ../../docs/_static/benchmarks/found_items.svg
cp "$1"/*cpu_usage*     ../../docs/_static/benchmarks/cpu_usage.svg
cp "$1"/*timing*        ../../docs/_static/benchmarks/timing.svg
