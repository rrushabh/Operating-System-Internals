#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <vector>
#include <list>
#include <string>
#include <regex>
#include <iterator>
#include <unistd.h>

using namespace std;

// initialise global variables
int num_random_numbers;
int ofs = 0;
vector<int> randvals;
int CURRENT_TIME = 0;
int num_performing_IO = 0;
int time_of_IO_start;
int time_of_IO_end;
int total_IO_time = 0;
int max_prio;

bool verbose = false;

enum State { CREATED, READY, RUNNING, BLOCKED };
enum Transition { TRANS_TO_READY, TRANS_TO_PREEMPT, TRANS_TO_RUN, TRANS_TO_BLOCK };
const string States[] = {"CREATED", "READY", "RUNNING", "BLOCKED"};

int myrandom(int burst);

// -------------------------------------------------------------------------------------------------------------- //

// process object: to store all relevant information about processes
struct Process {
    int pid;
    int arrival_time;
    int total_cpu_time;
    int max_cpu_burst; // max cpu burst
    int cpu_burst; // randomly computed cpu burst
    int max_io_burst; // max io burst
    int io_burst; // randomly computed io burst
    int state_ts; // time stamp of entry to current state
    int remaining_cpu_time;
    int static_prio;
    int dynamic_prio;
    int finishing_time;
    int turnaround_time;
    int total_io_time;
    int cpu_waiting_time;

    // default constructor
    Process() : pid(-1), arrival_time(-1), total_cpu_time(-1), cpu_burst(-1), io_burst(-1) {}

    Process(int pid, int arrival_time, int total_cpu_time, int max_cpu_burst, int max_io_burst) {
        this->pid = pid;
        this->arrival_time = arrival_time;
        this->total_cpu_time = total_cpu_time;
        this->remaining_cpu_time = total_cpu_time;
        this->max_cpu_burst = max_cpu_burst;
        this->cpu_burst = 0;
        this->max_io_burst = max_io_burst;
        this->io_burst = 0;
        this->state_ts = arrival_time;
        this->total_io_time = 0;
        this->cpu_waiting_time = 0;
    }

    // conversion operator for boolean logic
    operator bool() const {
        return arrival_time >= 0;
    }

    // set cpu burst before new cpu cycle
    void set_cpu_burst() {
        this->cpu_burst = myrandom(this->max_cpu_burst);
    }

    // set io burst before new io cycle
    void set_io_burst() {
        this->io_burst = myrandom(this->max_io_burst);
    }

};

vector<Process> processes;

// -------------------------------------------------------------------------------------------------------------- //

// event object: to store all relevant information about events
struct Event {
    int time_stamp;
    Process* process;
    int old_state;
    int new_state;

    // default constructor
    Event() : time_stamp(-1), process(), old_state(CREATED), new_state(CREATED) {}

    Event(int time_stamp, Process* process, int old_state, int new_state) {
        this->time_stamp = time_stamp;
        this->process = process;
        this->old_state = old_state;
        this->new_state = new_state;
    }

    // conversion operator for boolean logic
    operator bool() const {
        return time_stamp >= 0;
    }

    // return correct transition based on old state and new state
    int get_transition() {
        int transition;
        switch (this->new_state) {
            case BLOCKED:
                transition = TRANS_TO_BLOCK;
                break;
            case RUNNING:
                transition = TRANS_TO_RUN;
                break;
            case READY:
                if (this->old_state == RUNNING) {
                    transition = TRANS_TO_PREEMPT;
                } else {
                    // mark end of io time if no processes are performing io: needed for tracking total io time
                    if (this->old_state == BLOCKED) {
                        num_performing_IO--;
                        if (num_performing_IO == 0) {
                            time_of_IO_end = CURRENT_TIME;
                            total_IO_time = total_IO_time + (time_of_IO_end - time_of_IO_start);
                        }
                    }
                    transition = TRANS_TO_READY;
                }
                break;
        }
        return transition;
    }
};

