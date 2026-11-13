#include "sam.h"
#include <stdio.h>
#include "uart.h"

typedef struct RingBuf RingBuf;
struct RingBuf {
    uint8_t buffer[1024]; // Byte storage and the capacity used by push() and pop().
    int capacity; // Unused field. The buffer array determines capacity.
    
    int insertIdx; // Next slot written by the receive handler.
    int removeIdx; // Next slot read by the consumer.
    int length; // Number of unread bytes.
};
RingBuf ringBuf = {0}; // Receive FIFO, initially empty.


int push(RingBuf* rb, uint8_t val){
    if(rb->length >= (sizeof(rb->buffer)/sizeof(rb->buffer[0]))){
        return 0;
    }
    rb->buffer[rb->insertIdx] = val;
    rb->insertIdx = (rb->insertIdx + 1) % (sizeof(rb->buffer)/sizeof(rb->buffer[0]));
    rb->length++;
    return 1;
}
/************************************************************************/
/*
 * push(): Appends one byte at the write index and wraps the index
 * at the array boundary. Returns 0 if full, otherwise 1.
 */
/************************************************************************/

int pop(RingBuf* rb, uint8_t* val){    
    if(!rb->length){
        return 0;
    }
    *val = rb->buffer[rb->removeIdx];
    rb->removeIdx = (rb->removeIdx + 1) % (sizeof(rb->buffer)/sizeof(rb->buffer[0]));
    rb->length--;
    return 1;
}
/************************************************************************/
/*
 * pop(): Removes the oldest queued byte into *val. Returns 0 if
 * empty and leaves *val alone. Returns 1 after copying a byte.
 */
/************************************************************************/

void uart_init(uint32_t cpufreq, uint32_t baudrate){
    PMC->PMC_PCER0 |= (1 << ID_UART);
    
    // Set UART pins (A8, A9) to use alternate function (this disables regular IO)
    PIOA->PIO_PDR   |=   PIO_PA8 | PIO_PA9;
    // Set alternate function A (see tables 9-2, 34-2)
    PIOA->PIO_ABSR  &= ~(PIO_PA8 | PIO_PA9);
    
    // Configure UART settings
    UART->UART_CR   |= UART_CR_TXEN | UART_CR_RXEN;
    UART->UART_MR   |= UART_MR_PAR_NO;
    UART->UART_BRGR = cpufreq / 16 / baudrate;
    
    
    // Configure interrupts on receive ready and errors
    UART->UART_IDR = 0xFFFFFFFF;
    UART->UART_IER = UART_IER_RXRDY | UART_IER_OVRE | UART_IER_FRAME | UART_IER_PARE;

    // Enable UART interrupt in the Nested Vectored Interrupt Controller (NVIC)
    NVIC_EnableIRQ((IRQn_Type) ID_UART);
    
}    
/************************************************************************/
/*
 * uart_init(): Selects the UART pins on PA8 and PA9, enables RX
 * and TX without parity, and sets the integer baud divisor.
 * Enables receive-ready and error interrupts in the UART and NVIC.
 */
/************************************************************************/

void uart_tx(uint8_t val){
    while(!(UART->UART_SR & UART_SR_TXEMPTY)){}
    UART->UART_THR = val;
}
/************************************************************************/
/*
 * uart_tx(): Waits for TXEMPTY, then loads one byte into the
 * transmit holding register.
 */
/************************************************************************/

uint8_t uart_rx(uint8_t* val){
    return pop(&ringBuf, val);
}    
/************************************************************************/
/*
 * uart_rx(): Tries to read one buffered byte. Returns 1 on success
 * or 0 when no byte is queued.
 */
/************************************************************************/

int uart_flush(char* buf, int len){
    int r = 0;
    for(; r < len; r++){
        int ret = uart_rx((uint8_t*)&buf[r]);
        if(!ret){
            break;
        }                
    }    
    return r;
}
/************************************************************************/
/*
 * uart_flush(): Copies up to len queued bytes into buf, stopping
 * when the receive queue is empty. Returns the number copied.
 * The caller supplies any string terminator it needs.
 */
