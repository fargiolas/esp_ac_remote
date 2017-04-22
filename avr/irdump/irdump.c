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
#define MINBURSTSIZE 10

/* interrupts are nice... but too much global state for my taste,
 * should I move to a simple polling instead of pin change
 * interrupts? */
volatile uint16_t buffer[MAXBURSTSIZE];
volatile uint8_t edge_count = 0;
volatile uint8_t last_burst_size = 0;

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

#define DEFAULT_TOLERANCE 0.2
#define _APPROX(x, a, tol) (((x) <= ((a)+(a*tol))) && ((x) >= ((a)-(a*tol))))
#define APPROX(x, a) _APPROX((x), (a), (DEFAULT_TOLERANCE))

#define HEADER_MARK 3000
#define HEADER_SPACE 9000
#define LONG_CMD_SPACE 2500
#define SPACE_BIT_THRESHOLD 1000

void parse(volatile uint16_t *buf, uint8_t sz)
{
    uint8_t chunk[8];
    uint8_t bit;

    /* skip till first header, sometimes there's a spurious space before */
    while (!APPROX(*buf, HEADER_MARK)) { buf++; sz--; }

    memset(chunk, 0, sizeof(chunk));

    /* print raw timings */

    for (uint8_t i=0; i<sz; i++) {
        if (APPROX(buf[i], HEADER_MARK))
            printf("H%u ", buf[i]);
        else if (APPROX(buf[i], HEADER_SPACE))
            printf("h%u ", buf[i]);
        else {
            printf("%c%u ", i&1 ? 'm' : 's', buf[i]);
        }
    }
    printf("\n");

    /* bits and bytes */
    /* doesn't handle the double commands (power off and timer setup)
     * because I'm lazy, the new command seem to be marked by an extra
     * bit plus a new header, sometimes the extra bit space is longer
     * than usual */
    for (uint8_t i=0; i<sz; i++) {
        if (APPROX(buf[i], HEADER_MARK)) {
            printf("H");
        }
        else if (APPROX(buf[i], HEADER_SPACE)) {
            printf("h");
        }
        else if (i & 1) {
            bit = buf[i] > SPACE_BIT_THRESHOLD ? 1 : 0;
            printf("%d", bit);
            chunk[((i-2)/16)] |= bit << ((i-2)/2)%8;
        }
    }
    for (uint8_t i=0; i<7; i++) {
        printf(" %02X", chunk[i]);
    }
    printf("\n");

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

    TCNT1 = 0;
    TIFR1 = 0;

    sei();

    for (;;) {
        _delay_ms(1);
        if ((edge_count == 0) && (last_burst_size >= MINBURSTSIZE)) {
            printf("got buf: %d\n", last_burst_size);
            parse(buffer, last_burst_size);
            last_burst_size = 0;
        }
    }

    return 1;
}
