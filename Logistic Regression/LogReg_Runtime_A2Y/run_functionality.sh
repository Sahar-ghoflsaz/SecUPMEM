#this is just a simple test for functionality test

echo " *********** Logistic Regression training (A2Y) **********"

NR_DPUS=1280 NR_TASKLETS=16 make -B
echo " *********** Built**********"
echo " "
echo " *********** Reproducing results for 10240 samples and 16 featureds **********"
./bin/host_code -m 10240 -n 16 -l 0.001 -i 100