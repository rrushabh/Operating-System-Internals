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

// exactly 64 virtual pages
#define NUM_VPAGES 64

// declare global variables
int MAX_FRAMES;
int num_random_numbers;
int ofs = 0;
vector<int> randvals;
ifstream input_file;
unsigned long inst_count = 0;
int ctx_switches = 0;
int process_exits = 0;
unsigned long long cost = 0;

struct PTE { // 32 bit structure
    unsigned int VALID:1; // 1 if entry is valid, otherwise translation is invalid
    unsigned int REFERENCED:1; // set to 1 every time there is a successful load or store
    unsigned int MODIFIED:1; // set to 1 every time there is a successful store
    unsigned int WRITE_PROTECT:1; // if 1 then only loads are allowed - store raises a write protection exception
    unsigned int PAGEDOUT:1;
    unsigned int frame_num:7; // because max frame_num = 128
    // 32 - 12 = 20 bits available for other information
    unsigned int VMA_SEARCHED:1;
    unsigned int IN_VMA:1;
    unsigned int FILE_MAPPED:1;
    unsigned int ZEROED:1;


    // default constructor
    PTE() : VALID(0), REFERENCED(0), MODIFIED(0), WRITE_PROTECT(0), PAGEDOUT(0), frame_num(0), VMA_SEARCHED(0), IN_VMA(0), FILE_MAPPED(0), ZEROED(0) {}

};

struct VMA {
    unsigned int start:6;
    unsigned int end:6;
    unsigned int WRITE_PROTECTED:1;
    unsigned int FILE_MAPPED:1;

    // default constructor
    VMA() : start(0), end(0), WRITE_PROTECTED(0), FILE_MAPPED(0) {}

    VMA(int start, int end, int WRITE_PROTECTED, int FILE_MAPPED) {
        this->start = start;
        this->end = end;
        this->WRITE_PROTECTED = WRITE_PROTECTED;
        this->FILE_MAPPED = FILE_MAPPED;
    }
};

// structure to store process output information
struct pstat {
    int pid;
    int unmaps;
    int maps;
    int ins;
    int outs;
    int fins;
    int fouts;
    int zeros;
    int segv;
    int segprot;

    pstat(int pid) {
        this->pid = pid;
        this->unmaps = 0;
        this->maps = 0;
        this->ins = 0;
        this->outs = 0;
        this->fins = 0;
        this->fouts = 0;
        this->zeros = 0;
        this->segv = 0;
        this->segprot = 0;
    }
};

vector<pstat> pstats;

// Process object
struct Process {
    int pid;
    vector<VMA> address_space;
    PTE page_table[NUM_VPAGES];

    // default constructor
    Process() : pid(-1) {}

    Process(int pid) {
        this->pid = pid;
    }
};

// pointer to current process
Process* current_process;

// Frame table entry object
struct FTE {
    int frame_num;
    int process_id;
    int vpage;
    unsigned long age;
    int time_of_last_use;

    // default constructor
    FTE() : frame_num(-1), process_id(-1), vpage(-1), age(0), time_of_last_use(-1) {}
};

// declare free list, frame table and processes vector
deque<int> free_list;
FTE* frame_table;
vector<Process*> processes;

// create pager interface, from which specific pager algorithms are derived
class Pager {
    public:
        virtual FTE* select_victim_frame() = 0; // virtual base class
        virtual bool reset_age() = 0; // true for aging, false otherwise
};

// -------------------------------------------------------------------------------------------------------------- //

// Derived classes with different function implementations to override the virtual functions in Pager
class FIFO : public Pager {
    public:
        int HAND;
        
        FIFO() {
            this->HAND = 0; // 0 to MAX_FRAMES
        }
        
        // return victim frame, next frame that hand is pointing to
        FTE* select_victim_frame() { 
            FTE* frame = &frame_table[HAND];
            HAND = (HAND + 1) % MAX_FRAMES;
            return frame;
        }

        bool reset_age() {
            return false;
        }
};

class Random : public Pager {
    public:
                
        // return victim frame at random
        FTE* select_victim_frame() {
            int randval = randvals[ofs];
            ofs = (ofs + 1) % num_random_numbers;
            int random_num = (randval % MAX_FRAMES);
            FTE* frame = &frame_table[random_num];
            return frame;
        }

