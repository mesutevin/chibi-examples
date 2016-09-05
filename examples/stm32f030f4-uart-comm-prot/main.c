/*
    ChibiOS - Copyright (C) 2006..2015 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "ch.h"
#include "hal.h"

#define SOH 0x01
#define EOT 0x04
#define GET_BYTE(VALUE, BYTE_INDEX) ((VALUE & (0xFF << (BYTE_INDEX * 8))) >> (BYTE_INDEX * 8))
#define PAYLOAD_LENGTH 23
#define PACKET_SIZE 5 + PAYLOAD_LENGTH
#ifndef TRUE
#define TRUE 1
#define FALSE !TRUE
#endif

void shift_array(uint8_t *arr, int i) {
    int j = 0;
    while(i < PACKET_SIZE) {
        arr[j] = arr[i];
        j++;
        i++;
    }

}



typedef struct Comm {

    //Comm functions
    void (*send) (struct Comm *);
    void (*read) (struct Comm *);
    void (*set_digital) (struct Comm *, int, int);
    int (*get_digital) (struct Comm *, int);
    void (*set_analog) (struct Comm *, int, uint16_t);
    void (*on_receive) (struct Comm *);
    //#Comm functions

    //Protocol variables
    uint8_t frame_start;
    uint8_t protocol_id;
    uint8_t length; 		//length of payload
    uint8_t comp_of_length;	//complement of length
    uint32_t digital_io;
    uint16_t analog_io[4];
    uint8_t virtual_tx_start;
    uint8_t virtual_tx[5];
    uint8_t virtual_rx[5];
    uint8_t frame_end;

    //#Protocol variables

} Comm;

void comm_protocol_send(Comm *c) {
    uint8_t packet[PACKET_SIZE];

    packet[0] = c->frame_start;
    packet[1] = c->protocol_id;

    //TODO: calc_payload_length??? but not ready payload here
    packet[2] = c->length;
    packet[3] = ~(c->length);

    //Payload
    packet[4] = GET_BYTE(c->digital_io, 3);
    packet[5] = GET_BYTE(c->digital_io, 2);
    packet[6] = GET_BYTE(c->digital_io, 1);
    packet[7] = GET_BYTE(c->digital_io, 0);

    packet[8] = GET_BYTE(c->analog_io[3], 1);
    packet[9] = GET_BYTE(c->analog_io[3], 0);
    packet[10] = GET_BYTE(c->analog_io[2], 1);
    packet[11] = GET_BYTE(c->analog_io[2], 0);
    packet[12] = GET_BYTE(c->analog_io[1], 1);
    packet[13] = GET_BYTE(c->analog_io[1], 0);
    packet[14] = GET_BYTE(c->analog_io[0], 1);
    packet[15] = GET_BYTE(c->analog_io[0], 0);

    packet[16] = 0x01; //virtual_tx_start
    //virtual_tx
    packet[17] = 0x16;
    packet[18] = 0x17;
    packet[19] = 0x18;
    packet[20] = 0x19;
    packet[21] = 0x19;
    //#virtual_tx

    //virtual_rx
    packet[22] = 0x16;
    packet[23] = 0x17;
    packet[24] = 0x18;
    packet[25] = 0x19;
    packet[26] = 0x19;
    //#virtual_rx

    //#Payload
    packet[27] = c->frame_end;
    /*
    int i;
    for(i = 0; i < PACKET_SIZE; i++) {
	//printf("%d-", i);
	printf("%X, ", packet[i]);
    }
    */
    sdWrite(&SD1, packet, PACKET_SIZE);

}

void comm_protocol_set_digital(Comm *c, int pin, int state) {
    uint32_t mask = 0 << 31;
    if(pin < 0 || pin > 15)
    	return;

    if(state) {
    	mask |= (1<<pin);
    	mask <<= 16;
    	c->digital_io = c->digital_io | mask;
    }
    else {
    	mask |= (1 << pin);
    	mask = ~mask;
    	mask <<= 16;
    	uint32_t new_mask = 0 << 31;
    	new_mask |= 1 << 15;
    	mask |= new_mask;
    	c->digital_io &= mask;
    }

}

int comm_protocol_get_digital(Comm *c, int pin) {
    uint32_t mask = 0 << 31;
    mask |= (1<<pin);
    mask <<= 16;
    mask &= c->digital_io;
    uint8_t result = mask >> (16+pin);
    if(result == 0x01) {
        return TRUE;
    }
    else return FALSE;

}

void comm_protocol_set_analog(Comm *c, int pin, uint16_t value) {
    if(pin >= 0 && pin <= 3) {
	c->analog_io[pin] = value;
    }
}

