#!/bin/bash
ip a a 192.168.0.19/24 dev wan
ip l set wan up
ip r a default via 192.168.0.11
