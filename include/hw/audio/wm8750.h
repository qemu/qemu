/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_DAC_WM8750_H
#define HW_DAC_WM8750_H


#define TYPE_WM8750 "wm8750"
#define TYPE_MV88W8618_AUDIO "mv88w8618_audio"

typedef void data_req_cb(void *opaque, int free_out, int free_in);

void wm8750_data_req_set(DeviceState *dev, data_req_cb *data_req, void *opaque);
void wm8750_dac_dat(void *opaque, uint32_t sample);
uint32_t wm8750_adc_dat(void *opaque);
void *wm8750_dac_buffer(void *opaque, int samples);
void wm8750_dac_commit(void *opaque);
void wm8750_set_bclk_in(void *opaque, int new_hz);

#endif