void comm_protocol_read(Comm *c) {
    uint8_t rxbuffer[PACKET_SIZE];
    int frame_detected = FALSE;
    int read_n_byte = PACKET_SIZE;
    int i = 0;
    int read_flag = TRUE;

    while(!frame_detected) {

        if(read_flag) {
            sdRead(&SD1, rxbuffer + PACKET_SIZE - read_n_byte, read_n_byte);
            read_flag = FALSE;
        }

        if(rxbuffer[i] == c->frame_start) {
            if(i+3 < PACKET_SIZE) { //rxbuffer[i+3] is exist?
                if( (rxbuffer[i+2] ^ rxbuffer[i+3]) == 0xFF) { //length xor length complement
                    if(i == 0) {
                        if(rxbuffer[PACKET_SIZE-1] == c->frame_end) {
                            //telegram is valid
                            //clear the IO's
                            c->digital_io = 0;
                            c->analog_io[0] = 0;
                            c->analog_io[1] = 0;
                            c->analog_io[2] = 0;
                            c->analog_io[3] = 0;
                            //#clear

                            //set the IO's
                            c->digital_io |= rxbuffer[4] << 24;
                            c->digital_io |= rxbuffer[5] << 16;
                            c->digital_io |= rxbuffer[6] << 8;
                            c->digital_io |= rxbuffer[7] << 0;

                            c->analog_io[0] |= rxbuffer[8] << 8;
                            c->analog_io[0] |= rxbuffer[9] << 0;
                            c->analog_io[1] |= rxbuffer[10] << 8;
                            c->analog_io[1] |= rxbuffer[11] << 0;
                            c->analog_io[2] |= rxbuffer[12] << 8;
                            c->analog_io[2] |= rxbuffer[13] << 0;
                            c->analog_io[3] |= rxbuffer[14] << 8;
                            c->analog_io[3] |= rxbuffer[15] << 0;

                            c->virtual_tx_start = rxbuffer[16];

                            int j;
                            for(j = 0; j < 5; j++) {
                                c->virtual_tx[j] = rxbuffer[17+j];
                            }

                            for(j = 0; j < 5; j++) {
                                c->virtual_rx[j] = rxbuffer[22+j];
                            }

                            //sdWrite(&SD1, rxbuffer, sizeof(rxbuffer));
                            c->on_receive(c);
                            //if virtual tx start is true than call virtual serial tx start

                            frame_detected = TRUE;
                        }
                        else {
                            i++;
                            continue;
                        }
                    }
                    else {
                        shift_array(rxbuffer, i);
                        read_n_byte = i;
                        i = 0;
                        read_flag = TRUE;
                    }
                }
                else {
                    i++;
                    continue;
                }
            }
            else {
                shift_array(rxbuffer, i);
                read_n_byte = i;
                i = 0;
                read_flag = TRUE;
            }

        }
        else if(i < PACKET_SIZE-1){
            i++;

        }
        else {
            read_n_byte = PACKET_SIZE;
            i = 0;
            read_flag = TRUE;
        }
    }



}

void comm_protocol_on_receive(Comm *c) {
    //This function invoked when the valid telegram is received.
    //You can write here, what ever do you want.
    //Example if virtual_tx_start is true then start second serial tx with virtual_tx
    /*
    if(c->virtual_tx_start) {
        sdStart(&SD2, NULL);
        sdWrite(&SD2, virtual_tx, 5);

    }
    else {
        sdStop(&SD2);
    }
    */
    sdWrite(&SD1, (c->virtual_rx), 5);
    return;
}

Comm* new_Comm(Comm* cp, uint8_t id){
    cp->protocol_id = id;
    cp->frame_start = SOH;
    cp->length = 0x1B;
    cp->frame_end = EOT;

    cp->send = comm_protocol_send;
    cp->read = comm_protocol_read;
    cp->set_digital = comm_protocol_set_digital;
    cp->get_digital = comm_protocol_get_digital;
    cp->set_analog = comm_protocol_set_analog;
    cp->on_receive = comm_protocol_on_receive;

    return cp;
};


int main(void) {
    /*
    * System initializations.
    * - HAL initialization, this also initializes the configured device drivers
    *   and performs the board-specific initializations.
    * - Kernel initialization, the main() function becomes a thread and the
    *   RTOS is active.
    */
    halInit();
    chSysInit();

    /*
    * Activates the serial driver 2 using the driver default configuration.
    */
    sdStart(&SD1, NULL);
    palSetPadMode(GPIOA, 2, PAL_MODE_ALTERNATE(1));       /* USART1 TX.       */
    palSetPadMode(GPIOA, 3, PAL_MODE_ALTERNATE(1));      /* USART1 RX.       */

    palClearPad(GPIOA, 1);
    static Comm __c;
    Comm* c = new_Comm(&__c, 0xAA);

    //Example
    int i;
    for(i = 0; i < 16; i++) {
    c->set_digital(c, i, 0);
        if(i < 5) {
            c->set_analog(c, i, 0xFFAA);
        }
    }
    c->set_digital(c, 0, 1);
    //#Example
    c->send(c);
    /*
    //Example
    c->read(c);
    int result = c->get_digital(c, 0);
    if(result) {
      palSetPad(GPIOA, 1);
      chThdSleepMilliseconds(1000);
      palClearPad(GPIOA, 1);

    }
    else {
      palSetPad(GPIOA, 1);
      chThdSleepMilliseconds(5000);
      palClearPad(GPIOA, 1);

    }
    //#Example
    */
    while (1) {
      if(true) {
          c->read(c);
          chThdSleepMilliseconds(5000);
      }
    }
}
