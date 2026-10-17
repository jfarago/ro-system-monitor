#include "flow.h"
#include <Arduino.h>
#include "config.h"

namespace flow {

struct Counter {
  volatile uint32_t pulses = 0;
  volatile uint32_t lastEdgeUs = 0;
  uint32_t lastSnapshot = 0;
};

static Counter cIn, cOut;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR onEdge(Counter &c) {
  uint32_t now = micros();
  if (now - c.lastEdgeUs < FLOW_DEBOUNCE_US) return;
  c.lastEdgeUs = now;
  c.pulses++;
}
static void IRAM_ATTR isrIn() { onEdge(cIn); }
static void IRAM_ATTR isrOut() { onEdge(cOut); }

void begin() {
  // Internal pull-ups are harmless with a divider and needed if open-collector.
  pinMode(PIN_FLOW_IN, INPUT_PULLUP);
  pinMode(PIN_FLOW_OUT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_IN), isrIn, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_OUT), isrOut, FALLING);
}

Pulses takeDelta() {
  portENTER_CRITICAL(&mux);
  uint32_t pIn = cIn.pulses, pOut = cOut.pulses;
  portEXIT_CRITICAL(&mux);
  // Unsigned subtraction handles the 32-bit wrap.
  Pulses d{pIn - cIn.lastSnapshot, pOut - cOut.lastSnapshot};
  cIn.lastSnapshot = pIn;
  cOut.lastSnapshot = pOut;
  return d;
}

}  // namespace flow
