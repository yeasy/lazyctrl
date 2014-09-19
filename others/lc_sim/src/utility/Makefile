PREPROCESS=./preprocess.py
CUT=./cut.py
NUM_HOST=6509

RECORD_FILE=../result/traffic_record_1day/flow_anon_2008-01-01.record.txt
AGG_FILE=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts.agg.txt
AFFINITY_FILE=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts.sw_affinity.txt
METIS_INPUT=../result/metis_result/metis_input_${NUM_HOST}.txt

AGG_SCALE_FILE=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts_scale.agg.txt
AFFINITY_SCALE_FILE=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts_scale.sw_affinity.txt
METIS_INPUT_SCALE=../result/metis_result/metis_input_${NUM_HOST}hosts_scale.txt

AGG_SCALE_FILE2=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts_scale2.agg.txt
AFFINITY_SCALE_FILE2=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts_scale2.sw_affinity.txt
METIS_INPUT_SCALE2=../result/metis_result/metis_input_${NUM_HOST}hosts_scale2.txt


AGG_SCALE_FILE3=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts_scale3.agg.txt
AFFINITY_SCALE_FILE3=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_HOST}hosts_scale3.sw_affinity.txt
METIS_INPUT_SCALE3=../result/metis_result/metis_input_${NUM_HOST}hosts_scale3.txt

agg:
	echo "Aggregate original flow records into undirectional flow pairs"
	$(PREPROCESS) -m agg -i $(RECORD_FILE) -o $(AGG_FILE)

scale:
	echo "Scale flow pairs based on the aggregated file."
	$(PREPROCESS) -m scale -i $(AGG_FILE) -o $(AGG_SCALE_FILE)

scale2:
	echo "Scale flow pairs based on the aggregated file."
	$(PREPROCESS) -m scale -i $(AGG_FILE) -o $(AGG_SCALE_FILE2)

scale3:
	echo "Scale flow pairs based on the aggregated file."
	$(PREPROCESS) -m scale -i $(AGG_FILE) -o $(AGG_SCALE_FILE3)

#original trace file
affinity:
	echo "Generate the switch affinity file."
	$(PREPROCESS) -m affinity -i $(AGG_FILE) -o $(AFFINITY_FILE) -n 6509

focus:
	echo "Calculate the focus value based on the agg file."
	$(PREPROCESS) -m focus -i $(AGG_FILE)
cut:
	echo "Generate the metis_input file for cutting."
	$(CUT) -m kcut -i $(AFFINITY_FILE) -o $(METIS_INPUT)

#scaled trace file
scale_affinity:
	echo "Generate the switch affinity_scale file."
	$(PREPROCESS) -m affinity -i $(AGG_SCALE_FILE) -o $(AFFINITY_SCALE_FILE) -n 65090
scale_focus:
	echo "Calculate the focus value based on the agg file."
	$(PREPROCESS) -m focus -i $(AGG_SCALE_FILE)
scale_cut:
	echo "Generate the metis_input_scale file for cutting."
	$(CUT) -m kcut -i $(AFFINITY_SCALE_FILE) -o $(METIS_INPUT_SCALE)

scale_affinity2:
	echo "Generate the switch affinity_scale file."
	$(PREPROCESS) -m affinity -i $(AGG_SCALE_FILE2) -o $(AFFINITY_SCALE_FILE2) -n 65090
scale_focus2:
	echo "Calculate the focus value based on the agg file."
	$(PREPROCESS) -m focus -i $(AGG_SCALE_FILE2)
scale_cut2:
	echo "Generate the metis_input_scale file for cutting."
	$(CUT) -m kcut -i $(AFFINITY_SCALE_FILE2) -o $(METIS_INPUT_SCALE2)

scale_affinity3:
	echo "Generate the switch affinity_scale file."
	$(PREPROCESS) -m affinity -i $(AGG_SCALE_FILE3) -o $(AFFINITY_SCALE_FILE3) -n 65090
scale_focus3:
	echo "Calculate the focus value based on the agg file."
	$(PREPROCESS) -m focus -i $(AGG_SCALE_FILE3)
scale_cut3:
	echo "Generate the metis_input_scale file for cutting."
	$(CUT) -m kcut -i $(AFFINITY_SCALE_FILE3) -o $(METIS_INPUT_SCALE3)

#use random
NUM_RANDOM_HOST=65000
AGG_RANDOM_FILE=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_RANDOM_HOST}hosts_random.agg.txt
AFFINITY_RANDOM_FILE=../result/traffic_record_1day/flow_anon_2008-01-01_${NUM_RANDOM_HOST}hosts_random.sw_affinity.txt
METIS_INPUT_RANDOM=../result/metis_result/metis_input_${NUM_RANDOM_HOST}hosts_random.txt

random:
	echo "Generate random aggregated file."
	$(PREPROCESS) -m random -o $(AGG_RANDOM_FILE) -n ${NUM_RANDOM_HOST}
random_affinity:
	echo "Generate the switch affinity_random file:" $(AFFINITY_RANDOM_FILE)
	$(PREPROCESS) -m affinity -i $(AGG_RANDOM_FILE) -o $(AFFINITY_RANDOM_FILE) -n ${NUM_RANDOM_HOST}
random_cut:
	echo "Generate the metis_input_random file for cutting:" $(METIS_INPUT_RANDOM)
	$(CUT) -m kcut -i $(AFFINITY_RANDOM_FILE) -o $(METIS_INPUT_RANDOM)
