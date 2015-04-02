#!/bin/sh
autoreconf -i
debuild -i -us -uc -b
