// profiler.cpp

#include "profiler.h" // Include the header file first

// Include necessary engine/game headers
#include "g_local.h"  // For gi, level, gtime_t, edict_t, cvar_t, etc.
#include "memory_safety.h"  // For memory safety wrappers

// Include necessary standard library headers for implementation
#include <boost/container/flat_map.hpp>  // For boost::container::flat_map
#include <boost/container/small_vector.hpp>  // For boost::container::small_vector
#include <numeric>   // For std::accumulate
#include <algorithm> // For std::sort, std::max_element
#include <vector>    // For std::vector definition (still used in PrintResults)
#include <utility>   // For std::pair (used implicitly by map)

// --- Global Profiler Variable Definitions ---
// These lines actually create the variables in memory.

boost::container::flat_map<std::string, ProfileData> g_profiler_data; // The actual flat_map instance (cache-friendly sorted vector)
bool g_profiler_enabled = false;                    // Initialize the flag to disabled

// Note: The definition for 'cvar_t* g_horde_profiler;' should be in another file
// (like g_main.cpp or wherever your cvars are defined) to avoid linker errors.

// --- ProfileData Member Function Definitions ---

// Resets the counters for a single frame
void ProfileData::reset_frame() {
	total_duration_this_frame = std::chrono::nanoseconds{ 0 };
	max_duration_this_frame = std::chrono::nanoseconds{ 0 };
	call_count_this_frame = 0;
}

// Adds a measured duration to the current frame's total and updates max
void ProfileData::record_duration(std::chrono::nanoseconds duration) {
	total_duration_this_frame += duration;
	// Update the maximum duration seen *this frame*
	if (duration > max_duration_this_frame) {
		max_duration_this_frame = duration;
	}
	call_count_this_frame++;
}

// Moves the completed frame's total duration into the rolling history buffer
void ProfileData::update_history() {
	// Enforce hard limit on history size (small_vector has inline capacity of 60)
	constexpr size_t MAX_HISTORY = std::min(HISTORY_SIZE, MAX_PROFILER_HISTORY);

	// If history buffer is at or beyond max size, remove oldest entry
	if (history.size() >= MAX_HISTORY) {
		history.erase(history.begin()); // Remove oldest element
	}

	// Add the total duration from the just-completed frame to the end
	// small_vector handles capacity automatically (inline storage for HISTORY_SIZE=60)
	history.push_back(total_duration_this_frame);
}

// Calculates the average duration (in milliseconds) over the history period
double ProfileData::get_average_ms() const {
	if (history.empty()) {
		return 0.0; // Avoid division by zero if no history
	}

	// Use long double for the sum to minimize potential overflow if durations are huge
	long double sum_ns_count = 0;
    for(const auto& dur : history) {
        sum_ns_count += static_cast<long double>(dur.count()); // Sum nanosecond counts
    }

	// Calculate average in nanoseconds
	double avg_ns = sum_ns_count / history.size();

	// Convert average nanoseconds to milliseconds
	return avg_ns / 1e6;
}

// Finds the maximum single-frame total duration (in milliseconds) within the history
double ProfileData::get_max_ms_history() const {
	if (history.empty()) {
		return 0.0; // No history, no maximum
	}

	// Find the iterator pointing to the largest duration in the history vector
	auto max_iter = std::max_element(history.begin(), history.end());

	// Dereference the iterator to get the actual duration value (if valid)
	if (max_iter != history.end()) {
		return static_cast<double>(max_iter->count()) / 1e6; // Convert ns to ms
	}

	return 0.0; // Should not be reached if history wasn't empty
}


// --- Profiler Management Function Definitions ---

// Called at the beginning of each game frame to reset counters
void Profiler_ResetFrame() {
	// Iterate through all entries in the global data map
	for (auto& pair : g_profiler_data) {
		// Call the reset function for each ProfileData object
		pair.second.reset_frame();
	}
}

// Called at the end of each game frame to update the history buffers
void Profiler_UpdateHistory() {
	// Only do work if the profiler is actually enabled
	if (!g_profiler_enabled) {
		return;
	}
	// Iterate through all entries in the global data map
	for (auto& pair : g_profiler_data) {
		// Call the history update function for each ProfileData object
		pair.second.update_history();
	}
}

