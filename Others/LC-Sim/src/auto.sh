SW_GROUP_MAP_FILE="./result/metis_result/partition_6509/metis_input_6509.txt.part.5"
HOST_SW_MAP_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts.host_sw_map.txt"
TRACE_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts.agg.txt"


SW_GROUP_MAP_FILE="./result/metis_result/partition_6509hosts_scale//metis_input_6509_scale.txt.part.101"
HOST_SW_MAP_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts_scale.host_sw_map.txt"
TRACE_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts_scale.agg.txt"

SW_GROUP_MAP_FILE="./result/metis_result/partition_6509hosts_scale2/metis_input_6509hosts_scale2.txt.part.102"
HOST_SW_MAP_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts_scale2.host_sw_map.txt"
TRACE_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts_scale2.agg.txt"

SW_GROUP_MAP_FILE="./result/metis_result/partition_6509hosts_scale3/metis_input_6509hosts_scale3.txt.part.105"
HOST_SW_MAP_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts_scale3.host_sw_map.txt"
TRACE_FILE="./result/traffic_record_1day/flow_anon_2008-01-01_6509hosts_scale3.agg.txt"
./sim.py -v ${HOST_SW_MAP_FILE} -s ${SW_GROUP_MAP_FILE} -t ${TRACE_FILE}
