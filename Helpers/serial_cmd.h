#ifndef SERIAL_CMD_H
#define SERIAL_CMD_H

#include <stdbool.h>

void print_help(void);
bool process_serial_commands(float *frequency, float *duty_cycle, int *auto_tc_print);

#endif