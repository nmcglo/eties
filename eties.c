#include "eties.h"

#define DEBUG_RAND_TIEBREAKER 0

unsigned int nlp_in_other_pes;

int* incast_times;

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
	s->running_sum = (int) s->cur_rec_mean;
	s->next_incast_time = incast_times[0];
	s->incasts_completed = 0;
	s->received_triggers = 0;

	if (lp->gid < 100)
		printf("LP %ld: Starting Mean: %.10f\n",lp->gid, s->cur_rec_mean);

	for(int i = 0; i < g_eties_start_events; i++) {
		// tw_lpid	 dest = tw_rand_integer(lp->rng, 0, ttl_lps -1);
		tw_lpid dest = lp->gid;

		if(dest >= (g_tw_nlp * tw_nnodes()))
			tw_error(TW_LOC, "bad dest");

		tw_event *e = tw_event_new(dest, 0, lp);	
		eties_message *new_m = tw_event_data(e);
		new_m->val = (int) (tw_rand_unif(lp->rng) * 100.0);
		new_m->chain_identifier = 1;
		tw_event_send(e);
		// printf("%d: Scheduling event %d:%d for (%.4f,%.4f)\n",lp->gid, lp->pe->id, e->event_id, e->sig.recv_ts, e->sig.event_tiebreaker);
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
	m->num_rngs = 0;
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

	//If we have a set number of events to be generated per start event, AND
	//if we've already generated enough causal events eminating from this event, then don't generate another event

	tw_stime offset;
	if(g_eties_events_per_start > 0 && m->chain_identifier >= g_eties_events_per_start) {
		bf->c10 = 1;
		offset = 1;
		m->chain_identifier = 0;
	}
	else {
		offset = timestep_increment;
	}
	// printf("offset = %.4f\n",offset);

	//we only want to trigger g_eties_start_events number of children for regular offset events
	if (m->offset == 1)
	{
		bf->c11;
		s->received_triggers++;
		if (s->received_triggers == g_eties_start_events) {
			bf->c12 = 1;
			s->received_triggers = 0;
			return;
		}
	}
	


	//if the event to be created is less than the end time, proceed - otherwise stop generating new events
	//This is generally fine but if we are testing the tiebreaker RNG rollback count, then new'ing an event
	//that is never actually sent or processed will cause discrepancies.
	if (tw_now(lp)+offset < g_tw_ts_end) {
		tw_lpid	dest;
		int do_incast = 0;
		int now = tw_now(lp);
		if (num_incast > 0 && now+offset == s->next_incast_time) {
			bf->c2 = 1;
			dest = 0;
			do_incast = 1;
			s->incasts_completed += 1;
			s->next_incast_time = incast_times[s->incasts_completed];
		}

		// dest = (lp->gid +1)%ttl_lps;

		// int normal_dest = 1;
		// if (percent_override > 0) {
		// 	bf->c6 = 1;
		//  	if (tw_rand_unif(lp->rng) <= percent_override) {
		// 		dest = (lp->gid + (int)s->cur_rec_mean)%ttl_lps;
		// 		normal_dest = 0;
		// 	}
		// }		
		// if (normal_dest) 		
		// {
		// 	bf->c3 = 1;
		// 	int now = tw_now(lp);
		// 	if(now+1 == s->next_incast_time)
		// 	{
		// 		bf->c4 = 1;
		// 		dest = 0;
		// 		s->incasts_completed += 1;
		// 		s->next_incast_time = incast_times[s->incasts_completed];
		// 	}
		// 	else
		// 	{
		// 		if (tw_rand_unif(lp->rng) <= percent_remote)
		// 		{
		// 			bf->c3 = 1;
		// 			dest = tw_rand_integer(lp->rng, 0, ttl_lps - 1);
		// 			// Makes PHOLD non-deterministic across processors! Don't uncomment
		// 			/* dest += offset_lpid; */
		// 			/* if(dest >= ttl_lps) */
		// 			/* 	dest -= ttl_lps; */
		// 		} else
		// 		{
		// 			dest = lp->gid;
		// 		}
		// 	}
		// }

		int num_child_events = 1;
		if (offset == 0)
			num_child_events = g_eties_child_events;

		for(int i = 0; i <  num_child_events; i++)
		{
			bf->c3 = 1;
			m->num_rngs++;
			if (tw_rand_unif(lp->rng) <= percent_remote) {
				bf->c4 = 1;
				m->num_rngs++;
				if (tw_rand_unif(lp->rng) <= percent_override)
					dest = (lp->gid + (int)s->cur_rec_mean)%ttl_lps;
				else {
					bf->c5 = 1;
					dest = tw_rand_integer(lp->rng, 0, ttl_lps - 1);
					m->num_rngs++;
				}
			}
			else {
				dest = lp->gid;
			}
			if(dest >= (g_tw_nlp * tw_nnodes()))
				tw_error(TW_LOC, "bad dest");

			tw_event *e = tw_event_new(dest, offset, lp);
			eties_message *new_m = tw_event_data(e);
			bf->c1 = 1;
			new_m->val = (int) (tw_rand_unif(lp->rng) * 100.0);
			new_m->offset = offset;
			m->num_rngs++;
			new_m->chain_identifier = m->chain_identifier + 1;
			tw_event_send(e);
		}



		// printf("%d: Scheduling event %d:%d for (%.4f,%.4f)\n",lp->gid, lp->pe->id, e->event_id, e->sig.recv_ts, e->sig.event_tiebreaker);
		// sleep(1);
	}
	
}

