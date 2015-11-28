#!/bin/bash

apt-get update && apt-get upgrade && apt-get install vim && apt-get install tor &&
chown root /var/lib/tor && mkdir /var/lib/tor/keys

# edit torrc before generating router keys

tor --list-fingerprint --orport 9050 \
    --dirserver "x <dirserver ip>:9051 <certificate fingerprint of directory server>" \
    --datadirectory /var/lib/tor/

# add environmental variables and /etc/hosts entries for other containers

# set up a tc/qdisc rule, examples of which can be seen in netem-rule, probably including 
# network delays and/or corruption

# start tor (make sure dirserver is up, then add relays to ad hoc network)


