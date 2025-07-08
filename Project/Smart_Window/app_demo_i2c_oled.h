/*
 * Copyright (c) 2022 HiSilicon (Shanghai) Technologies CO., LIMITED.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APP_DEMO_I2C_OLED_H
#define APP_DEMO_I2C_OLED_H

unsigned int OledInit(void);
void OledSetPosition(unsigned char x, unsigned char y);
void OledFillScreen(unsigned char fiiData);
void OledPositionCleanScreen(unsigned char fillData, unsigned char line,
    unsigned char pos, unsigned char len);
void OledShowChar(unsigned char x, unsigned char y, unsigned char chr, unsigned char charSize);
void OledShowStr(unsigned char x, unsigned char y, unsigned char *chr, unsigned char charSize);
unsigned char *FlaotToString(double d, unsigned char *str);
void AllLedOff(void);
#endif