#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BAUD 19200

#include <util/setbaud.h>

void uart_init(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;

#if USE_2X
    UCSR0A |= _BV(U2X0);
#else
    UCSR0A &= ~(_BV(U2X0));
#endif
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); /* 8-bit data */
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);   /* Enable RX and TX */
}

/*
 * Send character c down the UART Tx, wait until tx holding register
 * is empty.
 */
int
uart_putchar(char c, FILE *stream)
{

    if (c == '\a')
    {
        fputs("*ring*\n", stdout);
        return 0;
    }

    if (c == '\n')
        uart_putchar('\r', stream);
    loop_until_bit_is_set(UCSR0A, UDRE0);
    UDR0 = c;

    return 0;
}

#define MAXBURSTSIZE 512
#define MAXCMDINBURST 4
#define MINBURSTSIZE 10
#define DEFAULT_TOLERANCE 0.2
#define _APPROX(x, a, tol) (((x) <= ((a)+(a*tol))) && ((x) >= ((a)-(a*tol))))
#define APPROX(x, a) _APPROX((x), (a), (DEFAULT_TOLERANCE))

#define HEADER_MARK 3000
#define HEADER_SPACE 9000
#define LONG_CMD_SPACE 2500
#define SPACE_BIT_THRESHOLD 1000
// #define TIMINGSDEBUG


/* interrupts are nice... but too much global state for my taste,
 * should I move to a simple polling instead of pin change
 * interrupts? */
volatile uint16_t buffer[MAXBURSTSIZE];
volatile uint8_t edge_count = 0;
volatile uint16_t last_burst_size = 0;

ISR(PCINT2_vect) {
    buffer[edge_count] = TCNT1 / 2; /* time to us */

    last_burst_size = edge_count;
    edge_count++;

    TCNT1 = 0; /* reset timer at each edge */
}

ISR(TIMER1_OVF_vect) {
    /* timer overflows at 2^16 * 0.5 us = 33 ms */
    /* the biggest space/mark we can have is about 9 ms */
    /* anything above it can be considered idling, reset edge counter */
    edge_count = 0;
}

FILE uart_output = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_RW);

void parse(volatile uint16_t *buf, uint16_t sz)
{
    uint8_t chunk[8];
    uint8_t bit;

    uint16_t headers[MAXCMDINBURST];
    uint8_t headers_count = 0;
    memset(headers, 0, MAXCMDINBURST*sizeof(uint16_t));

    memset(chunk, 0, 8*sizeof(uint8_t));


    /* super buggy, barely tested, just works for my single limited
     * use case, go beyond it if you don't like it and shut up */

    /* find headers */
    for (uint16_t i=0; i<(sz-1); i++) {
        if (APPROX(buf[i], HEADER_MARK))
            if (APPROX(buf[i+1], HEADER_SPACE)) {
                // printf("found header at: %d\n", i);
                if (headers_count >= MAXCMDINBURST - 1)
                    printf("too much commands, probably something very wrong,"
                           "ignoring the following ones... FWIW\n");
                else {
                    headers[headers_count] = i;
                    headers_count++;
                }
            }
    }

    /* foreach header try to parse the command */
    /* print raw timings if enabled, the full bit field and the parsed
     * bytes */
    for (uint8_t h=0; h<headers_count; h++) {
        uint16_t start = headers[h]+2;
        uint16_t end = ((h+1) < headers_count) ? headers[h+1] : sz;

#ifdef TIMINGSDEBUG
        printf("H%d ", buf[headers[h]]);
        printf("h%d ", buf[headers[h]+1]);
        for (uint16_t i=start; i<end; i++) {
            printf("%c%u ", i&1 ? 'm' : 's', buf[i]);
        }
        printf("\n");
#endif

        printf("Hh");
        for (uint16_t i=start; i<end; i++) {
            if ((i-start)&1) {
                bit = buf[i] > SPACE_BIT_THRESHOLD ? 1 : 0;
                printf("%d", bit);
                chunk[((i-start)/2)/8] |= bit << ((i-start)/2)%8;
            }
        }

        for (uint8_t i=0; i<7; i++) {
            printf(" %02X", chunk[i]);
        }

        printf("\n");
    }
}

int main (void)
{
    uart_init();
    stdout = stdin = &uart_output;

    DDRD &= ~_BV(DDD2);     /* enable input on PD2 */
    PORTD |= _BV(PORTD2);   /* pull up on PD2 */
    PCMSK2 |= _BV(PCINT18); /* set pin change interrupt mask */
    PCICR |= _BV(PCIE2);    /* enable pin change interrupt */

    TCCR1B |= (1 << CS11); /* 16bit counter, prescaler 8, 0.5 us ticks */
    TIMSK1 = (1 << TOIE1); /* interrupt on overflow */

    TCNT1 = 0;             /* reset counter */
    TIFR1 = 0;             /* reset interrupt flag, not sure is needed */

    sei();

    for (;;) {
        _delay_ms(2);
        if ((edge_count == 0) && (last_burst_size >= MINBURSTSIZE)) {
            // printf("got buf: %d\n", last_burst_size);
            parse(buffer, last_burst_size);
            last_burst_size = 0;
        }
    }

    return 1;
}
