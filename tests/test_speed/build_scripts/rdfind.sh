#!/bin/sh

wget http://rdfind.pauldreik.se/rdfind-1.3.4.tar.gz
tar xvf rdfind-1.3.4.tar.gz
cd rdfind-1.3.4
./configure && make
