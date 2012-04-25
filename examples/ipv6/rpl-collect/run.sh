#!/bin/bash

wmctrl -s 3
JAVA_HOME=/usr/lib/jvm/java-6-openjdk-amd64/ make TARGET=cooja attack_sinkhole.csc &
read
wmctrl -s 4
JAVA_HOME=/usr/lib/jvm/java-6-openjdk-amd64/ make TARGET=cooja attack_wormhole.csc  &
read
wmctrl -s 5
JAVA_HOME=/usr/lib/jvm/java-6-openjdk-amd64/ make TARGET=cooja attack_hello.csc     &
read
wmctrl -s 6
JAVA_HOME=/usr/lib/jvm/java-6-openjdk-amd64/ make TARGET=cooja attack_clone.csc     &
