#!/bin/sh

git clone https://github.com/SeeSpotRun/rmlint
cd rmlint
git checkout scheduler-clean
scons -j4 DEBUG=0

# Make sure new and old binary does
# not get confused.
mv rmlint rmlint-spot
