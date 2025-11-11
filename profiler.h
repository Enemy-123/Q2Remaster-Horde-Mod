#ifndef PROFILER_H
#define PROFILER_H

#pragma once // Often used for header guards, especially in MSVC

// Required Standard Library Headers
#include <chrono>   // For high-resolution timing
#include <string>   // For function names
#include <vector>   // For storing timing history
#include <map> // Add this include

void Profiler_ResetFrame();       // Call this at the START of G_RunFrame
void Profiler_UpdateHistory();    // Called internally by Profiler_RunFrame_End
void Profiler_PrintResults();     // Called internally by Profiler_RunFrame_End
void Profiler_RunFrame_End();     // <--- ADD THIS DECLARATION
void Profiler_Reset();            // Call this on resetgame to clear all profiling data

// Forward Declarations (if needed, depending on your project structure)
// If g_local.h or another common header already defines cvar_t, you might not need this.
// struct cvar_s;
// typedef struct cvar_s cvar_t;

// --- Profiler Data Structure ---

struct ProfileData {
	std::string name = "Unnamed"; // Function name
	std::chrono::nanoseconds total_duration_this_frame{ 0 }; // Sum of durations this frame
	std::chrono::nanoseconds max_duration_this_frame{ 0 };   // Max single call duration this frame
	uint32_t call_count_this_frame = 0;                    // How many times called this frame

	// History for averaging/smoothing
	std::vector<std::chrono::nanoseconds> history;           // Stores total_duration_this_frame for past frames
	static constexpr size_t HISTORY_SIZE = 60;             // Number of frames to keep history (e.g., ~1 sec at 60Hz)

	// Member function declarations (defined in profiler.cpp)
	void reset_frame();
	void record_duration(std::chrono::nanoseconds duration);
	void update_history();
	double get_average_ms() const;
	double get_max_ms_history() const;
};

// --- Global Profiler Variables (Declarations) ---
// These tell the compiler these variables exist elsewhere (in profiler.cpp)

// Use forward declaration for std::map if you don't want to include <map> here
extern std::map<std::string, ProfileData> g_profiler_data; // The main storage for profile results

extern bool g_profiler_enabled; // Global flag to turn profiling on/off

// --- Profiler Management Function Prototypes ---
// These functions manage the profiler state each frame (defined in profiler.cpp)

// Resets the per-frame counters (total_duration_this_frame, etc.)
void Profiler_ResetFrame();

// Moves the completed frame's total time into the history buffer
void Profiler_UpdateHistory();

// Prints the formatted profiling results to the console periodically
void Profiler_PrintResults();


// --- RAII Timer Scope Class (Definition MUST be in header) ---
// This class automatically times the scope it's declared in.

class ProfilerScope {
public:
	// Constructor: Stores name, checks if enabled, starts timer
	ProfilerScope(const std::string& function_name) :
		m_name(function_name),
		m_active(g_profiler_enabled) // Capture enabled state at construction
	{
		// Only start the timer if the profiler is globally enabled
		if (m_active) {
			m_start_time = std::chrono::high_resolution_clock::now();
		}
	}

	// Destructor: Stops timer, calculates duration, records it if it was active
	~ProfilerScope() {
		// Only record if the profiler was active when this scope object was created
		if (m_active) {
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - m_start_time);

			// Access the global map (defined in profiler.cpp)
			// operator[] conveniently gets or creates the entry
			auto& data_entry = g_profiler_data[m_name];

			// Ensure the name field is set correctly, especially if the entry was just created
			if (data_entry.name == "Unnamed") {
				data_entry.name = m_name;
			}
			// Record the measured duration for this specific call
			data_entry.record_duration(duration);
		}
	}

	// --- Prevent Copying/Moving ---
	// Ensures the timer starts/stops correctly for the specific scope
	ProfilerScope(const ProfilerScope&) = delete;
	ProfilerScope& operator=(const ProfilerScope&) = delete;
	ProfilerScope(ProfilerScope&&) = delete;
	ProfilerScope& operator=(ProfilerScope&&) = delete;

private:
	std::string m_name;                                                 // Name of the function/scope being timed
	std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time; // Start time point
	bool m_active;                                                      // Was the profiler enabled when this scope started?
};

// --- Helper Macro (Definition MUST be in header) ---
// Provides a convenient way to use ProfilerScope.
// Creates a ProfilerScope object with a unique variable name based on the line number.
#define PROFILE_SCOPE(name) ProfilerScope _profiler_scope##__LINE__(name)


// Macro to safely increment a uint16_t counter with a runtime limit check
// and an optional compile-time type check.
#define INCREMENT_U16_WITH_LIMIT(counter, context_string) \
    do { \
        /* Compile-time check: Ensure the variable passed IS a uint16_t */ \
        static_assert(std::is_same_v<decltype(counter), uint16_t>, \
                      #counter " passed to INCREMENT_U16_WITH_LIMIT is not a uint16_t"); \
        \
        /* Runtime check: Prevent overflow */ \
        if ((counter) < std::numeric_limits<uint16_t>::max()) { \
            (counter)++; \
        } else if (developer && developer->integer) { \
            /* Optional runtime warning when limit is hit */ \
            /* Use ## to create unique static variable names based on the counter name */ \
            static int overflow_warn_##counter = -1; \
            if (overflow_warn_##counter != g_horde_local.level) { \
                 /* Print context string passed to the macro */ \
                 gi.Com_PrintFmt("Warning: uint16_t limit reached for " #counter " in {} (Wave {})\n", \
                                 context_string, g_horde_local.level); \
                 overflow_warn_##counter = g_horde_local.level; \
            } \
        } \
    } while(0) // do-while(0) idiom for safe macro usage
    
#endif // PROFILER_H