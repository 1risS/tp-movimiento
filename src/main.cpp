#include <Arduino.h>
#include <Servo.h>
#include "smm_parameters.h"
#include <math.h>

Servo myservo_y;
Servo myservo_z;

// analog pins used to connect the potentiometers
int potpin_y = A0;
int potpin_z = A1;
// variable to read the value from the analog pin
int val;

// smoothing and change detection variables
int prev_angle_y = -1;
int prev_angle_z = -1;
const int num_readings = 5;
int readings_y[num_readings];
int readings_z[num_readings];
int read_index = 0;
int total_y = 0;
int total_z = 0;

// Movement function variables
bool scroll_active = false;
bool like_active = false;
bool dubious_active = false;
bool knobs_disabled = false; // Flag to permanently disable knob control
int scroll_step = 0;
int like_step = 0;
int dubious_step = 0;
unsigned long last_step_time = 0;
// const unsigned long scroll_step_delays[] = {100, 150, 200, 200}; // Individual delays for each scroll step
const unsigned long scroll_step_delays[] = {200, 300, 300, 0, 200}; // Individual delays for each scroll step
const unsigned long like_step_delay = 100;                          // ms between steps for Like
const unsigned long dubious_step_delay = 200;                       // ms between steps for Dubious
const unsigned long dubious_wait_min = 500;                         // minimum wait time for dubious
const unsigned long dubious_wait_max = 2000;                        // maximum wait time for dubious
unsigned long dubious_wait_time = 0;                                // current wait time for dubious steps

// Smooth servo movement variables
int current_y = 180, current_z = 0; // Current servo positions
int target_y = 180, target_z = 0;   // Target servo positions
int start_y = 180, start_z = 0;     // Starting positions for current movement
unsigned long step_start_time = 0;  // When current step started
bool step_in_progress = false;      // Whether we're currently moving servos

// =============================================================================
// SEMI-MARKOV MODEL VARIABLES
// =============================================================================

// State definitions (matching smm_parameters.h)
#define STATE_AFTER_SCROLL 0
#define STATE_AFTER_LIKE 1
#define STATE_AFTER_DUBIOUS 2

// Action definitions (matching smm_parameters.h)
#define ACTION_SCROLL 0
#define ACTION_LIKE 1
#define ACTION_DUBIOUS_SCROLL 2

// Simulation configuration
const float MAX_DWELL_TIME = 30.0f;
const unsigned long SIMULATION_SEED = 42;

// Semi-Markov model state
unsigned long simulation_start_time;
unsigned long event_counter = 0;
int current_state = STATE_AFTER_SCROLL; // Start in some initial state
float total_dwell_time = 0.0f;
bool use_like2_variant = false; // Which LIKE variant to use
bool smm_mode_active = false;   // Whether SMM mode is active
bool smm_waiting = false;
unsigned long smm_wait_start = 0;
unsigned long smm_wait_duration = 0;

// Statistics tracking
unsigned long state_transitions[3] = {0, 0, 0};
float total_dwell_by_state[3] = {0.0f, 0.0f, 0.0f};

void smoothMoveServos(int new_target_y, int new_target_z, unsigned long duration_ms)
{
  start_y = current_y; // Remember where we're starting from
  start_z = current_z;
  target_y = new_target_y;
  target_z = new_target_z;
  step_start_time = millis();
  step_in_progress = true;
}

void updateServoPositions()
{
  if (!step_in_progress)
    return;

  unsigned long current_time = millis();
  unsigned long elapsed = current_time - step_start_time;

  // SAFETY CHECK: Prevent array out of bounds access
  if (scroll_step >= 5)
  {
    Serial.print("ERROR: scroll_step out of bounds: ");
    Serial.println(scroll_step);
    step_in_progress = false;
    return;
  }

  unsigned long duration = scroll_step_delays[scroll_step];

  if (elapsed >= duration)
  {
    // Movement complete
    current_y = target_y;
    current_z = target_z;
    myservo_y.write(current_y);
    myservo_z.write(current_z);
    step_in_progress = false;
    return;
  }

  // Calculate progress (0.0 to 1.0)
  float progress = (float)elapsed / duration;

  // Linear interpolation
  current_y = start_y + (int)((target_y - start_y) * progress);
  current_z = start_z + (int)((target_z - start_z) * progress);

  myservo_y.write(current_y);
  myservo_z.write(current_z);
}

// =============================================================================
// SEMI-MARKOV MODEL FUNCTIONS
// =============================================================================

float random_uniform() { return random(0, 32768) / 32767.0f; }