        bool reset_age() {
            return false;
        }
};

class Clock : public Pager {
    public:
        int HAND;
        
        Clock() {
            this->HAND = 0; // 0 to MAX_FRAMES
        }
        
        // return victim frame following clock algorithm, if referenced bit of vpage is not set
        FTE* select_victim_frame() { 
            FTE* frame = &frame_table[HAND];
            while (processes[frame->process_id]->page_table[frame->vpage].REFERENCED != 0) {
                processes[frame->process_id]->page_table[frame->vpage].REFERENCED = 0;
                HAND = (HAND + 1) % MAX_FRAMES;
                frame = &frame_table[HAND];
            }
            HAND = (HAND + 1) % MAX_FRAMES;
            return frame;
        }

        bool reset_age() {
            return false;
        }
};

class EnhancedSecondChance : public Pager {
    public:
        int HAND;
        unsigned long last_reset_time;
        int classes[4];
        
        EnhancedSecondChance() {
            this->HAND = 0; // 0 to MAX_FRAMES
            this->last_reset_time = 0;
        }
        
        // return victim frame following NRU algorithm
        FTE* select_victim_frame() { 
            // initialise classes
            for (int i = 0; i < 4; i++) {
                classes[i] = -1;
            }

            // track whether we need to reset referenced bits or not (if 50 or more instr have passed)
            bool reset = (inst_count - this->last_reset_time >= 50);
            if (reset) {
                this->last_reset_time = inst_count;
            }
            int lowest_class = 4;
            
            FTE* frame = &frame_table[HAND];
            for (int i=0; i < MAX_FRAMES; i++) {
                int frame_class = 2*(processes[frame->process_id]->page_table[frame->vpage].REFERENCED) + processes[frame->process_id]->page_table[frame->vpage].MODIFIED;
                // if we are not resetting and class = 0 then we can return straight away
                if (!reset && (frame_class == 0)) {
                    HAND = (HAND + 1) % MAX_FRAMES;
                    return frame;
                }
                // allocate frame to class if class is empty
                if (classes[frame_class] == -1) {
                    lowest_class = min(lowest_class, frame_class);
                    classes[frame_class] = HAND;
                }
                // reset referenced bits if required
                if (reset) {
                    processes[frame->process_id]->page_table[frame->vpage].REFERENCED = 0;
                }
                HAND = (HAND + 1) % MAX_FRAMES;
                frame = &frame_table[HAND];
            }
            // return frame from lowest class
            frame = &frame_table[classes[lowest_class]];
            HAND = (classes[lowest_class] + 1) % MAX_FRAMES;
            return frame;
        }

        bool reset_age() {
            return false;
        }
};

class Aging : public Pager {
    public:
        int HAND;
        
        Aging() {
            this->HAND = 0; // 0 to MAX_FRAMES
        }
        
        FTE* select_victim_frame() {
            int victim_frame_idx;
            unsigned long lowest_counter = UINT32_MAX;
            FTE* frame = &frame_table[HAND];
            for (int i=0; i < MAX_FRAMES; i++) {
                // increment age of frame
                frame->age = frame->age >> 1;
                // if referenced then set first bit
                if (processes[frame->process_id]->page_table[frame->vpage].REFERENCED == 1) {
                    frame->age = (frame->age | 0x80000000);
                    processes[frame->process_id]->page_table[frame->vpage].REFERENCED = 0;
                }
                // update lowest counter
                if (frame->age < lowest_counter) {
                    victim_frame_idx = HAND;
                    lowest_counter = frame->age;
                }
                HAND = (HAND + 1) % MAX_FRAMES;
                frame = &frame_table[HAND];
            }
            // return frame with lowest counter
            HAND = (victim_frame_idx + 1) % MAX_FRAMES;
            return &frame_table[victim_frame_idx];
        }

        bool reset_age() {
            return true;
        }
};

class WorkingSet : public Pager {
    public:
        int HAND;
        
        WorkingSet() {
            this->HAND = 0; // 0 to MAX_FRAMES
        }
        
