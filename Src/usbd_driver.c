#include "usbd_driver.h"

void initialize_gpio_pins() {
    // Enables the clock for GPIOA
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN);

    // Sets alternate function 10 for: PA11 (-), and PA12 (+).
    MODIFY_REG(GPIOA->AFR[1], GPIO_AFRH_AFSEL11 | GPIO_AFRH_AFSEL12,
            _VAL2FLD(GPIO_AFRH_AFSEL11, 0xA) | _VAL2FLD(GPIO_AFRH_AFSEL12, 0xA));

    // Configures USB pins (in GPIOA) to work in alternate function mode.
    MODIFY_REG(GPIOA->MODER, GPIO_MODER_MODER11 | GPIO_MODER_MODER12,
            _VAL2FLD(GPIO_MODER_MODER11, 2) | _VAL2FLD(GPIO_MODER_MODER12, 2));
}

void initialize_core(void)
{
    /* 1. Enable USB FS clock */
    SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_OTGFSEN);

	MODIFY_REG(USB_OTG_FS_DEVICE->DCFG,
		USB_OTG_DCFG_DSPD,
		_VAL2FLD(USB_OTG_DCFG_DSPD, 0x03)
	);

    /* 3. Configuration GUSBCFG */
    // AJOUT VITAL : USB_OTG_GUSBCFG_PHYSEL (Bit 6)
    // Sans ce bit, le contrôleur ne sait pas qu'il doit utiliser le PHY interne !
    MODIFY_REG(USB_OTG_FS_GLOBAL->GUSBCFG,
        USB_OTG_GUSBCFG_FDMOD | USB_OTG_GUSBCFG_TRDT | USB_OTG_GUSBCFG_PHYSEL,
        USB_OTG_GUSBCFG_FDMOD | _VAL2FLD(USB_OTG_GUSBCFG_TRDT, 0x9) | USB_OTG_GUSBCFG_PHYSEL
    );

    /* 4. Power Down & VBUS - LA CORRECTION MAJEURE EST ICI */
    // On active le Transceiver
    SET_BIT(USB_OTG_FS_GLOBAL->GCCFG, USB_OTG_GCCFG_PWRDWN);

    // On DÉSACTIVE la détection hardware VBUS (VBDEN=0) pour éviter les problèmes de câblage PA9
    CLEAR_BIT(USB_OTG_FS_GLOBAL->GCCFG, USB_OTG_GCCFG_VBDEN);

    // FORCE B-SESSION VALID (Override logiciel)
    // Cela dit au contrôleur : "Crois-moi, le câble est branché, active-toi."
    // C'est l'astuce utilisée dans tous les drivers baremetal robustes.
    USB_OTG_FS_GLOBAL->GOTGCTL |= (USB_OTG_GOTGCTL_BVALOEN | USB_OTG_GOTGCTL_BVALOVAL);

    /* 5. Device Config */
    MODIFY_REG(USB_OTG_FS_DEVICE->DCFG,
        USB_OTG_DCFG_DSPD | USB_OTG_DCFG_PFIVL,
        _VAL2FLD(USB_OTG_DCFG_PFIVL, 0x03) | _VAL2FLD(USB_OTG_DCFG_DSPD, 0x03)
    );



    /* 7. Interrupts */
    SET_BIT(USB_OTG_FS_GLOBAL->GINTMSK,
    		USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM | USB_OTG_GINTMSK_SOFM |
    		USB_OTG_GINTMSK_USBSUSPM | USB_OTG_GINTMSK_WUIM | USB_OTG_GINTMSK_IEPINT |
    		USB_OTG_GINTSTS_OEPINT | USB_OTG_GINTMSK_RXFLVLM
    	);

    WRITE_REG(USB_OTG_FS_GLOBAL->GINTSTS, 0xFFFFFFFF);
    SET_BIT(USB_OTG_FS_GLOBAL->GAHBCFG, USB_OTG_GAHBCFG_GINT);
}

void connect(void)
{
    SET_BIT(USB_OTG_FS_GLOBAL->GCCFG, USB_OTG_GCCFG_PWRDWN);
    // C'est cette ligne qui dit au PC "Je suis là" (Pull-up sur D+)
    CLEAR_BIT(USB_OTG_FS_DEVICE->DCTL, USB_OTG_DCTL_SDIS);
}

void disconnect(void)
{
    SET_BIT(USB_OTG_FS_DEVICE->DCTL, USB_OTG_DCTL_SDIS);
    CLEAR_BIT(USB_OTG_FS_GLOBAL->GCCFG, USB_OTG_GCCFG_PWRDWN);
}
