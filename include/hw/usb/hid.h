#ifndef HW_USB_HID_H
#define HW_USB_HID_H

/* HID interface requests */
#define HID_GET_REPORT   0xa101
#define HID_GET_IDLE     0xa102
#define HID_GET_PROTOCOL 0xa103
#define HID_SET_REPORT   0x2109
#define HID_SET_IDLE     0x210a
#define HID_SET_PROTOCOL 0x210b

/* HID descriptor types */
#define USB_DT_HID    0x21
#define USB_DT_REPORT 0x22
#define USB_DT_PHY    0x23

#endif
