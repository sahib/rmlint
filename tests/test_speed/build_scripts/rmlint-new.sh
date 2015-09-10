#!/bin/sh

git clone https://github.com/sahib/rmlint
cd rmlint
git checkout develop
scons -j4 DEBUG=0
