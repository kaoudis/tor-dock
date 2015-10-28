#!/bin/bash

yum update && yum install epel-release && yum install vim && yum install tor
chown root /var/lib/tor
tor --list-fingerprint --orport 1 \
    --dirserver "x 127.0.0.1:1 ffffffffffffffffffffffffffffffffffffffff" \
    --datadirectory /var/lib/tor/