// -------------------------------------------------------------------------------------------------------------- //

// Discrete Event Simulation class
class DES {
    public:
        // event queue, maintained in chronological order
        list<Event> eventQ;
        
        // add new event to eventQ
        void add_Event(Event event) {

            // find correct index to add event
            list<Event>::iterator it = eventQ.begin();
            while ((it != eventQ.end()) && (it->time_stamp <= event.time_stamp)) {
                advance(it, 1);
            }

            // add event to eventQ at the correct index
            eventQ.insert(it, event);
        }

        // get next event from eventQ
        Event* get_Event() {

            Event* new_event = new Event();
            if (!eventQ.empty()) {
                // return first event in eventQ
                new_event->time_stamp = eventQ.front().time_stamp;
                new_event->process = eventQ.front().process;
                new_event->old_state = eventQ.front().old_state;
                new_event->new_state = eventQ.front().new_state;
                eventQ.pop_front();
                return new_event;
            }
            return nullptr;
        }

        // return time of next event in queue
        int get_next_event_time() {
            if (eventQ.empty()) {
                return -1;
            }
            return eventQ.front().time_stamp;
        }

        // upon preemption, remove any later blocking/preemption events for the same process
        bool remove_event_at_different_time(Process* p) {
            // iterate through queue to find corresponding event if any
            list<Event>::iterator it = eventQ.begin();
            while (it != eventQ.end()) {
                if ((it->process == p) && (it->time_stamp != CURRENT_TIME)) {
                    // delete event
                    it = eventQ.erase(it);
                    return true;
                } else {
                    ++it;
                }
            }
            return false;
        }
};

DES des;

// -------------------------------------------------------------------------------------------------------------- //

// create scheduler interface, from which specific schedulers are derived
class Scheduler {
    public:
        list<Process*> runQ;
        int quantum;

        virtual void add_process(Process* p) = 0;
        virtual Process* get_next_process() = 0;
        virtual bool does_preempt() = 0;
};

// -------------------------------------------------------------------------------------------------------------- //

// Derived classes with different function implementations to override the virtual functions in Scheduler
class FCFS : public Scheduler {
    public:
        // initialise quantum to 10000
        FCFS() {
            this->quantum = 10000;
        }
        // add process to back of queue
        void add_process(Process *p) { 
            runQ.push_back(p);
        }
        // get next process from front of queue
        Process* get_next_process() { 
            Process* p = nullptr;
            if (!runQ.empty()) {
                p = runQ.front();
                runQ.pop_front();
            }
            return p;
        }
        bool does_preempt() {
            return false;
        }
};

class LCFS : public Scheduler {
    public:
        // initialise quantum to 10000
        LCFS() {
            this->quantum = 10000;
        }
        // add process to front of queue
        void add_process(Process *p) { 
            runQ.push_front(p);
        }
        // get next process from front of queuue
        Process* get_next_process() { 
            Process* p = nullptr;
            if (!runQ.empty()) {
                p = runQ.front();
                runQ.pop_front();
            }
            return p;
        }
        bool does_preempt() {
            return false;
        }
};

class SRTF : public Scheduler {
    public:
        // initialise quantum to 10000
        SRTF() {
            this->quantum = 10000;
        }
        // add process to queue based on remaining cpu time
        void add_process(Process *p) { 
            // find correct position to add process
            list<Process*>::iterator it = runQ.begin();
            while ((it != runQ.end()) && ((*it)->remaining_cpu_time <= p->remaining_cpu_time)) {
                advance(it, 1);
            }
            // add process to runQ at the correct position
            runQ.insert(it, p);
        }
        // get next process from front of queue
        Process* get_next_process() { 
            Process* p = nullptr;
            if (!runQ.empty()) {
                p = runQ.front();
                runQ.pop_front();
            }
            return p;
        }
        bool does_preempt() {
            return false;
        }
};

