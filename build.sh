#!/bin/sh
set -e
( cd host && make -B && make install )
( cd plugin && make -B && make install )
