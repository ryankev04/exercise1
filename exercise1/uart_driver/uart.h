#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart1_init(void);
void uart1_send(char c);
uint8_t uart1_available(void);
char uart1_receive(void);

#endif
