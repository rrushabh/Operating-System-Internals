#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <regex>

using namespace std;

struct Symbol {
    string value;

    Symbol(const string& v) {
        regex pattern("[a-zA-Z][a-zA-Z0-9]*");
        if (!regex_match(v, pattern)) {
            value = "INVALID_SYMBOL";
        } else {
            if (v.length() > 16) {
                value = "INVALID_SYMBOL_LONG";
            } else {
                value = v;
            }
        }
    }
};

class Tokenizer {
    public:
        ifstream file;
        bool EOF_reached;
        bool newlineread;
        char delimiters[4] = " \t\n";
        char* token;
        string curr_line;
        string prev_line;
        string line;
        int curr_line_num;
        int curr_offset;
        int abs_address_offset;
        vector<pair<pair<string, int>, pair<pair<int, int>, int>>> symbolTable;
        // <<name, abs_address>, <<not used?, redefined?>, module_num>>
        vector<string> memoryMap;
        size_t index_size = 3;

        Tokenizer(string input_path) {
            file.open(input_path.c_str(), fstream::in);
            if (!file.is_open()) {
                cerr << "Usage: ./linker inputfile" << endl;
            } else {
                // cout << "Tokenizer successfully created" << endl;
                EOF_reached = false;
                newlineread = false;
            }
            
            curr_line_num = 0;
            curr_offset = 0;
        }

        void reset_file(string input_path) {
            file.close();
            file.open(input_path.c_str(), fstream::in);
            curr_line_num = 0;
            curr_offset = 0;
        }

        void __parseerror(int errcode) {
            static string errstr[] = {
                "NUM_EXPECTED", // Number expect, anything >= 2^30 is not a number either // DONE
                "SYM_EXPECTED", // Symbol Expected // DONE
                "ADDR_EXPECTED", // Addressing Expected which is A/E/I/R // DONE
                "SYM_TOO_LONG", // > 16, Symbol Name is too long // DONE
                "TOO_MANY_DEF_IN_MODULE", // > 16 // DONE
                "TOO_MANY_USE_IN_MODULE", // > 16 // DONE
                "TOO_MANY_INSTR", // total num_instr exceeds memory size (512)
            };
            cout << "Parse Error line " << curr_line_num << " offset " << curr_offset << ": " << errstr[errcode] << "\n";
            // printf("Parse Error line %d offset %d: %s\n", curr_line_num, curr_offset, errstr[errcode]);
        }

        char* getToken() {
            token = strtok(NULL, delimiters);
            if (token == NULL) {
                // cout << "token is null, moving to next line" << "\n";
                //prev_line = curr_line;
                if (getline(file, curr_line)) {
                    prev_line = curr_line;
                    // cout << "next line is non-empty" << "\n";
                    // curr_line = line;
                    // cout << curr_line << "\n";
                    curr_line_num++;
                    curr_offset = 1;
                    // cout << "reading token" << endl;
                    token = strtok(const_cast<char*>(curr_line.c_str()), delimiters);
                    if (token == NULL) {
                        // cout << "next token is null, recursive" << endl;
                        // cout << "Going recursive\n";
                        return getToken();
                    }
                    // cout << "Printing first\n";
                    curr_offset = token - const_cast<char*>(curr_line.c_str()) + 1;
                    // cout << "Token: " << curr_line_num << ":" << curr_offset << " : " << token << "\n";
                } else {
                    // cout << "nextline is empty, eof reached" << endl;
                    //curr_offset = prev_line.length() + 1;
                    if(file.eof() & (prev_line == curr_line)) {
                        curr_offset = prev_line.length();
                        // cout << prev_line << endl;
                    }
                    else
                    {
                        curr_offset = prev_line.length() + 1;
                        // cout << curr_offset << endl;
                    }
                    // if (file.eof()) {
                    //     // cout << "file eof" << endl;
                    //     curr_offset -= 1;
                    //     // if (curr_offset < 1) {
                    //     //     curr_offset = 1;
                    //     // }
                    // }
                    EOF_reached = true;
                    // cout << "next line is empty" << "\n";
                    // cout << "Final Spot in File : line=" << curr_line_num << " offset=" << prev_line.length() + 1 << "\n";
                }
            } else {
                curr_offset = token - const_cast<char*>(curr_line.c_str()) + 1;
                // cout << "Token: " << curr_line_num << ":" << curr_offset << " : " << token << "\n";
            }
            return token;
        }

        int readInt() {
            token = getToken();
            if (EOF_reached) {
                return -1;
            }
            if(!token) {
                __parseerror(0);
                exit(1);
            }
            string tokAsString = token;
            for (char c : tokAsString) {
                if (!isdigit(c)) {
                    __parseerror(0);
                    exit(1);
            }
            }
            int tokAsInt = stoi(tokAsString);
            if (tokAsInt >= pow(2, 30)) {
                __parseerror(0);
                exit(1);
            }
            // cout << "Parsed integer " << tokAsInt << " \n";
            return tokAsInt; // return token as int
        }