void
eties_event_handler_rc(eties_state * s, tw_bf * bf, eties_message * m, tw_lp * lp)
{
	// printf("%d: reverse! (%.4f, %.4f)\n",lp->gid, tw_now_sig(lp).recv_ts, tw_now_sig(lp).event_tiebreaker );
	s->cur_rec_mean = m->rc_saved_mean; //Saved state is safer when dealing with floats
	s->running_sum -=m->val; //The running sum is meant to be a verification. Still working with floats but not looking with enough precision to matter

	if (bf->c10) {
		m->chain_identifier = g_eties_events_per_start;
		// return;
	}

	if (bf->c11) {
		s->received_triggers--;
		if (bf->c12) {
			s->received_triggers = g_eties_start_events-1;
			return;
		}
	}

	// if (bf->c1) {
	// 	tw_rand_reverse_unif(lp->rng); //new val
	// }
	if (bf->c2) {
		s->incasts_completed -= 1;
		s->next_incast_time = incast_times[s->incasts_completed];
	}
	// if (bf->c3) {
	// 	tw_rand_reverse_unif(lp->rng); //remote coin flip
	// 	if (bf->c4)
	// 		tw_rand_reverse_unif(lp->rng); //override coin flip
	// 	if (bf->c5)
	// 		tw_rand_reverse_unif(lp->rng); //random remote dest
	// }
	for(int i = 0; i < m->num_rngs; i++)
	{
		tw_rand_reverse_unif(lp->rng);
	}
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
	if (lp->gid < 100)
		printf("LP %ld:  Final Running Mean %.10f    Running Sum %lld\n",lp->gid, s->cur_rec_mean, s->running_sum);

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
	TWOPT_DOUBLE("override", percent_override, "whether or not we override destination based on cur value"),
	TWOPT_UINT("incast", num_incast, "number of planned incasts"),
	TWOPT_DOUBLE("remote", percent_remote, "desired remote event rate"),
	TWOPT_UINT("nlp", nlp_per_pe, "number of LPs per processor"),
	TWOPT_DOUBLE("mult", mult, "multiplier for event memory allocation"),
	TWOPT_DOUBLE("lookahead", lookahead, "lookahead for events"),
	TWOPT_UINT("timestep-increment", timestep_increment, "timestamp offset between causal and resulting events"),
	TWOPT_UINT("start-events", g_eties_start_events, "number of initial messages per LP"),
	TWOPT_UINT("chain-length", g_eties_events_per_start, "total number of messages generated per start event"),
	TWOPT_UINT("child-events", g_eties_child_events, "total number of parallel messages generated per event"),
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

	
	incast_times = (int*)calloc(num_incast, sizeof(int));
	tw_stime gap = g_tw_ts_end/(num_incast+1);
	printf("incast times: ");
	for(int i = 1; i <= num_incast; i++)
	{
		incast_times[i-1] = (int)(gap*i);
		printf("%d ", incast_times[i-1]);
	}
	printf("\n");

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
