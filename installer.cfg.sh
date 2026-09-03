#!/bin/bash

copy "services/network-observation/apps/network-observationd/network-observationd" "/usr/sbin/"
copy "services/network-observation/apps/net-observe/net-observe" "/usr/bin/"

make_dir "/etc/network-observation/"
copy "services/network-observation/packaging/config/network-observationd.conf" "/etc/network-observation/"
copy "services/network-observation/packaging/monit/network-observationd.cfg" "/etc/monit.d/"

make_dir "/usr/libexec/network-observation/"
copy "services/network-observation/packaging/scripts/start-network-observationd" "/usr/libexec/network-observation/"
copy "services/network-observation/packaging/scripts/stop-network-observationd" "/usr/libexec/network-observation/"
copy "services/network-observation/packaging/scripts/check-network-observationd" "/usr/libexec/network-observation/"

copy "services/network-observation/packaging/dbus/org.rsc.NetworkObservation.conf" "/etc/dbus-1/system.d/"

###

copy "services/rmc-inventory/apps/inventory-agentd/inventory-agentd" "/usr/sbin/"

make_dir "/etc/inventory-agent/"
copy "services/rmc-inventory/packaging/config/inventory-agentd.conf" "/etc/inventory-agent/"
copy "services/rmc-inventory/packaging/monit/inventory-agentd.cfg" "/etc/monit.d/"

make_dir "/usr/libexec/inventory-agent/"
copy "services/rmc-inventory/packaging/scripts/start-inventory-agentd" "/usr/libexec/inventory-agent/"
copy "services/rmc-inventory/packaging/scripts/stop-inventory-agentd" "/usr/libexec/inventory-agent/"
copy "services/rmc-inventory/packaging/scripts/check-inventory-agentd" "/usr/libexec/inventory-agent/"

copy "services/rmc-inventory/packaging/dbus/org.rsc.Inventory.conf" "/etc/dbus-1/system.d/"