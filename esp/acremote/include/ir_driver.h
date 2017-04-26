#ifndef _IR_DRIVER_H
#define _IR_DRIVER_H

#define IR_AC_MODE_AUTO      0x00
#define IR_AC_MODE_COOL      0x10
#define IR_AC_MODE_DRY       0x20
#define IR_AC_MODE_FAN       0x30
#define IR_AC_MODE_WARM      0x40

#define IR_FAN_MODE_AUTO     0x01
#define IR_FAN_MODE_LOW      0x05
#define IR_FAN_MODE_MID      0x09
#define IR_FAN_MODE_HIGH     0x0B
#define IR_FAN_MODE_NATURAL  0x0D

#define IR_POWER_MODE_NORMAL 0x01
#define IR_POWER_MODE_SLEEP  0x03
#define IR_POWER_MODE_TURBO  0x07
#define IR_POWER_MODE_SAVE   0x0F

#define IR_DISPLAY_MODE_ON   0xF0
#define IR_DISPLAY_MODE_OFF  0xE0

#define IR_SWING_MODE_ON     0xA0
#define IR_SWING_MODE_OFF    0xF0

#define IR_ION_MODE_ON       0x08
#define IR_ION_MODE_OFF      0x00


void ICACHE_FLASH_ATTR ir_send_cmd (uint8_t *command);
void ICACHE_FLASH_ATTR ir_send_cmd_simple (uint8_t *command);

void ICACHE_FLASH_ATTR build_command (uint8_t *cmd, uint8_t mode, uint8_t temperature,
                                      uint8_t fan_speed, uint8_t swing,
                                      uint8_t energy_mode, uint8_t ions,
                                      uint8_t display);



#endif /* _IR_REMOTE_H */
