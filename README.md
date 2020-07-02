Eliciting event ties in ROSS to test for non-determinism
---
---

This model is designed to be absolutely terrible for a PDES simulator that doesn't handle event ties deterministically. Meaning from simulation to simulation, while events with different timestamps occur in timestamp order by the end of the simulation, the order of events with the same timestamps may occur in different orders.

This is not a problem if the model properly dithers its events with random values to try and ensure that each event has a unique timestamp but sometimes, especially in long running simulations, RNG collisions can occur despite efforts to avoid it.


If a model uses only operations on state that are 100% commutative, then this isn't really a big deal. For example, if every event had a random value that, when received, was added to the running sum on the receiving LP (and this value is subtracted during rollbacks), then the order in which events are processed - even tie events - doesn't matter.

That is not the case if instead of addition we use an operation like mean. That is what this model does.
Every LP starts with a random value called its cur_rec_mean - current recursive mean. It sends a group of new events to random destinations, each event message containing a different random value.

Upon receipt of an event, an LP will take its current recursive mean and average it with the new random value that it received. This is not attempting to create an average over all random values received - that would be order independent. This is creating an average of an average of an average... and so on.

This is strictly order dependent and different orders can have wildly different final values. This is based on the property:

`Mean(Mean(A,B),C) != Mean(Mean(A,C),B)`

---

To run this simulation, you need [ROSS](https://github.com/ROSS-org/ROSS). Symlink this model into the ROSS source `models` folder and build ROSS.

Once ROSS is built, an example execution in sequential mode is:

```
mpirun -n 1 models/eties/eties --synch=1 --extramem=1000 --nlp=4 --start-events=10 --end=10000
```

That will run the simulation in sequential mode with 4 LPs, each creating 10 starting events, and ending at timestamp 10000. The final output, the running mean and running sums printed out at the end, should be deterministic - the same every time the simulation is run.

To run this same simulation in parallel conservative on 4 PEs:

```
mpirun -n 4 models/eties/eties --synch=2 --extramem=1000 --nlp=1 --start-events=10 --end=10000

```

This should also be deterministic and its output should match the sequential execution. If it doesn't, then there is a problem with deterministic ordering of forward events.

To run this same simulation in parallel optimistic on 4 PEs:

```
mpirun -n 4 models/eties/eties --synch=3 --extramem=1000 --nlp=1 --start-events=10 --end=10000
```

If ROSS doesn't handle event ties properly: by either rolling events back in a non-deterministic way or replaying them forward in a non-deterministic way, then this will both not match the sequential execution output - nor will it be consistent from run to run. This effect happens less frequently when there are not many rollbacks or event ties so extending the end time of the simulation will lead to more event ties over a longer period of time.

A helpful tool in debugging is to use optimistic debug, or --synch=4. This runs all events forward in sequential, but then rolls them all back as well to the beginning of the simulation. If the LP state at the end of the simulation does not match the LP state at the beginnning, then there is something wrong - likely with the model level reverse compuation:

```
mpirun -n 1 models/eties/eties --synch=4 --extramem=1000 --nlp=4 --nkp=1 --start-events=10 --end=10000
```


This model is also used to test a possible fix in ROSS for this behavior by using an independent deterministic RNG value for aiding in the processing of event ties to ensure that they are processed in the correct order. If that feature is enabled, then there is an additional debug option #define'd that allows for the printing out of the count of that RNG value which can be verified to be fully rolled back using `--synch=4`.