        FTE* select_victim_frame() { 
            FTE* frame = &frame_table[HAND];
            int victim_frame_idx = HAND;
            unsigned int smallest_time = UINT32_MAX;
            for (int i=0; i < MAX_FRAMES; i++) {
                // if this vpage has been referenced then we update the frame's time of last use
                if (processes[frame->process_id]->page_table[frame->vpage].REFERENCED == 1) {
                    frame->time_of_last_use = inst_count;
                    processes[frame->process_id]->page_table[frame->vpage].REFERENCED = 0;
                } else {
                    // return frame if age > 50
                    if (inst_count - frame->time_of_last_use >= 50) {
                        HAND = (HAND + 1) % MAX_FRAMES;
                        return frame;
                    } else {
                        // update smallest time
                        if (frame->time_of_last_use < smallest_time) {
                            smallest_time = frame->time_of_last_use;
                            victim_frame_idx = HAND;
                        }
                    }
                }
                HAND = (HAND + 1) % MAX_FRAMES;
                frame = &frame_table[HAND];
            }
            HAND = (victim_frame_idx + 1) % MAX_FRAMES;
            return &frame_table[victim_frame_idx];
        }

        bool reset_age() {
            return false;
        }
};

Pager* pager;

bool in_vma(int page_num, Process* process) {
    // function to check whether vpage corresponds to a vma in this process.
    // usage: if (in_vma(page_num, process)) then

    // iterate through process's address space
    for (int i = 0; i < process->address_space.size(); i++) {
        if (process->address_space[i].start <= page_num && page_num <= process->address_space[i].end) {
            // set vpage bits as necessary
            process->page_table[page_num].FILE_MAPPED = process->address_space[i].FILE_MAPPED;
            process->page_table[page_num].WRITE_PROTECT = process->address_space[i].WRITE_PROTECTED;
            return true;
        }
    }
    return false;
}

FTE* allocate_frame_from_free_list() {
    // return next frame from free list
    if (free_list.empty()) {
        return nullptr;
    }
    int frame_index = free_list.front();
    free_list.pop_front();
    FTE* frame = &frame_table[frame_index];
    return frame;
}

FTE* get_frame() {
    // get next frame, either from free list or using pager algorithm
    FTE* frame = allocate_frame_from_free_list();
    if (frame == NULL) {
        frame = pager->select_victim_frame();
    }
    return frame;
}

bool get_next_instruction(char &operation, int &vpage) {
    // get next instruction from the input file
    string line;
    // skip lines starting with #
    do {
        getline(input_file, line);
    } while (line[0] == '#');

    // parse line, using call by reference semantics to update values of operation and vpage
    if (!input_file.eof()) {
        stringstream ss(line);
        ss >> operation >> vpage;
        return true;
    }
    return false;
}

void unmap_frame(FTE* frame, bool exiting) {
    // function to unmap a frame
    printf(" UNMAP %d:%d\n", frame->process_id, frame->vpage);
    cost = cost + 410;
    Process* process = processes[frame->process_id];
    pstats[process->pid].unmaps++;
    PTE* pte = &process->page_table[frame->vpage];
    if (pte->MODIFIED) {
        if (pte->FILE_MAPPED) {
            printf(" FOUT\n");
            cost = cost + 2800;
            pstats[process->pid].fouts++;
        } else {
            if (!exiting) {
                printf(" OUT\n");
                cost = cost + 2750;
                pstats[process->pid].outs++;
                pte->PAGEDOUT = 1;
            }
        }
    }
    // reset bits of page table entry
    pte->VALID = 0;
    pte->REFERENCED = 0;
    pte->MODIFIED = 0;
    pte->frame_num = 0;

    // reset frame
    frame->process_id = -1;
    frame->vpage = -1;

    // if this is an exit instruction then we return this frame to the free list
    if (exiting) {
        free_list.push_back(frame->frame_num);
    }
}

