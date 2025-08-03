/*
 * Improved Semi-Markov Model with State Memory
 *
 * This version remembers the previous action and behaves differently based on
 * context:
 * - After LIKE: Usually short watching + high scroll probability
 * - After SCROLL: Normal watching behavior
 * - After DUBIOUS_SCROLL: Longer watching (giving video another chance)
 *
 * Replace parameters below with output from smm_fit.py
 */

#include <math.h>

// =============================================================================
// IMPROVED MODEL PARAMETERS - REPLACE WITH OUTPUT FROM smm_fit.py
// =============================================================================

// State definitions
#define STATE_AFTER_SCROLL 0
#define STATE_AFTER_LIKE 1
#define STATE_AFTER_DUBIOUS 2

// Action definitions
#define ACTION_SCROLL 0
#define ACTION_LIKE 1
#define ACTION_DUBIOUS_SCROLL 2

// Example parameters (replace with real fitted values)
// Parameters for WATCHING_AFTER_SCROLL state
const float MEAN_DWELL_AFTER_SCROLL = 3.200000f;
const float DWELL_RATE_AFTER_SCROLL = 0.312500f;
const float PROB_SCROLL_AFTER_SCROLL = 0.600000f;
const float PROB_LIKE_AFTER_SCROLL = 0.250000f;
const float PROB_DUBIOUS_AFTER_SCROLL = 0.150000f;
const float CUM_PROB_SCROLL_AFTER_SCROLL = 0.600000f;
const float CUM_PROB_LIKE_AFTER_SCROLL = 0.850000f;
const float CUM_PROB_DUBIOUS_AFTER_SCROLL = 1.000000f;

// Parameters for WATCHING_AFTER_LIKE state
const float MEAN_DWELL_AFTER_LIKE = 1.500000f; // Shorter - already liked it!
const float DWELL_RATE_AFTER_LIKE = 0.666667f;
const float PROB_SCROLL_AFTER_LIKE = 0.850000f; // High scroll probability
const float PROB_LIKE_AFTER_LIKE = 0.100000f;
const float PROB_DUBIOUS_AFTER_LIKE = 0.050000f;
const float CUM_PROB_SCROLL_AFTER_LIKE = 0.850000f;
const float CUM_PROB_LIKE_AFTER_LIKE = 0.950000f;
const float CUM_PROB_DUBIOUS_AFTER_LIKE = 1.000000f;

// Parameters for WATCHING_AFTER_DUBIOUS state
const float MEAN_DWELL_AFTER_DUBIOUS = 4.800000f; // Longer - second chance
const float DWELL_RATE_AFTER_DUBIOUS = 0.208333f;
const float PROB_SCROLL_AFTER_DUBIOUS = 0.400000f;
const float PROB_LIKE_AFTER_DUBIOUS = 0.200000f;
const float PROB_DUBIOUS_AFTER_DUBIOUS = 0.400000f; // Might dubious again
const float CUM_PROB_SCROLL_AFTER_DUBIOUS = 0.400000f;
const float CUM_PROB_LIKE_AFTER_DUBIOUS = 0.600000f;
const float CUM_PROB_DUBIOUS_AFTER_DUBIOUS = 1.000000f;

// Lookup arrays for efficient state-dependent parameter access
const float MEAN_DWELL_BY_STATE[3] = {
    MEAN_DWELL_AFTER_SCROLL, MEAN_DWELL_AFTER_LIKE, MEAN_DWELL_AFTER_DUBIOUS};

const float DWELL_RATE_BY_STATE[3] = {
    DWELL_RATE_AFTER_SCROLL, DWELL_RATE_AFTER_LIKE, DWELL_RATE_AFTER_DUBIOUS};

const float CUM_PROB_SCROLL_BY_STATE[3] = {CUM_PROB_SCROLL_AFTER_SCROLL,
                                           CUM_PROB_SCROLL_AFTER_LIKE,
                                           CUM_PROB_SCROLL_AFTER_DUBIOUS};

const float CUM_PROB_LIKE_BY_STATE[3] = {CUM_PROB_LIKE_AFTER_SCROLL,
                                         CUM_PROB_LIKE_AFTER_LIKE,
                                         CUM_PROB_LIKE_AFTER_DUBIOUS};

// =============================================================================
// SIMULATION CONFIGURATION
// =============================================================================

const unsigned long SERIAL_BAUD_RATE = 9600;
const bool VERBOSE_OUTPUT = true;
const float MAX_DWELL_TIME = 30.0f;
const unsigned long SIMULATION_SEED = 42;

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

unsigned long simulation_start_time;
unsigned long event_counter = 0;
int current_state = STATE_AFTER_SCROLL; // Start in some initial state
float total_dwell_time = 0.0f;

// Statistics tracking
unsigned long state_transitions[3] = {0, 0, 0};
float total_dwell_by_state[3] = {0.0f, 0.0f, 0.0f};

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

float random_uniform() { return random(0, 32768) / 32767.0f; }

float sample_exponential(float rate) {
  float u = random_uniform();
  if (u <= 0.0001f)
    u = 0.0001f;

  float sample = -log(u) / rate;
  if (sample > MAX_DWELL_TIME) {
    sample = MAX_DWELL_TIME;
  }

  return sample;
}

int select_next_action(int state) {
  float r = random_uniform();

  if (r < CUM_PROB_SCROLL_BY_STATE[state]) {
    return ACTION_SCROLL;
  } else if (r < CUM_PROB_LIKE_BY_STATE[state]) {
    return ACTION_LIKE;
  } else {
    return ACTION_DUBIOUS_SCROLL;
  }
}