/************************************************************************/


void UART_Handler(){
    
    uint32_t status = UART->UART_SR;
    
    // Clear error status and keep receive and transmit enabled.
    if(status & (UART_SR_OVRE | UART_SR_FRAME | UART_SR_PARE)){
        UART->UART_CR = UART_CR_RXEN | UART_CR_TXEN | UART_CR_RSTSTA;
    }
    
    // Receive ready: push to ring buffer
    if(status & UART_SR_RXRDY){
        if(!push(&ringBuf, UART->UART_RHR & 0xff)){
            printf("UART receive buffer full\n");
        }
    }
    
} 
/************************************************************************/
/*
 * UART_Handler(): Takes a snapshot of UART status, clears any
 * reported receive errors, and queues the received byte if ready.
 * If the receive FIFO is full, drops the byte and prints a warning.
 */
/************************************************************************/



// See https://interrupt.memfault.com/blog/boostrapping-libc-with-newlib

extern int _end;
#include <sys/stat.h>

void *_sbrk(int incr){
    static unsigned char *heap = NULL;
    unsigned char *prev_heap;

    if(heap == NULL){
        heap = (unsigned char*)&_end;
    }
    prev_heap = heap;

    heap += incr;

    return prev_heap;
}
/************************************************************************/
/*
 * _sbrk(): Returns the previous heap end and advances it by incr.
 * The initial heap address comes from the linker symbol _end.
 * This hook performs no memory-bound checks.
 */
/************************************************************************/

int _close(int file){
    return -1;
}
/************************************************************************/
/*
 * _close(): Stub that returns failure without closing a device.
 */
/************************************************************************/

int _fstat(int file, struct stat* st){
    st->st_mode = S_IFCHR;
    return 0;
}
/************************************************************************/
/*
 * _fstat(): Reports character-device mode in st_mode.
 */
/************************************************************************/

int _isatty(int file){
    return 1;
}
/************************************************************************/
/*
 * _isatty(): Reports that the descriptor is a terminal.
 */
/************************************************************************/

int _lseek(int file, int ptr, int dir){
    return 0;
}
/************************************************************************/
/*
 * _lseek(): Returns zero without changing a file position.
 */
/************************************************************************/

__attribute__((naked)) void _exit(int status){
}
/************************************************************************/
/*
 * _exit(): Empty termination hook with the naked attribute.
 */
/************************************************************************/

void _kill(int pid, int sig){
    return;
}
/************************************************************************/
/*
 * _kill(): Stub with no signal handling.
 */
/************************************************************************/

int _getpid(void){
    return -1;
}
/************************************************************************/
/*
 * _getpid(): Returns -1. No process ID is supplied.
 */
/************************************************************************/

int _write(int file, char* ptr, int len){
    if(file > 1){
        return -1;    
    }

    for(int idx = 0; idx < len; idx++){
        uart_tx((uint8_t)ptr[idx]);
    }
    return len;
}
/************************************************************************/
/*
 * _write(): Sends len bytes via the blocking UART transmit routine.
 * Returns the byte count, or -1 for a descriptor greater than 1.
 */
/************************************************************************/

int _read(int file, char* ptr, int len){
    if(file > 1){
        return -1;
    }
    
    int nread = 0;
    for(int idx = 0; idx < len; idx++){
        int b = uart_rx((uint8_t*)&ptr[idx]);
        nread += b;     
        if(!b){
            return nread;
        }        
    }
    return nread;
}
/************************************************************************/
/*
 * _read(): Copies available UART bytes without waiting for more.
 * Returns the count copied, or -1 for a descriptor greater than 1.
 */
/************************************************************************/

/************************************************************************/
/*
misc:
 *
 * __asm("BKPT #0");
 */
/************************************************************************/
