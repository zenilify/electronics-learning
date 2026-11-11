#pragma once
// INA226 power monitor on the BSP I2C bus (0x41, 5 mOhm shunt — same as tab5-banco.yaml).
void battery_start(void);
int battery_percent(float v);   // hypothesis: 2S Li-ion, 6.4 V = 0 %, 8.3 V = 100 % (verify on the unit)
int battery_chg_kicks(void);   // times the charger was re-triggered by the supervisor in battery.c
