#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import sys

# Internal:
from app.application import MainApplication

# Gtk will take over now.
app = MainApplication()
sys.exit(app.run(sys.argv))
