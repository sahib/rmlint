#!/bin/sh

git clone https://github.com/sahib/rmlint
cd rmlint
git checkout v1.0.6
make

# Make sure new and old binary does
# not get confused.
mv rmlint rmlint-old
