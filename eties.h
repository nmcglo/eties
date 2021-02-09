#ifndef INC_eties_h
#define INC_eties_h

#include <ross.h>

/*
* eties Types
*/

typedef struct eties_state eties_state;
typedef struct eties_message eties_message;

struct eties_state
{
    double cur_rec_mean;
	unsigned long long running_sum;
	unsigned int next_incast_time;
	unsigned int incasts_completed;
	int received;
};

struct eties_message
{
	int chain_identifier; //how many events, including the start event, have been created by the start event
	int sum_identifier;
	int val;
	int offset;
	double rc_saved_mean;
	int rc_saved_sum;
	int num_rngs;
	int num_rngs2;
	tw_lpid original_lpid;
	short is_start;
};

/*
* eties Globals
*/
tw_stime lookahead = 1.0;
static tw_stime mult = 1.4;
static tw_stime percent_override = 0;
static unsigned int num_incast = 0;
static tw_stime percent_remote = 0.25;
static unsigned int ttl_lps = 0;
static unsigned int nlp_per_pe = 8;
static int timestep_increment = 1;
static int g_eties_start_events = 1;
static int g_eties_events_per_start = 1; //how many events are allowed to be created per start event?
static int g_eties_child_events = 1;
static int optimistic_memory = 100;

// rate for timestamp exponential distribution
static tw_stime rand_mean = 1.0;

static char run_id[1024] = "undefined";

#endif