// Memory debugging function for Arduino
// int getFreeMemory()
// {
//   extern int __heap_start, *__brkval;
//   int v;
//   return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
// }

float sample_exponential(float rate)
{
  // Add memory check
  // Serial.print("DEBUG: Free memory: ");
  // Serial.println(getFreeMemory());

  float u = random_uniform();
  if (u <= 0.0001f)
    u = 0.0001f;

  // Use a simpler approximation to avoid potential log() issues
  // Replace logf() with a safer approximation
  float sample;
  if (u > 0.9999f)
  {
    sample = 0.1f; // Very small value for very large u
  }
  else
  {
    // Simple approximation: -ln(u) ≈ (1-u) for u close to 1
    // For exponential distribution, use a simpler method
    sample = (1.0f - u) / rate;
  }

  if (sample > MAX_DWELL_TIME)
  {
    sample = MAX_DWELL_TIME;
  }

  // Ensure we return a reasonable value
  if (sample < 0.1f)
    sample = 0.1f;

  // Serial.print("DEBUG: Sampled dwell time: ");
  // Serial.println(sample);
  return sample;
}

int select_next_action(int state)
{
  float r = random_uniform();

  if (r < CUM_PROB_SCROLL_BY_STATE[state])
  {
    return ACTION_SCROLL;
  }
  else if (r < CUM_PROB_LIKE_BY_STATE[state])
  {
    return ACTION_LIKE;
  }
  else
  {
    return ACTION_DUBIOUS_SCROLL;
  }
}

