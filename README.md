# Elevator Simulation (Multithreaded, C++20)

This is a multithreaded elevator simulator. Each elevator runs in its own thread. Each customer starts at floor 0, follows a sequence of destinations, does some work on each floor, then either queues for the next ride or leaves when finished.

## How it works

- Elevators share a waiting pool protected by a single mutex.
- An elevator wakes, picks the closest waiting customer, travels to the pickup floor, opens and closes doors, then travels to the destination.
- After drop-off, the customer runs `doStuff()` (a short sleep to simulate time spent on the floor).
- If the customer has more destinations, they become waiting again from that floor. If not, they leave the system.
- The program exits when all customers are done and no one is waiting.

## Input format

One line per customer, comma-separated list of destination floors.

Example `requests.txt`:
  2, 3, 5
  4, 4, 6
Meaning:
- Customer 0: start at 0 → 2 → 3 → 5 → done  
- Customer 1: start at 0 → 4 → 4 → 6 → done

## Build

```bash
g++ -std=c++20 -O2 elevator_simple.cpp -o elevator_simple -pthread
./elevator_simple < requests.txt

Example Output:
[Elev 1] doors opening (pickup C1)
[Elev 1] doors closing
[Elev 1] C1 boarded at F4, dest F6
...
[Elev 0] shutting down
[Elev 1] shutting down
[System] All customers finished. Exiting.

