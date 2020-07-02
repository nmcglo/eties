#include "eties.h"

#define DEBUG_RAND_TIEBREAKER 0

unsigned int nlp_in_other_pes;

tw_peid
eties_map(tw_lpid gid)
{
	return (tw_peid) gid / g_tw_nlp;
}

void
eties_init(eties_state * s, tw_lp * lp)
{
    (void*)lp;
	s->cur_rec_mean = tw_rand_unif(lp->rng)*100.0; //give it a random starting value
	s->running_sum = s->cur_rec_mean;

	printf("LP %ld: Starting Mean: %.10f\n",lp->gid, s->cur_rec_mean);

	for(int i = 0; i < g_eties_start_events; i++) {
		tw_lpid	 dest = tw_rand_integer(lp->rng, 0, ttl_lps -1);

		if(dest >= (g_tw_nlp * tw_nnodes()))
			tw_error(TW_LOC, "bad dest");

		tw_event *e = tw_event_new(dest, 1, lp);	
		eties_message *new_m = tw_event_data(e);
		new_m->val = tw_rand_unif(lp->rng) * 100.0;
		tw_event_send(e);
	}

//This feature of the model for debugging the tiebreaker uses lp->core_rng which doesn't exist in mainline ross at time of dev
//Make the compiler happy if building with mainline ROSS
#ifdef USE_RAND_TIEBREAKER
	if (DEBUG_RAND_TIEBREAKER) {
		// tw_stime val = tw_rand_unif(lp->core_rng);
		printf("%ld: Post-Init Core RNG Count: %ld\n", lp->gid, lp->core_rng->count); //should equal --start-events
		// printf("%ld: Post-Init Core Random Value %.5f\n", lp->gid, val);
	}
#endif

}

void
eties_event_handler(eties_state * s, tw_bf * bf, eties_message * m, tw_lp * lp)
{
    (void) s;
    (void) m;

	//process the new mean destructively for current LP state

	//this operation is order dependent - so event ordering really matters
	//if event ties are not handled deterministically, this will cause a problem
	//Relies on fact that Mean(Mean(A,B),C) != Mean(Mean(A,C),B)
	//Another possible operation is modulo as A % B % C != A % C % B
	double new_mean = (m->val + s->cur_rec_mean) / 2.0;
	// double new_mean = floor((m->val + s->cur_rec_mean) / 2.0);

	m->rc_saved_mean = s->cur_rec_mean;
	s->cur_rec_mean = new_mean;
	s->running_sum += m->val; //adheres to associative property - should be deterministic on event ties

	//if the event to be created is less than the end time, proceed - otherwise stop generating new events
	if (tw_now(lp)+1 < g_tw_ts_end) {
		bf->c1 = 1;
		tw_lpid	 dest = tw_rand_integer(lp->rng, 0, ttl_lps -1);

		if(dest >= (g_tw_nlp * tw_nnodes()))
			tw_error(TW_LOC, "bad dest");

		tw_event *e = tw_event_new(dest, 1, lp);	
		eties_message *new_m = tw_event_data(e);
		new_m->val = tw_rand_unif(lp->rng) * 100.0;
		tw_event_send(e);
	}
}

void
eties_event_handler_rc(eties_state * s, tw_bf * bf, eties_message * m, tw_lp * lp)
{
	if (bf->c1) {
		tw_rand_reverse_unif(lp->rng); //new mean number
		tw_rand_reverse_unif(lp->rng); //dest
	}
	
	s->cur_rec_mean = m->rc_saved_mean; //Saved state is safer when dealing with floats
	s->running_sum -=m->val; //The running sum is meant to be a verification. Still working with floats but not looking with enough precision to matter
}

void eties_commit(eties_state * s, tw_bf * bf, eties_message * m, tw_lp * lp)
{
    (void) s;
    (void) bf;
    (void) m;
    (void) lp;
}

