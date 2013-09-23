#!/bin/sh
#This script will occupy the link stat information from remote ddcm
# by calling pull_agent.py

DCM_LIST="dcm_ip.list"
DDCM_LIST="ddcm_ip.list"

#capture each dcm's cpu information at background
if [ ! -f ${DCM_LIST} ]; then
    echo "[ERROR] There is no "${DCM_LIST};
fi
cat ${DCM_LIST} | while read dcm
do
    python pull_agent.py $dcm /tmp/cpu.dat 2>&1 >/dev/null &
done

#capture each ddcm's stat information at background
if [ ! -f ${DDCM_LIST} ]; then
    echo "[ERROR] There is no "${DDCM_LIST};
fi
cat ${DDCM_LIST} | while read ddcm
do
    python pull_agent.py $ddcm /tmp/lc_stat.dat 2>&1 >/dev/null &
done

#collect all stat information and write together for grouping usage
while true
do
    rm -f /tmp/cpus.dat
    rm -f /tmp/lc_stats.dat
    find tmp -type f -name '*_cpu.dat' -exec cat {} \; >/tmp/cpus.dat
    find tmp -type f -name '*_lc_stat.dat' -exec cat {} \; >>/tmp/lc_stats.dat
    # do grouping
    sleep 5
done