class RR : public Scheduler {
    public:
        // initialise quantum
        RR(int quantum) {
            this->quantum = quantum;
        }
        // initialise dynamic prio of process
        // add process to back of queue
        void add_process(Process *p) { 
            p->dynamic_prio = p->static_prio-1;
            runQ.push_back(p);
        }
        // get next process from front of queue
        Process* get_next_process() { 
            Process* p = nullptr;
            if (!runQ.empty()) {
                p = runQ.front();
                runQ.pop_front();
            }
            return p;
        }
        bool does_preempt() {
            return false;
        }
};


class PRIO : public Scheduler {
    public:
        // initialise queues
        list<Process*>* activeQ = nullptr;
        list<Process*>* expiredQ = nullptr;

        // initialise quantum
        PRIO(int quantum) {
            this->quantum = quantum;
            activeQ = new list<Process*>[max_prio];
            expiredQ = new list<Process*>[max_prio];
        }
        // add process to correct queue
        void add_process(Process *p) {
            if (p->dynamic_prio < 0) {
                p->dynamic_prio = p->static_prio - 1;
                expiredQ[p->dynamic_prio].push_back(p);
            } else {
                activeQ[p->dynamic_prio].push_back(p);
            }
        }
        // try to get next process from active queue, if not then swap queues and try again
        Process* get_next_process() { 
            Process* p = nullptr;
            int level = max_prio-1;
            while (level >= 0) {
                if (!activeQ[level].empty()) {
                    p = activeQ[level].front();
                    activeQ[level].pop_front();
                    return p;
                }
                level--;
            }
            // Swap queues
            list<Process*>* tempQ = expiredQ;
            expiredQ = activeQ;
            activeQ = tempQ;
            // Try again
            level = max_prio-1;
            while (level >= 0) {
                if (!activeQ[level].empty()) {
                    p = activeQ[level].front();
                    activeQ[level].pop_front();
                    return p;
                }
                level--;
            }
            return p;
        }
        bool does_preempt() {
            return false;
        }
};

class PREPRIO : public Scheduler {
    public:
        // initialise queues
        list<Process*>* activeQ = nullptr;
        list<Process*>* expiredQ = nullptr;

        // initialise quantum
        PREPRIO(int quantum) {
            this->quantum = quantum;
            activeQ = new list<Process*>[max_prio];
            expiredQ = new list<Process*>[max_prio];
        }
        // add process to correct queue
        void add_process(Process *p) {
            if (p->dynamic_prio < 0) {
                p->dynamic_prio = p->static_prio - 1;
                expiredQ[p->dynamic_prio].push_back(p);
            } else {
                activeQ[p->dynamic_prio].push_back(p);
            }
        }
        // try to get next process from active queue, if not then swap queues and try again
        Process* get_next_process() { 
            Process* p = nullptr;
            int level = max_prio-1;
            while (level >= 0) {
                if (!activeQ[level].empty()) {
                    p = activeQ[level].front();
                    activeQ[level].pop_front();
                    return p;
                }
                level--;
            }
            // Swap queues
            list<Process*>* tempQ = expiredQ;
            expiredQ = activeQ;
            activeQ = tempQ;
            // Try again
            level = max_prio-1;
            while (level >= 0) {
                if (!activeQ[level].empty()) {
                    p = activeQ[level].front();
                    activeQ[level].pop_front();
                    return p;
                }
                level--;
            }
            return p;
        }
        bool does_preempt() {
            return true;
        }
};

Scheduler* scheduler = nullptr;
bool CALL_SCHEDULER = false;


// -------------------------------------------------------------------------------------------------------------- //

Process* CURRENT_RUNNING_PROCESS = nullptr;