void print_state_name(int state)
{
  switch (state)
  {
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

void execute_smm_action(int action_code)
{
  event_counter++;
  unsigned long current_time = millis();
  float elapsed_seconds = (current_time - simulation_start_time) / 1000.0f;

  // Update statistics
  state_transitions[current_state]++;

  // Print action with context
  Serial.print("[");
  Serial.print(elapsed_seconds, 3);
  Serial.print("s] SMM Event #");
  Serial.print(event_counter);
  Serial.print(" (from state ");
  print_state_name(current_state);
  Serial.print("): ");

  switch (action_code)
  {
  case ACTION_SCROLL:
    Serial.println("SMM SCROLL");
    // Start scroll movement
    scroll_active = true;
    scroll_step = 0;
    knobs_disabled = true;
    current_state = STATE_AFTER_SCROLL;
    break;

  case ACTION_LIKE:
  {
    // Randomly choose between executeLike and executeLike2
    use_like2_variant = random(0, 2) == 1;
    if (use_like2_variant)
    {
      Serial.println("SMM LIKE (variant 2)");
    }
    else
    {
      Serial.println("SMM LIKE (variant 1)");
    }
    // Start like movement
    like_active = true;
    like_step = 0;
    last_step_time = millis();
    knobs_disabled = true;
    current_state = STATE_AFTER_LIKE;
  }
  break;

  case ACTION_DUBIOUS_SCROLL:
    Serial.println("SMM DUBIOUS_SCROLL");
    // Start dubious movement
    dubious_active = true;
    dubious_step = 0;
    dubious_wait_time = 0;
    last_step_time = millis();
    knobs_disabled = true;
    current_state = STATE_AFTER_DUBIOUS;
    break;

  default:
    Serial.println("ERROR: Unknown SMM action");
    break;
  }
}

void executeScroll()
{
  updateServoPositions(); // Update servo positions for smooth movement

  // Only start new step if previous movement is complete
  if (!step_in_progress)
  {
    switch (scroll_step)
    {
    case 0:
      smoothMoveServos(142, 142, scroll_step_delays[scroll_step]);
      Serial.println("Scroll Step 1");
      break;
    case 1:
      smoothMoveServos(110, 142, scroll_step_delays[scroll_step]);
      Serial.println("Scroll Step 2");
      break;
    case 2:
      smoothMoveServos(90, 100, scroll_step_delays[scroll_step]);
      Serial.println("Scroll Step 3");
      break;
    case 3:
      smoothMoveServos(147, 100, scroll_step_delays[scroll_step]);
      Serial.println("Scroll Step 4");
      break;
    case 4:
      Serial.println("Scroll movement complete!");
      // Serial.println("DEBUG: Before setting flags - scroll_active will be false");
      scroll_active = false;
      // Serial.println("DEBUG: After setting scroll_active = false");
      // Serial.print("DEBUG: Free memory after scroll completion: ");
      // Serial.println(getFreeMemory());
      scroll_step = 0;
      return;
    }

    scroll_step++;
  }
}

void executeLike()
{
  unsigned long current_time = millis();

  if (current_time - last_step_time >= like_step_delay)
  {
    switch (like_step)
    {
    case 0:
      // Step 1: Initial
      myservo_y.write(140);
      myservo_z.write(120);
      Serial.println("Like Step 1: Y=140, Z=116");
      break;
    case 1:
      // Step 2: First press
      myservo_y.write(120);
      delay(100);
      Serial.println("Like Step 2: Y=130, Z=128");
      break;
    case 2:
      // Step 3: Lift thumb up
      myservo_y.write(140);
      Serial.println("Like Step 3: Y=140, Z=128");
      break;
    case 3:
      // Step 4: Second press
      myservo_y.write(120);
      delay(100);
      Serial.println("Like Step 4: Y=130, Z=128");
      break;
    case 4:
      // Step 5: Lift thumb up again
      myservo_y.write(140);
      Serial.println("Like Step 5: Y=140, Z=128");
      Serial.println("Like movement complete!");
      Serial.println("Knob control permanently disabled.");
      like_active = false;
      knobs_disabled = true; // Permanently disable knob control
      like_step = 0;
      return;
    }

    like_step++;
    last_step_time = current_time;
  }
}

void executeLike2()
{
  unsigned long current_time = millis();

  if (current_time - last_step_time >= like_step_delay)
  {
    switch (like_step)
    {
    case 0:
      myservo_y.write(152);
      myservo_z.write(110);
      Serial.println("Like Step 1: Y=152, Z=100");
      break;
    case 1:
      myservo_y.write(120);
      myservo_z.write(110);
      delay(250);
      Serial.println("Like Step 2: Y=120, Z=100");
      break;
    case 2:
      myservo_y.write(152);
      myservo_z.write(110);
      Serial.println("Like Step 3: Y=152, Z=100");
      like_active = false;
      knobs_disabled = true; // Permanently disable knob control
      like_step = 0;
      return;
    }

    like_step++;
    last_step_time = current_time;
  }
}

void executeDubious()
{
  unsigned long current_time = millis();

  if (current_time - last_step_time >= dubious_step_delay)
  {
    switch (dubious_step)
    {
    case 0:
      // Step 1: Y 150, Z 146
      myservo_y.write(145);
      myservo_z.write(122);
      Serial.println("Dubious Step 1");
      break;
    case 1:
      // Step 2: Y 134, Z 140
      myservo_y.write(125);
      myservo_z.write(122);
      Serial.println("Dubious Step 2");
      break;
    case 2:
      // Step 3: Y 130, Z 125
      myservo_y.write(122);
      myservo_z.write(106);
      Serial.println("Dubious Step 3");
      break;
    case 3:
      // Step 4: wait some ms (random between 500-2000ms)
      if (dubious_wait_time == 0)
      {
        dubious_wait_time = random(dubious_wait_min, dubious_wait_max + 1);
        Serial.print("Dubious Step 4: Waiting ");
        Serial.print(dubious_wait_time);
        Serial.println("ms...");
        last_step_time = current_time;
        return;
      }
      if (current_time - last_step_time >= dubious_wait_time)
      {
        dubious_wait_time = 0; // Reset wait time
        break;
      }
      else
      {
        return; // Still waiting
      }
    case 4:
      // Step 5: Y 133, Z 144
      myservo_y.write(122);
      myservo_z.write(134);
      Serial.println("Dubious Step 5");
      break;
    case 5:
      // Step 6: wait some ms (random between 500-2000ms)
      if (dubious_wait_time == 0)
      {
        dubious_wait_time = random(dubious_wait_min, dubious_wait_max + 1);
        Serial.print("Dubious Step 6: Waiting ");
        Serial.print(dubious_wait_time);
        Serial.println("ms...");
        last_step_time = current_time;
        return;
      }
      if (current_time - last_step_time >= dubious_wait_time)
      {
        dubious_wait_time = 0; // Reset wait time
        break;
      }
      else
      {
        return; // Still waiting
      }
    case 6:
      // Step 6: Y 130, Z 127
      myservo_y.write(122);
      myservo_z.write(106);
      Serial.println("Dubious Step 6");
      break;
    case 7:
      // Step 7: Y 136, Z 127
      myservo_y.write(145);
      myservo_z.write(122);
      Serial.println("Dubious Step 7");
      Serial.println("Dubious movement complete!");
      Serial.println("Knob control permanently disabled.");
      dubious_active = false;
      knobs_disabled = true; // Permanently disable knob control
      dubious_step = 0;
      return;
    }

    dubious_step++;
    last_step_time = current_time;
  }
}

void setup()
{
  Serial.begin(9600);
  // Remove the blocking while loop that can cause hang
  delay(2000); // Just wait 2 seconds for serial to stabilize

  myservo_y.attach(9);
  myservo_z.attach(10);

  // Set servos to initial positions immediately
  myservo_y.write(180);
  myservo_z.write(0);

  // Read initial potentiometer values and initialize smoothing arrays
  int initial_pot_y = analogRead(potpin_y);
  int initial_pot_z = analogRead(potpin_z);

  // Initialize smoothing arrays with actual potentiometer readings
  for (int i = 0; i < num_readings; i++)
  {
    readings_y[i] = initial_pot_y;
    readings_z[i] = initial_pot_z;
  }

  // Initialize totals
  total_y = initial_pot_y * num_readings;
  total_z = initial_pot_z * num_readings;

  // Set previous angles to current mapped values to prevent initial movement
  prev_angle_y = map(initial_pot_y, 0, 1023, 0, 180);
  prev_angle_z = map(initial_pot_z, 0, 1023, 0, 180);

  Serial.print("Initial pot readings - Y: ");
  Serial.print(initial_pot_y);
  Serial.print(" (");
  Serial.print(prev_angle_y);
  Serial.print("°), Z: ");
  Serial.print(initial_pot_z);
  Serial.print(" (");
  Serial.print(prev_angle_z);
  Serial.println("°)");

  // Initialize Semi-Markov Model
  randomSeed(SIMULATION_SEED);
  simulation_start_time = millis();

  // Clear any garbage and send clean startup message
  Serial.println();
  Serial.println("=================================");
  Serial.println("Enhanced Servo Control System");
  Serial.println("=================================");
  Serial.println("Manual Commands:");
  Serial.println("  s - Start Scroll movement");
  Serial.println("  l - Start Like movement");
  Serial.println("  d - Start Dubious movement");
  Serial.println("  m - Toggle Semi-Markov Model");
  Serial.println("  r - Reset and enable knobs");
  Serial.println("=================================");
  Serial.println("System ready!");
  Serial.println("Use potentiometers or send commands");
  Serial.println();

  // Add a test message to verify we reach the end of setup
  Serial.println("Setup complete - entering main loop...");
}

void loop()
{
  // Add debug message to verify loop is running
  static unsigned long last_debug = 0;
  if (millis() - last_debug > 5000) // Every 5 seconds
  {
    // Serial.print("Loop running - system alive, free memory: ");
    // Serial.println(getFreeMemory());
    // Serial.print("Flags - scroll_active: ");
    // Serial.print(scroll_active);
    // Serial.print(", like_active: ");
    // Serial.print(like_active);
    // Serial.print(", dubious_active: ");
    // Serial.print(dubious_active);
    // Serial.print(", knobs_disabled: ");
    // Serial.println(knobs_disabled);
    last_debug = millis();
  }

  // Check for serial input to trigger movements
  if (Serial.available() > 0)
  {
    char input = Serial.read();
    Serial.print("Received command: ");
    Serial.println(input);

    // Manual control commands
    if (input == 's' || input == 'S')
    {
      if (!scroll_active && !like_active && !dubious_active)
      {
        scroll_active = true;
        knobs_disabled = true; // Disable knobs when scroll starts
        scroll_step = 0;
        last_step_time = millis();
        Serial.println("Starting Manual Scroll movement...");
        Serial.println("Knob control disabled.");
      }
    }
    else if (input == 'l' || input == 'L')
    {
      if (!scroll_active && !like_active && !dubious_active)
      {
        like_active = true;
        knobs_disabled = true; // Disable knobs when like starts
        like_step = 0;
        last_step_time = millis();
        Serial.println("Starting Manual Like movement...");
        Serial.println("Knob control disabled.");
      }
    }
    else if (input == 'd' || input == 'D')
    {
      if (!scroll_active && !like_active && !dubious_active)
      {
        dubious_active = true;
        knobs_disabled = true; // Disable knobs when dubious starts
        dubious_step = 0;
        dubious_wait_time = 0; // Reset wait time
        last_step_time = millis();
        Serial.println("Starting Manual Dubious movement...");
        Serial.println("Knob control disabled.");
      }
    }
    // SMM mode toggle
    else if (input == 'm' || input == 'M')
    {
      smm_mode_active = !smm_mode_active;
      if (smm_mode_active)
      {
        simulation_start_time = millis();
        Serial.println("=== Semi-Markov Model Mode ACTIVATED ===");
        Serial.println("SMM will now control servo movements automatically.");
        knobs_disabled = true;
      }
      else
      {
        Serial.println("=== Semi-Markov Model Mode DEACTIVATED ===");
        Serial.println("Manual control restored.");
        knobs_disabled = false;
      }
    }
    // Reset/Enable knobs
    else if (input == 'r' || input == 'R')
    {
      knobs_disabled = false;
      smm_mode_active = false;
      scroll_active = false;
      like_active = false;
      dubious_active = false;
      Serial.println("System reset. Knob control re-enabled.");
    }
  }

  // Execute scroll movement if active
  if (scroll_active)
  {
    executeScroll();
    return; // Skip potentiometer control during scroll
  }

  // Execute like movement if active
  if (like_active)
  {
    // executeLike2();
    executeLike();
    return; // Skip potentiometer control during like
  }

  // Execute dubious movement if active
  if (dubious_active)
  {
    executeDubious();
    return; // Skip potentiometer control during dubious
  }

  // Normal potentiometer control (only when scroll has never been executed)
  if (!knobs_disabled)
  {
    // subtract the last reading:
    total_y = total_y - readings_y[read_index];
    total_z = total_z - readings_z[read_index];

    // read from the sensors:
    readings_y[read_index] = analogRead(potpin_y);
    readings_z[read_index] = analogRead(potpin_z);

    // add the reading to the total:
    total_y = total_y + readings_y[read_index];
    total_z = total_z + readings_z[read_index];

    // advance to the next position in the array:
    read_index = read_index + 1;
    if (read_index >= num_readings)
    {
      read_index = 0;
    }

    // calculate the average and map to servo angle:
    int avg_y = total_y / num_readings;
    int avg_z = total_z / num_readings;
    int angle_y = map(avg_y, 0, 1023, 0, 180);
    int angle_z = map(avg_z, 0, 1023, 0, 180);

    // only update and print if angle changed
    bool changed = false;

    if (angle_y != prev_angle_y)
    {
      myservo_y.write(angle_y);
      prev_angle_y = angle_y;
      changed = true;
    }

    if (angle_z != prev_angle_z)
    {
      myservo_z.write(angle_z);
      prev_angle_z = angle_z;
      changed = true;
    }

    // print both values on same line if either changed
    if (changed)
    {
      Serial.print("Servo Y: ");
      Serial.print(angle_y);
      Serial.print(" | Servo Z: ");
      Serial.println(angle_z);
    }

    delay(15);
  }

  // =============================================================================
  // SEMI-MARKOV MODEL EXECUTION
  // =============================================================================

  // Execute Semi-Markov Model if active and no movements are running
  if (smm_mode_active && !scroll_active && !like_active && !dubious_active && knobs_disabled)
  {
    if (!smm_waiting)
    {
      // === STATE-DEPENDENT WATCHING ===
      // Get parameters for current state
      float dwell_rate = DWELL_RATE_BY_STATE[current_state];
      float mean_dwell = MEAN_DWELL_BY_STATE[current_state];

      // Sample dwell time based on current state
      float dwell_time = sample_exponential(dwell_rate);
      total_dwell_time += dwell_time;
      total_dwell_by_state[current_state] += dwell_time;

      Serial.print("SMM Watching (");
      print_state_name(current_state);
      Serial.print(") for ");
      Serial.print(dwell_time, 3);
      Serial.print("s [expected: ");
      Serial.print(mean_dwell, 1);
      Serial.println("s]...");

      // Start non-blocking wait
      smm_wait_start = millis();
      smm_wait_duration = (unsigned long)(dwell_time * 1000.0f);
      smm_waiting = true;
    }
    else
    {
      // Check if wait period is complete
      if (millis() - smm_wait_start >= smm_wait_duration)
      {
        smm_waiting = false;

        // === STATE-DEPENDENT ACTION SELECTION ===
        int action = select_next_action(current_state);
        execute_smm_action(action);

        // Print statistics periodically
        if (event_counter % 10 == 0)
        {
          Serial.println("\n--- SMM Statistics ---");
          Serial.println("State transitions:");
          Serial.print("  After SCROLL: ");
          Serial.println(state_transitions[STATE_AFTER_SCROLL]);
          Serial.print("  After LIKE: ");
          Serial.println(state_transitions[STATE_AFTER_LIKE]);
          Serial.print("  After DUBIOUS: ");
          Serial.println(state_transitions[STATE_AFTER_DUBIOUS]);
          Serial.println();
        }
      }
    }
  }
}
