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

#define NEAR(x, a, tol) (((x) <= ((a)+(tol))) && ((x) >= ((a)-(tol))))

void parse(volatile uint16_t *buf, uint8_t sz)
{
    /* skip till first header, sometimes there's a spurious space before */
    while (!NEAR(*buf, 3000, 500)) buf++;

    /*
    for (uint8_t i=0; i<sz; i++) {
        if (NEAR(buf[i], 3000, 500))
            printf("H%u ", buf[i]);
        else if (NEAR(buf[i], 9000, 500))
            printf("h%u ", buf[i]);
        else {
            printf("%c%u ", i&1 ? 's' : 'm', buf[i]);
        }
    }
    printf("\n");
    */

    for (uint8_t i=0; i<sz; i++) {
        if (NEAR(buf[i], 3000, 500))
            printf("H");
        else if (NEAR(buf[i], 9000, 500))
            printf("h");
        else if (i & 1)
            printf("%c", buf[i] > 1000 ? '1' : '0');
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
            parse(buffer, last_burst_size);
            last_burst_size = 0;
        }
    }

    return 1;
}