void map_frame(FTE* frame, PTE* current_pte, int vpage) {
    // function to map a frame to a vpage
    frame->process_id = current_process->pid;
    frame->vpage = vpage;
    frame->time_of_last_use = inst_count;
    if (current_pte->FILE_MAPPED) {
        printf(" FIN\n");
            cost = cost + 2350;
            pstats[current_process->pid].fins++;
    } else {
        if (current_pte->PAGEDOUT) {
            printf(" IN\n");
            cost = cost + 3200;
            pstats[current_process->pid].ins++;
        } else {
            printf(" ZERO\n");
            cost = cost + 150;
            pstats[current_process->pid].zeros++;
        }
    }
    // set page table entry bits
    current_pte->VALID = 1;
    current_pte->frame_num = frame->frame_num;
    // reset age of frame if we are using aging
    if (pager->reset_age()) {
        frame->age = 0;
    }
    printf(" MAP %d\n", frame->frame_num);
    cost = cost + 350;
    pstats[current_process->pid].maps++;
}

void update_pte(char &operation, PTE* current_pte) {
    // update pte if instruction is write or read
    // always set referenced bit
    current_pte->REFERENCED = 1;
    if (operation == 'w') {
        // if write then check pte's write protect bit
        if (current_pte->WRITE_PROTECT) {
            // cout << " SEGPROT" << endl;
            printf(" SEGPROT\n");
            cost = cost + 410;
            pstats[current_process->pid].segprot++;
        } else {
            // set modified bit
            current_pte->MODIFIED = 1;
        }
    }
}

int instruction_num;

void simulation() {
    // simulation function
    char operation;
    int vpage;
    instruction_num = 0;
    while (get_next_instruction(operation, vpage)) {
        // keep getting new instructions from the file
        // increment instruction count
        inst_count++;
        printf("%d: ==> %c %d\n", instruction_num, operation, vpage);
        instruction_num++;
        // condition on instruction
        if (operation == 'c') {
            // if context switch then set current process
            ctx_switches++;
            cost = cost + 130;
            current_process = processes[vpage];
        } else if (operation == 'e') {
            // if process exit then reset ptes of this process and unmap frames as required
            printf("EXIT current process %d\n", current_process->pid);
            process_exits++;
            cost = cost + 1230;
            for (int i=0; i < NUM_VPAGES; i++) {
                PTE* pte = &current_process->page_table[i];
                if (pte->VALID) {
                    unmap_frame(&frame_table[pte->frame_num], true);
                }
                pte->FILE_MAPPED = 0;
                pte->PAGEDOUT = 0;
                pte->IN_VMA = 0;
                pte->VMA_SEARCHED = 0;
            }
            current_process = nullptr;
            continue;
        } else {
            // if read or write instruction
            cost = cost + 1;
            PTE* current_pte = &current_process->page_table[vpage];
            // first check if page table entry is valid. if not then we throw page fault exception and can enter kernel mode
            if (!current_pte->VALID) {
                // generate page fault exception
                if (!current_pte->VMA_SEARCHED) {
                    // search for pte in process's address space
                    current_pte->IN_VMA = in_vma(vpage, current_process);
                    current_pte->VMA_SEARCHED = 1;
                }
                if (current_pte->IN_VMA) {
                    // allocate frame to this pte and map
                    FTE* new_frame = get_frame();
                    if (new_frame->process_id != -1) {
                        unmap_frame(new_frame, false);
                    }
                    map_frame(new_frame, current_pte, vpage);
                } else {
                    // if pte is not in address space then generate SEGV output
                    printf(" SEGV\n");
                    cost = cost + 440;
                    pstats[current_process->pid].segv++;
                    continue;
                }
            }
            // update bits of page table entry as required
            update_pte(operation, current_pte);
        }
    }
}

