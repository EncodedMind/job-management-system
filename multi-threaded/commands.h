#ifndef COMMANDS_H
#define COMMANDS_H

#include <string>

using namespace std;

// Command handler declarations
string handle_submit(string command);
string handle_status(string command);
string handle_status_all(string command);
string handle_show_active(string command);
string handle_show_workers(string command);
string handle_show_finished(string command);
string handle_shutdown(string command);

#endif // COMMANDS_H