int Simulation() {
    
    Event* event;
    // while eventQ has events
    while (event = des.get_Event()) {
        // extract relevant event information
        Process* current_process = event->process;
        CURRENT_TIME = event->time_stamp;
        int transition = event->get_transition();
        // calculate duration of time spent in process's previous state for accounting
        int timeInPrevState = CURRENT_TIME - current_process->state_ts;
        // set new state entry time
        current_process->state_ts = CURRENT_TIME;

        if (verbose) {
            cout << CURRENT_TIME << " " << current_process->pid << " " << timeInPrevState << ": " << States[event->old_state] << " -> " << States[event->new_state];
        }

        delete event;
        event = nullptr;
        Event new_event;

        // switch based on transition
        switch (transition) {
            // if process is transitioning to READY state
            case TRANS_TO_READY:

                if (verbose) {
                    cout << " cb=" << current_process->cpu_burst << " rem=" << current_process->remaining_cpu_time << " prio=" << current_process->dynamic_prio << endl;
                }

                // must come from BLOCKED or CREATED
                // add to run queue, no event created

                // reset dynamic priority
                current_process->dynamic_prio = current_process->static_prio - 1;

                // if preemptive priority scheduler then we check if we need to preempt a lower priority current running process
                if (scheduler->does_preempt()) {
                    if  ((CURRENT_RUNNING_PROCESS != nullptr) && (current_process->dynamic_prio > CURRENT_RUNNING_PROCESS->dynamic_prio)) {
                        if (des.remove_event_at_different_time(CURRENT_RUNNING_PROCESS)) {
                            // add new event for preemption
                            new_event = Event(CURRENT_TIME, CURRENT_RUNNING_PROCESS, RUNNING, READY);
                            des.add_Event(new_event);
                        }
                    }
                }
                
                // add process to runQ
                scheduler->add_process(current_process);
                CALL_SCHEDULER = true;
                break;

            // if process is being preempted
            case TRANS_TO_PREEMPT:

                // must come from RUNNING (preemption)
                // add to runqueue (no event is generated)

                // calculate remaining cpu time (total cpu time - timeInPrevState)
                current_process->remaining_cpu_time = current_process->remaining_cpu_time - timeInPrevState;

                // decrement cpu burst
                current_process->cpu_burst = current_process->cpu_burst - timeInPrevState;
                
                if (verbose) {
                    cout << " cb=" << current_process->cpu_burst << " rem=" << current_process->remaining_cpu_time << " prio=" << current_process->dynamic_prio << endl;
                }

                // decrement dynamic priority
                current_process->dynamic_prio--;

                // add process to runQ
                scheduler->add_process(current_process);
                CURRENT_RUNNING_PROCESS = nullptr;
                CALL_SCHEDULER = true;
                break;

            // if process is transitioning to RUNNING state
            case TRANS_TO_RUN:

                // create event for either preemption or blocking

                if (verbose) {
                    cout << " cb=" << current_process->cpu_burst << " rem=" << current_process->remaining_cpu_time << " prio=" << current_process->dynamic_prio << endl;
                }

                // decide whether to add event for blocking or preemption
                if (current_process->cpu_burst <= scheduler->quantum) {
                    new_event = Event(CURRENT_TIME + current_process->cpu_burst, current_process, RUNNING, BLOCKED); // event for blocking
                } else {
                    new_event = Event(CURRENT_TIME + scheduler->quantum, current_process, RUNNING, READY); // event for preemption
                }

                // update cpu waiting time
                current_process->cpu_waiting_time = current_process->cpu_waiting_time + timeInPrevState;

                // add event to eventQ
                des.add_Event(new_event);
                break;

            // if process is transitioning to BLOCKED state
            case TRANS_TO_BLOCK:

                // create event for when process becomes READY again

                // calculate remaining cpu time (total cpu time - timeInPrevState)
                current_process->remaining_cpu_time = current_process->remaining_cpu_time - timeInPrevState;
                current_process->cpu_burst = current_process->cpu_burst - timeInPrevState;

                // if remaining cpu time is 0, then process terminates
                if (current_process->remaining_cpu_time == 0) {
                    // set finishing time
                    current_process->finishing_time = CURRENT_TIME;
                    // calculate turnaround time
                    current_process->turnaround_time = current_process->finishing_time - current_process->arrival_time;
                    if (verbose) {
                        cout << " Done" << endl;
                    }
                } else {
                    // otherwise we create event for BLOCKING to READY
                    current_process->set_io_burst();
                    // keep track of number of processes currently performing IO
                    num_performing_IO++;
                    if (num_performing_IO == 1) {
                        // if at least one process performing IO then measure IO time
                        time_of_IO_start = CURRENT_TIME;
                    }

                    if (verbose) {
                        cout << " ib=" << current_process->io_burst << " rem=" << current_process->remaining_cpu_time << endl;
                    }

                    // update process's total IO time
                    current_process->total_io_time = current_process->total_io_time + current_process->io_burst;
                    // add event for BLOCKING to READY transition
                    new_event = Event(CURRENT_TIME + current_process->io_burst, current_process, BLOCKED, READY);
                    des.add_Event(new_event);
                }

                // no current running process
                CURRENT_RUNNING_PROCESS = nullptr;
                CALL_SCHEDULER = true;
                break;
        }

        // if we need to schedule a new process to run
        if (CALL_SCHEDULER) {
            // process all events at current time stamp at once
            if (des.get_next_event_time() == CURRENT_TIME) {
                continue;
            }
            CALL_SCHEDULER = false;
            
            if (CURRENT_RUNNING_PROCESS == nullptr) {
                // get next process from scheduler
                CURRENT_RUNNING_PROCESS = scheduler->get_next_process();
                // if no processes to schedule then continue
                if (CURRENT_RUNNING_PROCESS == nullptr) {
                    continue;
                }
                
                // update process's cpu burst
                if (CURRENT_RUNNING_PROCESS->cpu_burst == 0) {
                    CURRENT_RUNNING_PROCESS->set_cpu_burst();
                    if (CURRENT_RUNNING_PROCESS->cpu_burst > CURRENT_RUNNING_PROCESS->remaining_cpu_time) {
                        CURRENT_RUNNING_PROCESS->cpu_burst = CURRENT_RUNNING_PROCESS->remaining_cpu_time;
                    }
                }

                // add event to start running
                Event event = Event(CURRENT_TIME, CURRENT_RUNNING_PROCESS, READY, RUNNING);
                des.add_Event(event);                
            }
        }
    }

    // at end of simulation, return finishing time of last process
    return CURRENT_TIME;
}

