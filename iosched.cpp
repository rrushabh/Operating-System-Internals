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

long num_requests = 0;
long current_track_num = 0;

long current_time = 0; // this gives us total_time at the end of simulation
long tot_movement = 0;
long time_iobusy = 0; // converted to io_utilization ratio when outputting
long tot_turnaround = 0; // converted to avg_turnaround when outputting
long tot_waittime = 0; // converted to avg_waittime when outputting
long max_waittime = 0;
char algo_symbol;

// struct to hold a single IO request
struct IO_request {
    long request_id;
    long time_step;
    long track_num;

    // default constructor
    IO_request() : request_id(-1), time_step(-1), track_num(-1) {}

    IO_request(long request_id, long time_step, long track_num) {
        this->request_id = request_id;
        this->time_step = time_step;
        this->track_num = track_num;
    }
};

// pointer to active request
IO_request* active_request;

// struct to hold info on IO request (for output)
struct IO_info {
    long request_id;
    long arrival_time;
    long start_time;
    long end_time;

    IO_info() : request_id(-1), arrival_time(-1), start_time(-1), end_time(-1) {}

    IO_info(long request_id, long arrival_time, long start_time, long end_time) {
        this->request_id = request_id;
        this->arrival_time = arrival_time;
        this->start_time = start_time;
        this->end_time = end_time;
    }
};

// declare IO request queues
deque<IO_request*> IO_queue;
deque<IO_request*> add_queue;
deque<IO_request*> requests;
vector<IO_info*> infos;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// create Scheduler interface, from which specific scheduler algorithms are derived
class Scheduler {
    public:
        virtual IO_request* fetch() = 0; // virtual base class
        virtual void add_request() = 0;
};

// Derived classes with different function implementations to override the virtual functions in Scheduler
class FIFO : public Scheduler {
    public:
        
        FIFO() {}

        void add_request() {
            IO_request* new_request = requests.front();
            requests.pop_front();
            IO_queue.push_back(new_request);
        }
        
        // return next IO request
        IO_request* fetch() { 
            IO_request* request = IO_queue.front();
            IO_queue.pop_front();
            return request;
        }
};

class SSTF : public Scheduler {
    public:
        
        SSTF() {}

        void add_request() {
            IO_request* new_request = requests.front();
            requests.pop_front();
            IO_queue.push_back(new_request);
        }
        
        // return next IO request
        IO_request* fetch() { 
            // iterate through IO queue, find request with shortest seek time
            auto min_it = IO_queue.begin();
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                if (abs(current_track_num - (*it)->track_num) < abs(current_track_num - (*min_it)->track_num)) {
                    min_it = it;
                }
            }
            IO_request* request = (*min_it);
            IO_queue.erase(min_it);
            return request;
        }
};

class LOOK : public Scheduler {
    public:
        int direction;
        
        LOOK() {
            this->direction = 1;
        }

        void add_request() {
            IO_request* new_request = requests.front();
            requests.pop_front();
            IO_queue.push_back(new_request);
        }
        
        // return next IO request
        IO_request* fetch() {
            // iterate through IO queue, find request with shortest seek time in correct direction
            auto min_it = IO_queue.begin();
            bool found = false;
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                // check direction
                if ((current_track_num == (*it)->track_num) || ((current_track_num < (*it)->track_num) == direction)) {
                    if (!found || abs(current_track_num - (*it)->track_num) < abs(current_track_num - (*min_it)->track_num)) {
                        min_it = it;
                        found = true;


                    }
                }
            }
            IO_request* request = nullptr;
            if (found) {
                request = (*min_it);
                IO_queue.erase(min_it);
                return request;
            }
            // if not found then switch direction
            direction = (direction + 1) % 2;
            min_it = IO_queue.begin();
            // iterate again
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                if ((current_track_num == (*it)->track_num) || ((current_track_num < (*it)->track_num) == direction)) {
                    if (abs(current_track_num - (*it)->track_num) < abs(current_track_num - (*min_it)->track_num)) {
                        min_it = it;

                    }
                }
            }
            request = (*min_it);
            IO_queue.erase(min_it);
            return request;
        }
};

class CLOOK : public Scheduler {
    public:
        
        CLOOK() {}

        void add_request() {
            IO_request* new_request = requests.front();
            requests.pop_front();
            IO_queue.push_back(new_request);
        }
        
        // return next IO request
        IO_request* fetch() {
            // iterate through IO queue, find request with shortest seek time with track num higher than current
            auto min_it = IO_queue.begin();
            bool found = false;
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                // check track num
                if ((current_track_num == (*it)->track_num) || (current_track_num < (*it)->track_num)) {
                    if (!found || abs(current_track_num - (*it)->track_num) < abs(current_track_num - (*min_it)->track_num)) {
                        min_it = it;
                        found = true;


                    }
                }
            }
            IO_request* request = nullptr;
            if (found) {
                request = (*min_it);
                IO_queue.erase(min_it);
                return request;
            }
            // if not found then return to the start (track_num = 0)
            min_it = IO_queue.begin();
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                if (abs(0 - (*it)->track_num) < abs(0 - (*min_it)->track_num)) {
                    min_it = it;

                }
            }
            request = (*min_it);
            IO_queue.erase(min_it);
            return request;
        }
};

class FLOOK : public Scheduler {
    public:
        int direction;
        
        FLOOK() {
            this->direction = 1;
        }

        void add_request() {
            IO_request* new_request = requests.front();
            requests.pop_front();
            add_queue.push_back(new_request);
        }
        
