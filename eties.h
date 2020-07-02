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
	double running_sum;
};

struct eties_message
{
	double val;
	double rc_saved_mean;
};

/*
* eties Globals
*/
tw_stime lookahead = 1.0;
static tw_stime mult = 1.4;
static unsigned int ttl_lps = 0;
static unsigned int nlp_per_pe = 8;
static int g_eties_start_events = 1;
static int optimistic_memory = 100;

// rate for timestamp exponential distribution
static tw_stime rand_mean = 1.0;

static char run_id[1024] = "undefined";

#endif
