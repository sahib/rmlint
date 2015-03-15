#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import sys

# Internal:
from app.application import ShredderApplication

# Gtk will take over now.
app = ShredderApplication()
sys.exit(app.run(sys.argv))
