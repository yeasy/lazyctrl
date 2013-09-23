NUM=20 #num of hosts on every edge

./boot.sh
./configure --with-linux=/lib/modules/`uname -r`/build

sudo kill `cd /usr/local/var/run/openvswitch && cat ovsdb-server.pid ovs-vswitchd.pid`;
sudo rmmod openvswitch;
sudo modprobe -r openvswitch;
sleep 1;
make && sudo make install && sudo insmod datapath/linux/openvswitch.ko

mkdir -p /usr/local/etc/openvswitch
ovsdb-tool create /usr/local/etc/openvswitch/conf.db vswitchd/vswitch.ovsschema
sudo ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock --remote=db:Open_vSwitch,manager_options --private-key=db:SSL,private_key --certificate=db:SSL,certificate --bootstrap-ca-cert=db:SSL,ca_cert --pidfile --detach; 

sleep 1;

sudo ovs-vsctl --no-wait init; sleep 1; sudo ovs-vswitchd --pidfile --detach; sleep 1;

sudo route del default gw 192.168.56.1; sudo route del default gw 192.168.57.1;
sudo route add -net 239.0.0.0/24 eth1; 
sudo ifconfig eth2 0;
sudo ifconfig br0 192.168.57.10 up;

for ((i=1; i<=${NUM}; i++)); do
        sudo ip addr add 10.0.0.`expr $i + 0`/24 brd 10.0.0.255 dev br0; #10.0.0.1-10.0.0.20 is on host1
done

sudo sysctl -w net.ipv4.neigh.default.gc_stale_time=600;
sudo sysctl -w net.ipv4.neigh.br0.gc_stale_time=600;

#crl
sudo arp -s 192.168.57.1 0a:00:27:00:00:01;
for ((i=1; i<=${NUM}; i++)); do
    sudo arp -s 10.0.0.`expr $i + $NUM` 08:00:27:ab:b6:a5; #10.0.0.21-10.0.0.40 is on host2
    sudo arp -s 10.0.0.`expr $i + 2 \* $NUM` 08:00:27:b0:d5:78; #10.0.0.41-10.0.0.60 is on host3
done

ping -c 1 192.168.57.1 >/dev/null&

#thu
#sudo arp -s 10.0.0.2 08:00:27:ab:b6:a5;
#sudo arp -s 192.168.57.1 08:00:27:00:bc:89;

exit;
sleep 2;

for ((i=1; i<=${NUM}; i++)); do
    ping -c 1 10.0.0.`expr $i + $NUM` >>ping_history.txt;
    sleep 2;
done

for ((i=1; i<=${NUM}; i++)); do
    ping -c 1 10.0.0.`expr $i + 2 \* $NUM` >>ping_history.txt;
    sleep 2;
done