int main(int argc, char* argv[]) {

    bool got_algo = false;
    int c;
    char options;
    char algo_symbol;
    bool O = false;
    bool P = false;
    bool F = false;
    bool S = false;
    // read flags
    while ((c = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (c) {
            case 'f':
                // num frames
                sscanf(optarg, "%d", &MAX_FRAMES);
                break;
            case 'a':
                // algorithm specified
                got_algo = true;
                algo_symbol = optarg[0];
                // use specified algorithm
                switch (algo_symbol) {
                    case 'f':
                        pager = new FIFO();
                        break;
                    case 'r':
                        pager = new Random();
                        break;
                    case 'c':
                        pager = new Clock();
                        break;
                    case 'e':
                        pager = new EnhancedSecondChance();
                        break;
                    case 'a':
                        pager = new Aging();
                        break;
                    case 'w':
                        pager = new WorkingSet();
                        break;
                    default:
                        // return error message on unknown value
                        // cout << "Unknown Algorithm spec: -a{FRCEAW}" << endl;
                        printf("Unknown Algorithm spec: -a{FRCEAW}\n");
                        return 1;
                }
                break;
            case 'o':
                // set output bits based on arguments
                for (int i=0; i < strlen(optarg); i++) {
                    if (optarg[i] == 'O') {
                        O = true;
                    } else if (optarg[i] == 'P') {
                        P = true;
                    } else if (optarg[i] == 'F') {
                        F = true;
                    } else if (optarg[i] == 'S') {
                        S = true;
                    }
                }
                break;
            case '?':
                printf("Usage: ./mmu -f MAX_FRAMES -a ALGO input randomfile\n");
                printf("   -f specifies number of frames\n");
                printf("   -a specifies paging algorithm\n");
                return 1;
            
        }
    }
    // if no algorithm specified then use FIFO
    if (!got_algo) {
        pager = new FIFO();
    }
    
    // initialise frame table
    frame_table = new FTE[MAX_FRAMES];

    // initialise free list
    for (int i=0; i < MAX_FRAMES; i++) {
        frame_table[i].frame_num = i;
        free_list.push_back(i);
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
    
    // open input file
    input_file.open(argv[optind]);
    if (!input_file) {
        cerr << "Error: failed to open input file " << argv[optind] << endl;
        return 1;
    }

    string line;
    int num_processes, num_vmas;

    // skip lines that start with #
    do {
        getline(input_file, line);
    } while (line[0] == '#');

    num_processes = atoi(&line[0]);

    // loop over each process
    for (int i = 0; i < num_processes; i++) {
        // read the number of VMAs for this process
        do {
            getline(input_file, line);
        } while (line[0] == '#');
        num_vmas = atoi(&line[0]);

        // create a new Process object and add it to the vector
        Process* process = new Process(i);
        pstat p_stat = pstat(i);

        // loop over each VMA for this process
        for (int j = 0; j < num_vmas; j++) {
            // read the VMA data from the input file
            int start, end, write_protected, file_mapped;
            do {
                getline(input_file, line);
            } while (line[0] == '#');
            stringstream ss(line);
            ss >> start >> end >> write_protected >> file_mapped;

            // create a new VMA object and add it to the process
            VMA vma(start, end, write_protected, file_mapped);
            process->address_space.push_back(vma);
        }
        processes.push_back(process);
        pstats.push_back(p_stat);
    }

    // at this point we are pointing to the first instruction in the input file
    
    // run simulation, keep reading instructions
    simulation();

    // generate final outputs
    if (P) {
        // for each process, print state of page table
        for (auto it = processes.begin(); it != processes.end(); advance(it, 1)) {
            printf("PT[%d]:", (*it)->pid);
            for (int i=0; i < NUM_VPAGES; i++ ) {
                PTE pte = (*it)->page_table[i];
                if (!pte.VALID) {
                    if (pte.PAGEDOUT) {
                        printf(" #");
                    } else {
                        printf(" *");
                    }
                } else {
                    printf(" %d:%c%c%c", i, (pte.REFERENCED ? 'R' : '-'), (pte.MODIFIED ? 'M' : '-'), (pte.PAGEDOUT ? 'S' : '-'));
                }
            }
            printf("\n");
        }
    }

    if (F) {
        // print state of frame table
        printf("FT:");
        for (int i=0; i < MAX_FRAMES; i++) {
            if (frame_table[i].vpage == -1) {
                printf(" *");
            } else {
                printf(" %d:%d", frame_table[i].process_id, frame_table[i].vpage);
            }
        }
        printf("\n");
    }

    if (S) {
        // print per process output
        for (const auto pstat : pstats) {
            printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
                pstat.pid,
                pstat.unmaps, pstat.maps, pstat.ins, pstat.outs,
                pstat.fins, pstat.fouts, pstat.zeros,
                pstat.segv, pstat.segprot);
        }
        // print summary line
        printf("TOTALCOST %lu %lu %lu %llu %lu\n",
            inst_count, ctx_switches, process_exits, cost, sizeof(PTE));

    }

    // release dynamically allocated memory
    delete[] frame_table;

    return 0;
}