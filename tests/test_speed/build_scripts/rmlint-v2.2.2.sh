#!/bin/sh

git clone https://github.com/sahib/rmlint
cd rmlint
git checkout v2.2.2
scons -j4 DEBUG=0

# Make sure new and old binary does
# not get confused.
mv rmlint rmlint-v2.2.2
