#!/bin/bash
sudo docker create -i -t --cap-add=ALL --name=one --link=dirserver:dirserver ubuntu:latest /bin/bash
sudo docker create -i -t --cap-add=ALL --name=two --link=dirserver:dirserver --link=one:one ubuntu:latest /bin/bash
sudo docker create -i -t --cap-add=ALL --name=three --link=dirserver:dirserver --link=one:one --link=two:two ubuntu:latest /bin/bash
sudo docker create -i -t --cap-add=ALL --name=four --link=dirserver:dirserver --link=one:one --link=two:two --link=three:three ubuntu:latest /bin/bash
sudo docker create -i -t --cap-add=ALL --name=five --link=dirserver:dirserver --link=one:one --link=two:two --link=three:three --link=four:four ubuntu:latest /bin/bash
sudo docker create -i -t --cap-add=ALL --name=six --link=dirserver:dirserver --link=one:one --link=two:two --link=three:three --link=four:four --link=five:five ubuntu:latest /bin/bash
sudo docker create -i -t --cap-add=ALL --name=seven --link=dirserver:dirserver --link=one:one --link=two:two --link=three:three --link=four:four --link=five:five --link=six:six ubuntu:latest /bin/bash

# now we will need to go in and edit /etc/hosts so everyone knows about everyone else
# since we need a complete topology.
