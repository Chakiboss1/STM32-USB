#ifndef USBD_DRIVER_H_
#define USBD_DRIVER_H_

#include "stm32f4xx.h"

/* USB OTG FS base pointers */
#define USB_OTG_FS_GLOBAL   ((USB_OTG_GlobalTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_GLOBAL_BASE))
#define USB_OTG_FS_DEVICE   ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))
#define USB_OTG_FS_PCGCCTL  ((__IO uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_PCGCCTL_BASE))

#endif /* USBD_DRIVER_H_ */
