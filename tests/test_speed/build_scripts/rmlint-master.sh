#!/bin/sh

git clone https://github.com/sahib/rmlint
cd rmlint
git checkout master
scons -j4 DEBUG=0

# Make sure new and old binary does
# not get confused.
mv rmlint rmlint-master