void execute_action(int action_code) {
  event_counter++;
  unsigned long current_time = millis();
  float elapsed_seconds = (current_time - simulation_start_time) / 1000.0f;

  // Update statistics
  state_transitions[current_state]++;

  // Print action with context
  if (VERBOSE_OUTPUT) {
    Serial.print("[");
    Serial.print(elapsed_seconds, 3);
    Serial.print("s] Event #");
    Serial.print(event_counter);
    Serial.print(" (from state ");
    print_state_name(current_state);
    Serial.print("): ");
  }

  switch (action_code) {
  case ACTION_SCROLL:
    Serial.println(VERBOSE_OUTPUT ? "SCROLL" : "SCROLL");
    current_state = STATE_AFTER_SCROLL;
    break;

  case ACTION_LIKE:
    Serial.println(VERBOSE_OUTPUT ? "LIKE" : "LIKE");
    current_state = STATE_AFTER_LIKE;
    break;

  case ACTION_DUBIOUS_SCROLL:
    Serial.println(VERBOSE_OUTPUT ? "DUBIOUS_SCROLL" : "DUBIOUS_SCROLL");
    current_state = STATE_AFTER_DUBIOUS;
    break;

  default:
    Serial.println("ERROR: Unknown action");
    break;
  }
}

void print_state_name(int state) {
  switch (state) {
  case STATE_AFTER_SCROLL:
    Serial.print("after_scroll");
    break;
  case STATE_AFTER_LIKE:
    Serial.print("after_like");
    break;
  case STATE_AFTER_DUBIOUS:
    Serial.print("after_dubious");
    break;
  default:
    Serial.print("unknown");
    break;
  }
}

void print_enhanced_statistics() {
  if (VERBOSE_OUTPUT && event_counter > 0) {
    Serial.println("\n--- Enhanced Statistics ---");

    // State usage
    Serial.println("State transitions:");
    Serial.print("  After SCROLL: ");
    Serial.println(state_transitions[STATE_AFTER_SCROLL]);
    Serial.print("  After LIKE: ");
    Serial.println(state_transitions[STATE_AFTER_LIKE]);
    Serial.print("  After DUBIOUS: ");
    Serial.println(state_transitions[STATE_AFTER_DUBIOUS]);

    // Average dwell times by state
    Serial.println("Average dwell times:");
    for (int i = 0; i < 3; i++) {
      if (state_transitions[i] > 0) {
        float avg_dwell = total_dwell_by_state[i] / state_transitions[i];
        Serial.print("  State ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(avg_dwell, 3);
        Serial.print("s (target: ");
        Serial.print(MEAN_DWELL_BY_STATE[i], 3);
        Serial.println("s)");
      }
    }

    Serial.println("Expected behavior:");
    Serial.println("  - Short dwell after LIKE (quick scroll)");
    Serial.println("  - Long dwell after DUBIOUS (second chance)");
    Serial.println("  - Normal dwell after SCROLL");
  }
}

// =============================================================================
// ARDUINO SETUP AND MAIN LOOP
// =============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial) {
    ;
  }

  randomSeed(SIMULATION_SEED);
  simulation_start_time = millis();

  if (VERBOSE_OUTPUT) {
    Serial.println("=== Improved Semi-Markov Model Simulation ===");
    Serial.println("Now with STATE MEMORY!");
    Serial.println();

    Serial.println("Expected behaviors:");
    Serial.print("After LIKE: quick scroll (");
    Serial.print(MEAN_DWELL_AFTER_LIKE, 1);
    Serial.print("s avg, ");
    Serial.print(PROB_SCROLL_AFTER_LIKE * 100, 0);
    Serial.println("% scroll)");

    Serial.print("After SCROLL: normal (");
    Serial.print(MEAN_DWELL_AFTER_SCROLL, 1);
    Serial.print("s avg, ");
    Serial.print(PROB_SCROLL_AFTER_SCROLL * 100, 0);
    Serial.println("% scroll)");

    Serial.print("After DUBIOUS: longer watch (");
    Serial.print(MEAN_DWELL_AFTER_DUBIOUS, 1);
    Serial.print("s avg, ");
    Serial.print(PROB_SCROLL_AFTER_DUBIOUS * 100, 0);
    Serial.println("% scroll)");

    Serial.println("\nSimulation starting...\n");
  }

  delay(1000);
}

void loop() {
  // === STATE-DEPENDENT WATCHING ===
  // Get parameters for current state
  float dwell_rate = DWELL_RATE_BY_STATE[current_state];
  float mean_dwell = MEAN_DWELL_BY_STATE[current_state];

  // Sample dwell time based on current state
  float dwell_time = sample_exponential(dwell_rate);
  total_dwell_time += dwell_time;
  total_dwell_by_state[current_state] += dwell_time;

  if (VERBOSE_OUTPUT) {
    Serial.print("Watching (");
    print_state_name(current_state);
    Serial.print(") for ");
    Serial.print(dwell_time, 3);
    Serial.print("s [expected: ");
    Serial.print(mean_dwell, 1);
    Serial.println("s]...");
  }

  // Wait for the dwell period
  unsigned long dwell_ms = (unsigned long)(dwell_time * 1000.0f);
  delay(dwell_ms);

  // === STATE-DEPENDENT ACTION SELECTION ===
  int action = select_next_action(current_state);
  execute_action(action);

  // Print enhanced statistics periodically
  if (VERBOSE_OUTPUT && event_counter % 15 == 0) {
    print_enhanced_statistics();
    Serial.println();
  }

  delay(100);
}