        // Symbol readSym() {
        //     token = getToken();
        //     string tokAsString = token;
        //     Symbol symbol(tokAsString);
        //     string invalid = "INVALID_SYMBOL";
        //     if (symbol.value == invalid) {
        //         __parseerror(1);
        //         exit(1);
        //     }
        //     return symbol;
        // }

        string readSym() {
            token = getToken();
            if(!token) {
                __parseerror(1);
                exit(1);
            }
            string tokAsString = token;
            regex pattern("[a-zA-Z][a-zA-Z0-9]*");
            if (!regex_match(tokAsString, pattern)) {
                __parseerror(1);
                exit(1);
            } else if (tokAsString.length() > 16) {
                __parseerror(3);
                exit(1);
            }
            return tokAsString;
        }

        string readAIRE() {
            token = getToken();
            if(!token) {
                __parseerror(2);
                exit(1);
            }
            string tokAsString = token;
            regex pattern("[A|I|R|E]");
            if (!regex_match(tokAsString, pattern)) {
                __parseerror(2);
                exit(1);
            }
            return tokAsString;
        }

        void createSymbol(string sym, int val, int module_num) {
            for (int i=0; i<symbolTable.size(); i++) {
                if (symbolTable[i].first.first == sym) {
                    cout << "Warning: Module " << module_num << ": " << sym << " redefined and ignored" << endl; 
                    symbolTable[i].second.first.second = 1;
                    return;
                }
            }
            symbolTable.push_back({{sym, abs_address_offset+val}, {{0, 0}, module_num}});
        }

        void pass1() {
            int module_counter = 0;
            abs_address_offset = 0;
            while (file.peek() != EOF) {
                // cout << file.peek() << endl;
                // this is a new module
                module_counter++;
                vector<pair<string, int>> deflist;
                int defcount = readInt();
                if (defcount == -1) {
                    break;
                }
                if (defcount > 16) {
                    __parseerror(4);
                    exit(1);
                }
                for (int i=0; i<defcount; i++) {
                    string sym = readSym();
                    int val = readInt();
                    deflist.push_back({sym, val});
                }
                int usecount = readInt();
                if (usecount > 16) {
                    __parseerror(5);
                    exit(1);
                }
                for (int i=0;i<usecount;i++) {
                    string sym = readSym();
                    // we don’t do anything here  this would change in pass2
                }
                int instcount = readInt();
                // cout << instcount << endl;
                if (abs_address_offset + instcount > 512) {
                    __parseerror(6);
                    exit(1);
                }
                for (int i=0; i<instcount; i++) {
                    string addressmode = readAIRE();
                    int operand = readInt();
                    // : // various checks  this would change in pass2
                    // : // - “ -
                }
                for (int i=0; i<deflist.size(); i++) {
                    createSymbol(deflist[i].first, deflist[i].second, module_counter);
                }

                for (int i=0; i<symbolTable.size(); i++) {
                if (symbolTable[i].second.second == module_counter) {
                    if ((symbolTable[i].first.second-abs_address_offset) >= instcount) {
                        cout << "Warning: Module " << module_counter << ": " << symbolTable[i].first.first << " too big " << symbolTable[i].first.second-abs_address_offset << " (max=" << instcount-1 << ") assume zero relative" << endl;
                        symbolTable[i].first.second = abs_address_offset;
                    }
                }
            }

                abs_address_offset = abs_address_offset + instcount;
            }
            // error finishing parsing
            cout << "Symbol Table\n";
            for (const auto &p : symbolTable) {
                if (p.second.first.second == 1) {
                    cout << p.first.first << "=" << p.first.second << " Error: This variable is multiple times defined; first value used" << endl;
                } else {
                    cout << p.first.first << "=" << p.first.second << endl;
                }
            }
            cout << "\n";
        }

