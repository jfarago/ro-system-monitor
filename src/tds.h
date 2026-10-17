// Product-water TDS: 40 ms sampling, 30-sample median, DFRobot curve.
#pragma once
#include <stdint.h>

namespace tds {

void begin();
void update(uint32_t nowMs);  // call every loop
uint32_t medianMv();
float rawPpm(float waterTempC);                // before kTDS
float ppm(float kTds, float waterTempC);       // calibrated
float rejectionPct(float productPpm);          // vs FEED_TDS_PPM, 0..100

}  // namespace tds
