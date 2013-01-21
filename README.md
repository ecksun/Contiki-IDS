IDS for Contiki
===============

This is a IDS written for Contiki as part of my master thesis. It consists of
several different components, the biggest one is a network mapper, which
constructs a topological map of the network. This map is used to determine if
any sinkhole attacks are present, by looking at the ranks, and if any network
nodes are being filtered.

This work does also contain a very simple firewall, designed to stop malicious
network traffic before they reach the resource constrained network nodes. The
firewall code is in its own branch (firewall) as it requires changes to the
Contiki core files, more specifically core/net/tcpip.c

In the branch "attacks" a number of attacks is constructed designed to
compromise the workings of a RPL network.

This repository is based upon Contiki 2.6 and all changes of importance in the
IDS branch is contained within apps/ids-server, apps/ids-client and
apps/ids-common. The main part of the IDS is in the IDS branch.