        // return next IO request
        IO_request* fetch() {
            // if IO queue is empty then swap the queues
            if (IO_queue.empty()) {
                IO_queue.swap(add_queue);
            }
            // iterate through IO queue, find request with shortest seek time in correct direction
            auto min_it = IO_queue.begin();
            bool found = false;
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                if ((current_track_num == (*it)->track_num) || ((current_track_num < (*it)->track_num) == direction)) {
                    if (!found || abs(current_track_num - (*it)->track_num) < abs(current_track_num - (*min_it)->track_num)) {
                        min_it = it;
                        found = true;


                    }
                }
            }
            IO_request* request = nullptr;
            if (found) {
                request = (*min_it);
                IO_queue.erase(min_it);
                return request;
            }
            // if not found then switch direction
            direction = (direction + 1) % 2;
            // iterate again
            min_it = IO_queue.begin();
            for (auto it = IO_queue.begin(); it != IO_queue.end(); advance(it, 1)) {
                if ((current_track_num == (*it)->track_num) || ((current_track_num < (*it)->track_num) == direction)) {
                    if (abs(current_track_num - (*it)->track_num) < abs(current_track_num - (*min_it)->track_num)) {
                        min_it = it;

                    }
                }
            }
            request = (*min_it);
            IO_queue.erase(min_it);
            return request;
        }
};

Scheduler* scheduler;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int simulation() {
    while (true) {
        // add new request to IO queue if arrived
        if (!requests.empty()) {
            if (current_time == requests.front()->time_step) {
                scheduler->add_request();
            }
        }

        // complete active request if finished
        if (active_request && current_track_num == active_request->track_num) {
            // request is complete
            infos[active_request->request_id]->end_time = current_time;
            tot_turnaround = tot_turnaround + (current_time - active_request->time_step);
            num_requests++;
            active_request = nullptr;
        }

        // schedule next request if no active request
        if (!active_request) {
            if (requests.empty() && IO_queue.empty()) {
                if (add_queue.empty()) {
                // exit simulation
                    return 1;
                }
            }
            // if there are requests waiting to be scheduled then fetch next request from scheduler
            if (!IO_queue.empty() || !add_queue.empty()) {
                active_request = scheduler->fetch();
                infos[active_request->request_id]->start_time = current_time;
                long new_waittime = current_time - active_request->time_step;
                tot_waittime = tot_waittime + new_waittime;
                max_waittime = max(max_waittime, new_waittime);
            }
        }
        // if we have an active request then move head towards its track number
        if (active_request != nullptr) {
            if (active_request->track_num == current_track_num) {
                // edge case
                infos[active_request->request_id]->end_time = current_time;
                tot_turnaround = tot_turnaround + (current_time - active_request->time_step);
                num_requests++;
                active_request = nullptr;
                continue;
            } else {
                time_iobusy++;
                // move head in the direction of the requested track
                if (active_request->track_num > current_track_num) {
                    current_track_num++;
                } else {
                    current_track_num--;
                }
                tot_movement++;
            }
        }
        current_time++;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char* argv[]) {

    bool got_algo = false;
    int c;
    // read flags
    while ((c = getopt(argc, argv, "s:")) != -1) {
        switch (c) {
            case 's':
                // algorithm specified
                got_algo = true;
                algo_symbol = optarg[0];
                // use specified algorithm
                switch (algo_symbol) {
                    case 'N':
                        scheduler = new FIFO();
                        break;
                    case 'S':
                        scheduler = new SSTF();
                        break;
                    case 'L':
                        scheduler = new LOOK();
                        break;
                    case 'C':
                        scheduler = new CLOOK();
                        break;
                    case 'F':
                        scheduler = new FLOOK();
                        break;
                    default:
                        // return error message on unknown value
                        printf("Unknown Algorithm spec: -s{NSLCF}\n");
                        return 1;
                }
                break;
            case '?':
                printf("Usage: ./iosched -s ALGO inputfile \n");
                printf("   -s specifies IO scheduling algorithm\n");
                return 1;
            
        }
    }
    // if no algorithm specified then use FIFO
    if (!got_algo) {
        scheduler = new FIFO();
    }

    // open input file
    ifstream input_file(argv[optind]);
    if (!input_file) {
        cerr << "Error: failed to open input file " << argv[optind+1] << endl;
        return 1;
    }

    string line;
    long request_id = 0;

    // skip # lines
    while (input_file.peek() != EOF) {
        do {
            getline(input_file, line);
        } while (line[0] == '#');

        if (line.empty()) {
            continue;
        }

        long time_step, track_num;
        stringstream ss(line);
        ss >> time_step >> track_num;
        // generate IO_request objects and populate requests vector
        IO_request* new_request = new IO_request(request_id, time_step, track_num);
        IO_info* new_info = new IO_info(request_id, time_step, -1, -1);
        requests.push_back(new_request);
        infos.push_back(new_info);
        request_id++;
    }

    // run simulation
    simulation();

    // print output
    for (auto it = infos.begin(); it != infos.end(); advance(it, 1)) {
        printf("%5ld: %5ld %5ld %5ld\n", (*it)->request_id, (*it)->arrival_time, (*it)->start_time, (*it)->end_time);
    }
    
    double avg_turnaround = tot_turnaround / double(num_requests);
    double avg_waittime = tot_waittime / double(num_requests);
    double io_utilization = time_iobusy / double(current_time);

    printf("SUM: %ld %ld %.4lf %.2lf %.2lf %ld\n",
        current_time, tot_movement, io_utilization,
        avg_turnaround, avg_waittime, max_waittime);

    return 0;
}