// Called on resetgame to clear all accumulated profiling data
void Profiler_Reset() {
	g_profiler_data.clear();
}

// --- Profiler Printing Function Definition ---

// Prints the collected profiling data to the console periodically
void Profiler_PrintResults() {
	// Static variable to track the last time results were printed
	static gtime_t g_last_profiler_print_time = 0_sec;
	// Constants can be defined here or globally
	constexpr gtime_t PROFILER_PRINT_INTERVAL = 5_sec; // How often to print (in game time)
	constexpr int MAX_PROFILER_LINES_TO_PRINT = 15;   // Max functions to show per printout

	// --- Conditions to Print ---
	if (!g_profiler_enabled || level.time < g_last_profiler_print_time + PROFILER_PRINT_INTERVAL) {
		return; // Not enabled or not time yet
	}

	// Update the last print time *now*
	g_last_profiler_print_time = level.time;

	// --- Prepare Data for Sorting ---
	struct SortableProfileData {
		double avg_ms;
		const ProfileData* data;
		bool operator<(const SortableProfileData& other) const {
			return avg_ms > other.avg_ms; // Sort descending
		}
	};

	std::vector<SortableProfileData> sorted_data;
	// Safe reserve with bounds check
	if (!safe_reserve(sorted_data, std::min(g_profiler_data.size(), MAX_SAFE_RESERVE_SIZE))) {
		gi.Com_Print("WARNING: Failed to reserve memory for profiler data\n");
		return;
	}

	for (const auto& pair : g_profiler_data) {
		if (!pair.second.history.empty()) {
			// Use safe push_back to prevent overflow
			if (!safe_push_back(sorted_data, SortableProfileData{ pair.second.get_average_ms(), &pair.second },
				MAX_SAFE_CONTAINER_SIZE)) {
				gi.Com_Print("WARNING: Too many profiler entries\n");
				break;
			}
		}
	}
	std::sort(sorted_data.begin(), sorted_data.end());
	// --- End Data Preparation ---

	// --- Print Formatted Results to Console ---
	// Use the game's console print function with {} formatting

	// CORRECTED: Use {:.1f} for the float value (history duration)
	gi.Com_PrintFmt("\n--- Horde Profiler Stats (Avg ms / Max Frame ms over ~{:.1f}s) ---\n",
		ProfileData::HISTORY_SIZE * gi.frame_time_s); // Estimate history duration

	int count = 0; // Counter for limiting lines printed
	for (const auto& sorted_entry : sorted_data) {
		// Stop if we've printed the maximum number of lines
		if (++count > MAX_PROFILER_LINES_TO_PRINT) break;

		const ProfileData* data = sorted_entry.data;
		if (!data) continue; // Safety check

		double avg_ms = sorted_entry.avg_ms;         // Get pre-calculated average
		double max_ms = data->get_max_ms_history(); // Get max from history

		// CORRECTED: Use {} formatting with alignment and precision specifiers
		// {:<30} - Left align, width 30 (for the string name)
		// {:6.3f} - Width 6, 3 decimal places, fixed-point (for the double values)
		gi.Com_PrintFmt("{:<30}: {:6.3f} / {:6.3f}\n",
			data->name, // Pass the std::string directly
			avg_ms,     // Average milliseconds (double)
			max_ms);    // Max milliseconds (double)
	}

	// Print a message if no functions were recorded recently
	if (sorted_data.empty()) {
		gi.Com_PrintFmt("  (No profiled functions recorded recently)\n");
	}

	// Print a footer line
	gi.Com_PrintFmt("------------------------------------------------------\n");
}

void Profiler_RunFrame_End() {
    // First, check if the profiler is enabled. If not, do nothing.
    if (!g_profiler_enabled) {
        return;
    }

    // Call the function to move the current frame's data into the history
    Profiler_UpdateHistory();

    // Call the function that checks if it's time to print and prints results
    Profiler_PrintResults();
}