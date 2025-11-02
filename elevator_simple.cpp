#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <sstream>
#include <climits>

using namespace std;

//  Config
const int kMaxFloor = 20;
const int kMovePerFloorMs = 80;
const int kDoorMs = 60; // open/close each
const int kNumElevators = 2;

// Simulate arbitrary time a customer spends at a floor
void doStuff(int customerId, int floor)
{
    int min = 50;
    int max = 200;
    int stuff = rand() % (max - min + 1) + min; // generate random number between 50 and 200
    this_thread::sleep_for(chrono::milliseconds(stuff));
}

//  shared types
struct Customer
{
    int id;
    vector<int> schedule; // sequence of destination floors
    int currentFloor = 0;
    bool waiting = false; // waiting at current floor
    bool done = false;

    // waiting room for customer
    condition_variable cv;
};

struct Elevator
{
    int id;
    int currentFloor = 0;
    thread worker;
};

//  Global shared state (protected by g_lock)
mutex g_lock;
deque<Customer> all_customers;
atomic<int> all_activeCustomers = 0; // how many not done yet
condition_variable all_waiting;      // signaled when waiting set changes

// pick nearest waiting customer to elevatorFloor; returns customer index or -1; tie break arbitrarily
int pickNearestWaiting(int elevatorFloor)
{
    int bestIdx = -1;
    int bestDist = INT_MAX;
    for (int i = 0; i < (int)all_customers.size(); i++)
    {
        const auto &c = all_customers[i];
        if (!c.waiting || c.done)
            continue;

        int d = abs(c.currentFloor - elevatorFloor);
        if (d < bestDist)
        {
            bestDist = d;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void moveOneFloor(int &cur, int target, int elevId)
{
    using namespace chrono;

    if (target > cur)
    {
        this_thread::sleep_for(milliseconds(kMovePerFloorMs));
        cur++;
        cout << "[Elev " << elevId << "] up to F" << cur << "\n";
    }
    else if (target < cur)
    {
        this_thread::sleep_for(milliseconds(kMovePerFloorMs));
        cur--;
        cout << "[Elev " << elevId << "] down to F" << cur << "\n";
    }
}

void doorCycle(int elevId, const string &note)
{
    using namespace chrono;
    cout << "[Elev " << elevId << "] doors opening (" << note << ")\n";
    this_thread::sleep_for(milliseconds(kDoorMs));
    cout << "[Elev " << elevId << "] doors closing\n";
    this_thread::sleep_for(milliseconds(kDoorMs));
}

// elevator thread loop
void runElevator(Elevator *elev)
{
    using namespace chrono;

    while (true)
    {
        int chosen = -1;

        {
            unique_lock<mutex> lk(g_lock);

            // wait until there is at least one waiting customer or no customers remain (then we stop)
            all_waiting.wait(lk, []
                             {
                if (all_activeCustomers == 0) return true; // all done, exit
                for (auto& c : all_customers) if (c.waiting && !c.done) return true;
                return false; });

            if (all_activeCustomers == 0)
            {
                // everyone is done, exit elevator thread
                cout << "[Elev " << elev->id << "] shutting down\n";
                return;
            }

            // pick the nearest waiting customer
            chosen = pickNearestWaiting(elev->currentFloor);
            if (chosen != -1)
            {
                all_customers[chosen].waiting = false; // reserve this customer
            }
            else
            {
                // spurious wakeup or race conditions, loop again
                continue;
            }
        }

        // We release the lock while traveling and serving
        auto &cust = all_customers[chosen];

        // step 1: travel to pickup floor
        while (elev->currentFloor != cust.currentFloor)
        {
            moveOneFloor(elev->currentFloor, cust.currentFloor, elev->id);
        }
        doorCycle(elev->id, "pickup C" + to_string(cust.id));

        // step 2: see where next floor is from the schedule vector, exit if waiting
        int dest = -1;
        {
            lock_guard<mutex> lk(g_lock);

            // find next floor for this customer
            if (!cust.schedule.empty())
            {
                dest = cust.schedule.front();
                cust.schedule.erase(cust.schedule.begin());
            }
            else
            {
                // shouldn't happen
                dest = cust.currentFloor;
            }
        }

        cout << "[Elev " << elev->id << "] C" << cust.id
             << " boarded at F" << cust.currentFloor
             << ", dest F" << dest << "\n";

        // step 3: travel to floor
        while (elev->currentFloor != dest)
        {
            moveOneFloor(elev->currentFloor, dest, elev->id);
        }
        doorCycle(elev->id, "drop C" + to_string(cust.id));

        // step 4: drop off and update customer, maybe wait, maybe finish
        {
            lock_guard<mutex> lk(g_lock);
            cust.currentFloor = dest;

            if (cust.schedule.empty())
            {
                // customer leaves building so go to 0th floor
                cust.done = true;
                all_activeCustomers--;
                cout << "[Elev " << elev->id << "] C" << cust.id
                     << " finished at F" << dest
                     << " | remaining customers: " << all_activeCustomers.load() << "\n";

                // signal others in case this was the last one
                all_waiting.notify_all();
            }
            else
            {
            }
        }

        // do stuff outside the lock
        if (!cust.done)
        {
            doStuff(cust.id, dest);

            // enter waiting state again from the same floor
            {
                lock_guard<mutex> lk(g_lock);
                cust.waiting = true;
                // wake up elevator to pick up
                all_waiting.notify_one();
            }
        }
    }
}

// parse one line like "2, 3, 5" to vector<int>{2,3,5}
vector<int> parseLine(const string &line)
{
    vector<int> out;
    stringstream ss(line);
    string tok;
    while (getline(ss, tok, ','))
    {
        size_t a = tok.find_first_not_of(" \t\r\n");
        size_t b = tok.find_last_not_of(" \t\r\n");
        if (a == string::npos)
            continue;
        int v = stoi(tok.substr(a, b - a + 1));
        if (v < 0 || v > kMaxFloor)
        {
            throw runtime_error("Floor out of range in input: " + to_string(v));
        }
        out.push_back(v);
    }
    return out;
}

// input format : stdin or a file redirected
// each line is a customer: "2, 3, 5"
// this mean they start at 0 -> 2 -> 3 -> 5 -> 0  these are the order of floors they go to
int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    vector<string> lines;
    string line;
    while (getline(cin, line))
    {
        if (!line.empty())
            lines.push_back(line);
    }

    // build customers
    {
        lock_guard<mutex> lk(g_lock);

        all_customers.clear();
        int id = 0;
        for (auto &ln : lines)
        {
            auto itin = parseLine(ln);
            all_customers.emplace_back();
            Customer &c = all_customers.back();
            c.id = id++;
            c.currentFloor = 0;
            c.schedule = move(itin);
            c.waiting = !c.schedule.empty();
            c.done = c.schedule.empty();
        }

        all_activeCustomers = 0;
        for (auto &c : all_customers)
            if (!c.done)
                all_activeCustomers++;
    }

    // kick elevators if any customer is already waiting
    {
        lock_guard<mutex> lk(g_lock);
        all_waiting.notify_all();
    }

    // start elevators
    vector<Elevator> elevators;
    elevators.reserve(kNumElevators);
    for (int i = 0; i < kNumElevators; i++)
    {
        elevators.push_back(Elevator{i});
        elevators.back().worker = thread(runElevator, &elevators.back());
    }

    // wait for all customers to finish
    while (true)
    {
        unique_lock<mutex> lk(g_lock);
        if (all_activeCustomers == 0)
        {
            // wake all elevators so they can observe termination and exit
            all_waiting.notify_all();
            break;
        }
        // sleep a bit and check again, elevators will progress customers
        lk.unlock();
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    // join elevators
    for (auto &e : elevators)
    {
        if (e.worker.joinable())
            e.worker.join();
    }

    cout << "[System] All customers finished. Exiting.\n";
    return 0;
}
