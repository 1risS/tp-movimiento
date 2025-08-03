#!/usr/bin/env python3
"""
Semi-Markov Model with State Memory

This version remembers the previous action and models different behaviors
for watching-after-scroll, watching-after-like, and watching-after-dubious.

Expected CSV format:
- timestamp (float): Time in seconds
- event (string): One of 'scroll', 'like', 'dubious_scroll'
"""

import csv
import sys
import logging
import argparse
from collections import Counter


def load_data(filename):
    """Load timestamped events from CSV file."""
    events = []
    try:
        with open(filename, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                timestamp = float(row["timestamp"])
                event = row["event"].strip().lower()
                events.append((timestamp, event))
    except FileNotFoundError:
        logging.error(f"File '{filename}' not found.")
        sys.exit(1)
    except KeyError as e:
        logging.error(f"Required column {e} not found in CSV.")
        sys.exit(1)
    except ValueError as e:
        logging.error(f"Error parsing CSV: {e}")
        sys.exit(1)

    events.sort(key=lambda x: x[0])
    return events


def analyze_state_dependent_behavior(events):
    """Analyze behavior based on previous action state."""
    if len(events) < 2:
        logging.error("Need at least 2 events to analyze transitions.")
        sys.exit(1)

    valid_events = ["scroll", "like", "dubious_scroll"]

    # Store dwell times and next actions for each previous state
    state_data = {
        "scroll": {"dwell_times": [], "next_actions": []},
        "like": {"dwell_times": [], "next_actions": []},
        "dubious_scroll": {"dwell_times": [], "next_actions": []},
    }

    # Analyze transitions between consecutive events
    for i in range(1, len(events)):
        prev_time, prev_action = events[i - 1]
        curr_time, curr_action = events[i]

        if prev_action in valid_events and curr_action in valid_events:
            dwell_time = curr_time - prev_time
            if dwell_time > 0:
                state_data[prev_action]["dwell_times"].append(dwell_time)
                state_data[prev_action]["next_actions"].append(curr_action)

    return state_data


def calculate_state_parameters(state_data):
    """Calculate parameters for each state."""
    parameters = {}

    for state_name, data in state_data.items():
        if not data["dwell_times"]:
            logging.warning(f"No data for state '{state_name}', using defaults")
            parameters[state_name] = {
                "mean_dwell": 2.0,
                "rate": 0.5,
                "transition_probs": {"scroll": 0.6, "like": 0.2, "dubious_scroll": 0.2},
                "cumulative_probs": {"scroll": 0.6, "like": 0.8, "dubious_scroll": 1.0},
            }
            continue

        # Calculate dwell time parameters
        mean_dwell = sum(data["dwell_times"]) / len(data["dwell_times"])
        rate = 1.0 / mean_dwell

        # Calculate transition probabilities
        action_counts = Counter(data["next_actions"])
        total_actions = len(data["next_actions"])

        transition_probs = {}
        for action in ["scroll", "like", "dubious_scroll"]:
            transition_probs[action] = action_counts[action] / total_actions

        # Calculate cumulative probabilities
        cumulative_probs = {
            "scroll": transition_probs["scroll"],
            "like": transition_probs["scroll"] + transition_probs["like"],
            "dubious_scroll": 1.0,
        }

        parameters[state_name] = {
            "mean_dwell": mean_dwell,
            "rate": rate,
            "transition_probs": transition_probs,
            "cumulative_probs": cumulative_probs,
            "sample_size": len(data["dwell_times"]),
        }

    return parameters


def generate_cpp_constants(parameters):
    """Generate C++ header with state-dependent parameters."""

    cpp_content = """// Semi-Markov Model Parameters with State Memory
// Auto-generated from interaction data

#ifndef SMM_PARAMETERS_H
#define SMM_PARAMETERS_H

// State definitions
#define STATE_AFTER_SCROLL 0
#define STATE_AFTER_LIKE 1
#define STATE_AFTER_DUBIOUS 2

// Action definitions
#define ACTION_SCROLL 0
#define ACTION_LIKE 1
#define ACTION_DUBIOUS_SCROLL 2

"""

    # Generate parameters for each state
    state_names = ["scroll", "like", "dubious_scroll"]
    state_defines = ["SCROLL", "LIKE", "DUBIOUS"]

    for i, (state_name, state_define) in enumerate(zip(state_names, state_defines)):
        params = parameters[state_name]
        cpp_content += f"""
// Parameters for WATCHING_AFTER_{state_define} state
const float MEAN_DWELL_AFTER_{state_define} = {params['mean_dwell']:.6f}f;
const float DWELL_RATE_AFTER_{state_define} = {params['rate']:.6f}f;

const float PROB_SCROLL_AFTER_{state_define} = {params['transition_probs']['scroll']:.6f}f;
const float PROB_LIKE_AFTER_{state_define} = {params['transition_probs']['like']:.6f}f;
const float PROB_DUBIOUS_AFTER_{state_define} = {params['transition_probs']['dubious_scroll']:.6f}f;

const float CUM_PROB_SCROLL_AFTER_{state_define} = {params['cumulative_probs']['scroll']:.6f}f;
const float CUM_PROB_LIKE_AFTER_{state_define} = {params['cumulative_probs']['like']:.6f}f;
const float CUM_PROB_DUBIOUS_AFTER_{state_define} = {params['cumulative_probs']['dubious_scroll']:.6f}f;
"""

    # Create lookup arrays for efficient access
    cpp_content += """
// Lookup arrays for efficient state-dependent parameter access
const float MEAN_DWELL_BY_STATE[3] = {
    MEAN_DWELL_AFTER_SCROLL,
    MEAN_DWELL_AFTER_LIKE, 
    MEAN_DWELL_AFTER_DUBIOUS
};

const float DWELL_RATE_BY_STATE[3] = {
    DWELL_RATE_AFTER_SCROLL,
    DWELL_RATE_AFTER_LIKE,
    DWELL_RATE_AFTER_DUBIOUS
};

const float CUM_PROB_SCROLL_BY_STATE[3] = {
    CUM_PROB_SCROLL_AFTER_SCROLL,
    CUM_PROB_SCROLL_AFTER_LIKE,
    CUM_PROB_SCROLL_AFTER_DUBIOUS
};

const float CUM_PROB_LIKE_BY_STATE[3] = {
    CUM_PROB_LIKE_AFTER_SCROLL,
    CUM_PROB_LIKE_AFTER_LIKE,
    CUM_PROB_LIKE_AFTER_DUBIOUS
};

#endif // SMM_PARAMETERS_H
"""

    return cpp_content


def print_statistics(parameters):
    """Print detailed statistics for each state."""
    logging.info("=== Semi-Markov Model Results ===")

    for state_name in ["scroll", "like", "dubious_scroll"]:
        params = parameters[state_name]
        logging.info(f"--- After {state_name.upper()} ---")
        logging.info(f"Sample size: {params.get('sample_size', 0)} transitions")
        logging.info(f"Mean dwell time: {params['mean_dwell']:.3f} seconds")
        logging.info(f"Next action probabilities:")
        logging.info(f"  scroll: {params['transition_probs']['scroll']:.3f}")
        logging.info(f"  like: {params['transition_probs']['like']:.3f}")
        logging.info(
            f"  dubious_scroll: {params['transition_probs']['dubious_scroll']:.3f}"
        )

    # Show behavioral insights
    logging.info("=== Behavioral Insights ===")

    # Compare dwell times
    dwell_after_like = parameters["like"]["mean_dwell"]
    dwell_after_scroll = parameters["scroll"]["mean_dwell"]
    dwell_after_dubious = parameters["dubious_scroll"]["mean_dwell"]

    logging.info("Dwell time comparison:")
    logging.info(f"  After LIKE: {dwell_after_like:.2f}s")
    logging.info(f"  After SCROLL: {dwell_after_scroll:.2f}s")
    logging.info(f"  After DUBIOUS: {dwell_after_dubious:.2f}s")

    if dwell_after_like < dwell_after_scroll:
        logging.info("  → Users watch LESS after liking (quick scroll pattern)")
    if dwell_after_dubious > dwell_after_scroll:
        logging.info("  → Users watch MORE after dubious scroll (second chance)")

    # Compare scroll probabilities
    scroll_after_like = parameters["like"]["transition_probs"]["scroll"]
    scroll_after_scroll = parameters["scroll"]["transition_probs"]["scroll"]

    if scroll_after_like > scroll_after_scroll:
        logging.info(
            f"  → Higher scroll probability after LIKE ({scroll_after_like:.2f} vs {scroll_after_scroll:.2f})"
        )


def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    parser = argparse.ArgumentParser(
        description="Fit a Semi-Markov Model to event data."
    )
    parser.add_argument("csv_file", help="CSV file with columns: timestamp, event")
    args = parser.parse_args()

    csv_file = args.csv_file

    # Load and analyze data
    logging.info(f"Loading data from {csv_file}...")
    events = load_data(csv_file)

    # Analyze state-dependent behavior
    state_data = analyze_state_dependent_behavior(events)

    # Calculate parameters for each state
    parameters = calculate_state_parameters(state_data)

    # Print statistics and insights
    print_statistics(parameters)

    # Generate C++ constants
    cpp_content = generate_cpp_constants(parameters)

    # Output files
    output_filename = "smm_parameters.h"
    with open(output_filename, "w") as f:
        f.write(cpp_content)

    logging.info("=== C++ Header File Generated ===")
    logging.info(f"Output written to: {output_filename}")
    logging.info("Copy the following content to your Arduino sketch:")
    logging.info("=" * 60)
    logging.info(cpp_content)
    logging.info("=" * 60)


if __name__ == "__main__":
    main()