// -------------------------------------------------------------------------------------------------------------- //

int myrandom(int burst) {
    // returns number between 1 and burst
    int randval = randvals[ofs];
    ofs++;
    if (ofs > num_random_numbers-1) {
        ofs = 0;
    }
    return 1 + (randval % burst);
}


int main(int argc, char* argv[]) {

    bool got_s = false;
    int quantum;
    max_prio = 4;
    int c;
    string scheduler_name;
    char scheduler_symbol;
    // read flags
    while ((c = getopt (argc, argv, "vs:")) != -1) {
        switch (c) {
            case 'v':
                // enable verbose output
                verbose = true;
                break;
            case 's':
                // scheduler specified
                got_s = true;
                scheduler_symbol = optarg[0];
                // use specified scheduler
                switch (scheduler_symbol) {
                    case 'F':
                        scheduler = new FCFS();
                        scheduler_name = "FCFS";
                        break;
                    case 'L':
                        scheduler = new LCFS();
                        scheduler_name = "LCFS";
                        break;
                    case 'S':
                        scheduler = new SRTF();
                        scheduler_name = "SRTF";
                        break;
                    case 'R':
                        // extract quantum and max priority
                        sscanf(optarg + 1, "%d:%d", &quantum, &max_prio);
                        scheduler = new RR(quantum);
                        scheduler_name = "RR " + to_string(quantum);
                        // overwrite max priority to 4
                        max_prio = 4;
                        break;
                    case 'P':
                        // extract quantum and max priority
                        sscanf(optarg + 1, "%d:%d", &quantum, &max_prio);
                        scheduler = new PRIO(quantum);
                        scheduler_name = "PRIO " + to_string(quantum);
                        break;
                    case 'E':
                        // extract quantum and max priority
                        sscanf(optarg + 1, "%d:%d", &quantum, &max_prio);
                        scheduler = new PREPRIO(quantum);
                        scheduler_name = "PREPRIO " + to_string(quantum);
                        break;
                    default:
                        // return error message on unknown value
                        cout << "Unknown Scheduler spec: -s {FLSRPE}" << endl;
                        return 1;
                }
                break;
            case '?':
                // return error message on unknown flag
                cout << "Usage: ./sched [-v] [-s sched] input randomfile" << endl;
                cout << "   -v enables verbose" << endl;
                cout << "   -s specifies scheduler and params" << endl;
                return 1;
            
        }
    }
    // if no scheduler specified then use FCFS
    if (!got_s) {
        scheduler = new FCFS();
        scheduler_name = "FCFS";
    }
    
    // open input file
    ifstream input_file(argv[optind]);
    if (!input_file) {
        cerr << "Error: failed to open input file " << argv[optind] << endl;
        return 1;
    }

    string line;
    int pid = 0;
    // read each line of input file and create process objects
    while (getline(input_file, line)) {
        stringstream ss(line);
        int arrival_time, total_cpu_time, max_cpu_burst, max_io_burst;
        if (ss >> ws >> arrival_time >> ws >> total_cpu_time >> ws >> max_cpu_burst >> ws >> max_io_burst) {
            Process process = Process(pid, arrival_time, total_cpu_time, max_cpu_burst, max_io_burst);
            processes.push_back(process);
            pid++;
        } else {
            cerr << "Error input file format line " << pid << endl;
            return 1;
        }
    }

    // open random numbers file
    ifstream rand_file(argv[optind+1]);
    if (!rand_file) {
        cerr << "Error: failed to open rfile " << argv[optind+1] << endl;
        return 1;
    }

    // read first integer in file as size of list
    rand_file >> num_random_numbers;

    // read remaining random numbers
    int integer;
    while (rand_file >> integer) {
        randvals.push_back(integer);
    }
    
    // set static priorities  for processes
    for (int i = 0; i < processes.size(); i++) {
        processes[i].static_prio = myrandom(max_prio);
        processes[i].pid = i;
        // create arrival events
        des.add_Event(Event(processes[i].arrival_time, &processes[i], CREATED, READY));
    }

    // run simulation, catch finishing time of last process
    int final_time = Simulation();

    // calculate simulation metrics and generate output
    int total_cpubusy = 0;
    int total_turnaround = 0;
    int total_cpu_waiting = 0;

    cout << scheduler_name << endl;
    for (const auto process : processes) {
        // print process metrics
        printf("%04d: %4d %4d %4d %4d %1d | %5d %5d %5d %5d\n", process.pid, process.arrival_time, process.total_cpu_time, process.max_cpu_burst, process.max_io_burst, process.static_prio, process.finishing_time, process.turnaround_time, process.total_io_time, process.cpu_waiting_time);
        total_cpubusy = total_cpubusy + process.total_cpu_time;
        total_turnaround = total_turnaround + process.turnaround_time;
        total_cpu_waiting = total_cpu_waiting + process.cpu_waiting_time;
    }

    double cpu_util = 100.0*(total_cpubusy/(double) final_time);
    double io_util = 100.0*(total_IO_time/(double) final_time);
    double throughput = 100.0*(processes.size()/(double) final_time);
    double avg_turnaround = ((double) total_turnaround)/((double) processes.size());
    double avg_cpu_waiting = ((double) total_cpu_waiting)/((double) processes.size());

    // print aggregate metrics
    printf("SUM: %d %.2lf %.2lf %.2lf %.2lf %.3lf\n", final_time, cpu_util, io_util, avg_turnaround, avg_cpu_waiting, throughput);

    return 0;
}