        void pass2() {
            EOF_reached = false;
            memoryMap.push_back("Memory Map");
            cout << "Memory Map" << endl;
            int module_counter = 0;
            abs_address_offset = 0;
            int total_inst_num = 0;
            while (file.peek() != EOF) {
                module_counter++;
                // this is a new model
                int defcount = readInt();
                if (defcount == -1) {
                    break;
                }
                for (int i=0; i<defcount; i++) {
                    string sym = readSym();
                    int val = readInt();
                    // createSymbol(sym, val);
                }
                int usecount = readInt();
                vector<pair<string, int>> uselist;
                for (int i=0;i<usecount;i++) {
                    string sym = readSym();
                    uselist.push_back({sym, 0});
                    // check that each symbol used is defined in symbol table
                }
                int instcount = readInt();
                for (int i=0; i<instcount; i++) {
                    string addressmode = readAIRE();
                    int inst = readInt();
                    int opcode = inst / 1000;
                    int operand = inst % 1000;
                    int newaddress;
                    string error;
                    // check that opcode less than 10, if not then 9999
                    if ((opcode >= 10) & (addressmode != "I")) {
                        error = " Error: Illegal opcode; treated as 9999";
                        opcode = 9;
                        operand = 999;
                        newaddress = 999;
                    } else {
                        if (addressmode == "A") {
                            // check less than machine size, if not then 0
                            if (operand >= 512) {
                                error = " Error: Absolute address exceeds machine size; zero used";
                                operand = 0;
                            }
                            newaddress = operand;
                        } else if (addressmode == "I") {
                            // simple case, unchanged
                            // check that opcode less than 10, if not then 9999
                            if (opcode >= 10) {
                                error = " Error: Illegal immediate value; treated as 9999";
                                opcode = 9;
                                operand = 999;
                                newaddress = 999;
                            }
                            newaddress = operand;
                        } else if (addressmode == "R") {
                            // check that operand less than instcount, if not then operand = 0
                            if (operand >= instcount) {
                                error = " Error: Relative address exceeds module size; zero used";
                                operand = 0;
                            }
                            // operand + absolute module address
                            newaddress = abs_address_offset + operand;
                        } else if (addressmode == "E") {
                            // check operand strictly less than usecount, if not then treat as I
                            if (operand >= usecount) {
                                error = " Error: External address exceeds length of uselist; treated as immediate";
                                newaddress = operand;
                            } else {
                                // NEXT STEP: STORE USE LIST
                                // USE THIS OPERAND TO INDEX INTO USE LIST
                                string used_sym = uselist[operand].first;
                                uselist[operand].second = 1;
                                int symbol_found = 0;
                                for (int i=0; i<symbolTable.size(); i++) {
                                    if (symbolTable[i].first.first == used_sym) {
                                        symbol_found = 1;
                                        newaddress = symbolTable[i].first.second;
                                        symbolTable[i].second.first.first = 1; // symbol has been used
                                        break;
                                    }
                                }
                                if (symbol_found == 0) {
                                    error = " Error: " + used_sym + " is not defined; zero used";
                                    newaddress = 0;
                                }
                            }
                        
                        // operand is index into this module's use list
                        // check that indexed element in use list is in symbol table, if not then 0
                        // replace operand with absolute element address (from symbol table)
                        }
                    }
                    int num_zeroes = index_size - min(index_size, to_string(total_inst_num + i).size());
                    string index = string(num_zeroes, '0').append(to_string(total_inst_num + i));
                    int num_zeroes_2 = index_size - min(index_size, to_string(newaddress).size());
                    string newaddressf = string(num_zeroes_2, '0').append(to_string(newaddress));
                    string new_entry = index + ": " + to_string(opcode) + newaddressf + error;
                    cout << new_entry << endl;
                    memoryMap.push_back(new_entry);
                }

                // check all elements in uselist have been used
                for (int i=0; i<uselist.size(); i++) {
                    if (uselist[i].second == 0) {
                        cout << "Warning: Module " << module_counter << ": " << uselist[i].first << " appeared in the uselist but was not actually used" << endl; 
                    }
                }

                total_inst_num = total_inst_num + instcount;
                abs_address_offset = abs_address_offset + instcount;
            }
            // error finishing parsing
            // for (const auto &p : memoryMap) {
            //     cout << p << endl;
            // }

            for (int i=0; i<symbolTable.size(); i++) {
                if (symbolTable[i].second.first.first == 0) {
                    cout << "Warning: Module " << symbolTable[i].second.second << ": " << symbolTable[i].first.first << " was defined but never used" << endl; 
                }
            }
        }
};


// void pass2() {
//     while (!tokenizer.file.eof()) {
//         // this is a new model
//         int defcount = readInt();
//         for (int i=0; i<defcount; i++) {
//             Symbol sym = readSym();
//             int val = readInt();
//             createSymbol(sym, val); // this changes in pass2
//         }
//         int usecount = readInt();
//         for (int i=0;i<usecount;i++) {
//             Symbol sym = readSym();
//             // we don’t do anything here // this changes in pass2
//         }
//         int intscount = readInt();
//         for (int i=0; i<intscount; i++) {
//             char addressmode = readAIRE();
//             int operand = readInt();
//             : // this changes in pass2
//             : // - “ -
//         }
//     }
// }

int main(int argc, char* argv[]) {
    // Make sure exactly one additional argument is passed
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " inputfile" << endl;
        return 1;
    }
    
    string input_path(argv[1]);
    
    Tokenizer tokenizer(input_path);

    // while (!tokenizer.file.eof()) {
    //     tokenizer.getToken();
    //     tokenizer.readSym();
    //     tokenizer.readAIRE();
    //     // __parseerror(1);
    // }

    tokenizer.pass1();
    tokenizer.reset_file(input_path);
    tokenizer.pass2();

    // cout << "eof reached" << endl;
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();

    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();
    // tokenizer.getToken();

    return 0;
}