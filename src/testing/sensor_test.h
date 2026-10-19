#ifndef SENSOR_TEST_H
#define SENSOR_TEST_H

#include <Arduino.h>

void sensor_test_init();
void sensor_test_run_suite();
int sensor_test_get_authenticity_score();
int sensor_test_get_divergence_score();
int sensor_test_get_dht_score();
int sensor_test_get_i2c_score();
int sensor_test_get_overall_score();

#endif
