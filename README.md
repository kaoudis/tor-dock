# tor-dock
Helpful stuff for latency testing over a *private* (not connected to the real one) Tor network comprised of Docker containers.
Note that the configs in this directory have a setting called TestingTorNetwork: hopefully, this means we stay in our own LAN.

torrc_auth: torrc config for the directory server
torrc_exit_relay: torrc config for an or relay that is also an exit
torrc_or: torrc config for a plain old or

create-container.sh: run outside the container (I'm just using for reference and not actually running it)  
setup-container.sh: run inside the container (less typing!) 

*None* of this is supposed to, at any time, connect to the actual big grand Tor network. That said, use at your own risk.
