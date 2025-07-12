#ifndef SHUTDOWN_H
#define SHUTDOWN_H

#define SHUTDOWN_RELAY_PIN 22

void shutdown(void);
void init_relay(void);
void set_relay(int hilo);

#endif