void
eties_finish(eties_state * s, tw_lp * lp)
{
	//If the running mean and the runnng sum are both consistent between separate runs, the model is deterministic
	printf("LP %ld:  Final Running Mean %.10f    Running Sum %.3f\n",lp->gid, s->cur_rec_mean, s->running_sum);

	//Verify the core tiebreaking RNG - in optimistic debug, this value should equal that of post-init
#ifdef USE_RAND_TIEBREAKER
	if (DEBUG_RAND_TIEBREAKER) {
		// tw_rand_reverse_unif(lp->core_rng); //undo the value generation
		// tw_stime val = tw_rand_unif(lp->core_rng);
		printf("%ld: Final Core RNG Count: %ld\n", lp->gid, lp->core_rng->count);
		// printf("%ld: Final Core Random Value %.5f\n", lp->gid, val);
	}
#endif

}

tw_lptype       mylps[] = {
	{(init_f) eties_init,
     (pre_run_f) NULL,
	 (event_f) eties_event_handler,
	 (revent_f) eties_event_handler_rc,
	 (commit_f) eties_commit,
	 (final_f) eties_finish,
	 (map_f) eties_map,
	sizeof(eties_state)},
	{0},
};

void event_trace(eties_message *m, tw_lp *lp, char *buffer, int *collect_flag)
{
    (void) m;
    (void) lp;
    (void) buffer;
    (void) collect_flag;
    return;
}

void eties_stats_collect(eties_state *s, tw_lp *lp, char *buffer)
{
    (void) s;
    (void) lp;
    (void) buffer;
    return;
}

st_model_types model_types[] = {
    {(ev_trace_f) event_trace,
     0,
    (model_stat_f) eties_stats_collect,
    sizeof(int),
    NULL, //(sample_event_f)
    NULL, //(sample_revent_f)
    0},
    {0}
};

const tw_optdef app_opt[] =
{
	TWOPT_GROUP("eties Model"),
	TWOPT_UINT("nlp", nlp_per_pe, "number of LPs per processor"),
	TWOPT_DOUBLE("mult", mult, "multiplier for event memory allocation"),
	TWOPT_DOUBLE("lookahead", lookahead, "lookahead for events"),
	TWOPT_UINT("start-events", g_eties_start_events, "number of initial messages per LP"),
	TWOPT_UINT("memory", optimistic_memory, "additional memory buffers"),
	TWOPT_CHAR("run", run_id, "user supplied run name"),
	TWOPT_END()
};

int
main(int argc, char **argv)
{

#ifdef TEST_COMM_ROSS
    // Init outside of ROSS
    MPI_Init(&argc, &argv);
    // Split COMM_WORLD in half even/odd
    int mpi_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm split_comm;
    MPI_Comm_split(MPI_COMM_WORLD, mpi_rank%2, mpi_rank, &split_comm);
    if(mpi_rank%2 == 1){
        // tests should catch any MPI_COMM_WORLD collectives
        MPI_Finalize();
    }
    // Allows ROSS to function as normal
    tw_comm_set(split_comm);
#endif

	unsigned int i;

	// set a min lookahead of 1.0
	lookahead = 1.0;
	tw_opt_add(app_opt);
	tw_init(&argc, &argv);

#ifdef USE_DAMARIS
    if(g_st_ross_rank)
    { // only ross ranks should run code between here and tw_run()
#endif
	if( lookahead > 1.0 )
	  tw_error(TW_LOC, "Lookahead > 1.0 .. needs to be less\n");

	ttl_lps = tw_nnodes() * nlp_per_pe;
	g_tw_events_per_pe = (mult * nlp_per_pe * g_eties_start_events) +
				optimistic_memory;
	//g_tw_rng_default = TW_FALSE;
	g_tw_lookahead = lookahead;

	nlp_in_other_pes = (tw_nnodes()-1) * nlp_per_pe;

	tw_define_lps(nlp_per_pe, sizeof(eties_message));

	for(i = 0; i < g_tw_nlp; i++)
    {
		tw_lp_settype(i, &mylps[0]);
        st_model_settype(i, &model_types[0]);
    }

        if( g_tw_mynode == 0 )
	  {
	    printf("========================================\n");
	    printf("eties Model Configuration..............\n");
	    printf("   Lookahead..............%lf\n", lookahead);
	    printf("   Start-events...........%u\n", g_eties_start_events);
	    printf("   Mult...................%lf\n", mult);
	    printf("   Memory.................%u\n", optimistic_memory);
	    printf("========================================\n\n");
	  }

	tw_run();
#ifdef USE_DAMARIS
    } // end if(g_st_ross_rank)
#endif
	tw_end();

	return 0;
}
