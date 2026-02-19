#include "usbd_driver.h"
#include "usb_standards.h"
#include "strings.h"

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

	// Unmasks transfer completed interrupts for all endpoints.
	SET_BIT(USB_OTG_FS_DEVICE->DOEPMSK, USB_OTG_DOEPMSK_XFRCM);
	SET_BIT(USB_OTG_FS_DEVICE->DIEPMSK, USB_OTG_DIEPMSK_XFRCM);
}

static void set_device_address(uint8_t address)
{
	MODIFY_REG(USB_OTG_FS_DEVICE->DCFG,
			USB_OTG_DCFG_DAD,
			_VAL2FLD(USB_OTG_DCFG_DAD,address));
}

static void refresh_fifo_start_addresses()
{
	// The first changeable start address begins after the region of RxFIFO.
	uint16_t start_address = _FLD2VAL(USB_OTG_GRXFSIZ_RXFD, USB_OTG_FS->GRXFSIZ) * 4;

	// Updates the start address of the TxFIFO0.
	MODIFY_REG(USB_OTG_FS->DIEPTXF0_HNPTXFSIZ,
		USB_OTG_TX0FSA,
		_VAL2FLD(USB_OTG_TX0FSA, start_address)
	);

	// The next start address is after where the last TxFIFO ends.
	start_address += _FLD2VAL(USB_OTG_TX0FD, USB_OTG_FS->DIEPTXF0_HNPTXFSIZ) * 4;

	// Updates the start addresses of the rest TxFIFOs.
	for (uint8_t txfifo_number = 0; txfifo_number < ENDPOINT_COUNT - 1; txfifo_number++)
	{
		MODIFY_REG(USB_OTG_FS->DIEPTXF[txfifo_number],
			USB_OTG_NPTXFSA,
			_VAL2FLD(USB_OTG_NPTXFSA, start_address)
		);

		start_address += _FLD2VAL(USB_OTG_NPTXFD, USB_OTG_FS->DIEPTXF[txfifo_number]) * 4;
	}
}
static void configure_rxfifo_size(uint16_t size)
{
	// Considers the space required to save status packets in RxFIFO and gets the size in term of 32-bit words.
	size = 10 + (2 * ((size / 4) + 1));

	// Configures the depth of the FIFO.
	MODIFY_REG(USB_OTG_FS->GRXFSIZ,
		USB_OTG_GRXFSIZ_RXFD,
		_VAL2FLD(USB_OTG_GRXFSIZ_RXFD, size)
	);

	refresh_fifo_start_addresses();
}

static void configure_txfifo_size(uint8_t endpoint_number, uint16_t size)
{
	// Gets the FIFO size in term of 32-bit words.
	size = (size + 3) / 4;

	// Configures the depth of the TxFIFO.
	if (endpoint_number == 0)
	{
		MODIFY_REG(USB_OTG_FS_GLOBAL->DIEPTXF0_HNPTXFSIZ,
			USB_OTG_TX0FD,
			_VAL2FLD(USB_OTG_TX0FD, size)
		);
	}
	else
	{
		MODIFY_REG(USB_OTG_FS_GLOBAL->DIEPTXF[endpoint_number - 1],
			USB_OTG_NPTXFD,
			_VAL2FLD(USB_OTG_NPTXFD, size)
		);
	}
	refresh_fifo_start_addresses();
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

/** \brief Pops data from the RxFIFO and stores it in the buffer.
 * \param buffer Pointer to the buffer, in which the popped data will be stored.
 * \param size Count of bytes to be popped from the dedicated RxFIFO memory.
 */
static void read_packet(void *buffer, uint16_t size)
{
	// Note: There is only one RxFIFO.
	uint32_t *fifo = FIFO(0);

	for (; size >= 4; size -=4, buffer += 4)
	{
		// Pops one 32-bit word of data (until there is less than one word remaining).
		uint32_t data = *fifo;
		// Stores the data in the buffer.
		*((uint32_t*)buffer) = data;
	}

	if (size > 0)
	{
		// Pops the last remaining bytes (which are less than one word).
		uint32_t data = *fifo;

		for(; size > 0; size--, buffer++, data >>= 8)
		{
			// Stores the data in the buffer with the correct alignment.
			*((uint8_t*)buffer) = 0xFF & data;
		}
	}
}

/** \brief Pushes a packet into the TxFIFO of an IN endpoint.
 * \param endpoint_number The number of the endpoint, to which the data will be written.
 * \param buffer Pointer to the buffer contains the data to be written to the endpoint.
 * \param size The size of data to be written in bytes.
 */
static void write_packet(uint8_t endpoint_number, void const *buffer, uint16_t size)
{
	uint32_t *fifo = FIFO(endpoint_number);
	USB_OTG_INEndpointTypeDef *in_endpoint = IN_ENDPOINT(endpoint_number);

	// Configures the transmission (1 packet that has `size` bytes).
	MODIFY_REG(in_endpoint->DIEPTSIZ,
		USB_OTG_DIEPTSIZ_PKTCNT | USB_OTG_DIEPTSIZ_XFRSIZ,
		_VAL2FLD(USB_OTG_DIEPTSIZ_PKTCNT, 1) | _VAL2FLD(USB_OTG_DIEPTSIZ_XFRSIZ, size)
	);

	// Enables the transmission after clearing both STALL and NAK of the endpoint.
	MODIFY_REG(in_endpoint->DIEPCTL,
		USB_OTG_DIEPCTL_STALL,
		USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA
	);

	// Gets the size in term of 32-bit words (to avoid integer overflow in the loop).
	size = (size + 3) / 4;

	for (; size > 0; size--, buffer += 4)
	{
		// Pushes the data to the TxFIFO.
		*fifo = *((uint32_t *)buffer);
	}
}


/** \brief Flushes the RxFIFO of all OUT endpoints.
 */
static void flush_rxfifo()
{
	SET_BIT(USB_OTG_FS->GRSTCTL, USB_OTG_GRSTCTL_RXFFLSH);
}

/** \brief Flushes the TxFIFO of an IN endpoint.
 * \param endpoint_number The number of an IN endpoint to flush its TxFIFO.
 */
static void flush_txfifo(uint8_t endpoint_number)
{
	// Sets the number of the TxFIFO to be flushed and then triggers the flush.
	MODIFY_REG(USB_OTG_FS->GRSTCTL,
		USB_OTG_GRSTCTL_TXFNUM,
		_VAL2FLD(USB_OTG_GRSTCTL_TXFNUM, endpoint_number) | USB_OTG_GRSTCTL_TXFFLSH
	);
}


static void configure_endpoint0(uint8_t endpoint_size){
	SET_BIT(USB_OTG_FS_DEVICE->DAINTMSK, 1 << 0 | 1 << 16);

	MODIFY_REG(IN_ENDPOINT(0)->DIEPCTL,
		USB_OTG_DIEPCTL_MPSIZ,
		USB_OTG_DIEPCTL_USBAEP | _VAL2FLD(USB_OTG_DIEPCTL_MPSIZ, endpoint_size) | USB_OTG_DIEPCTL_SNAK
	);

	// Clears NAK, and enables endpoint data transmission.
	SET_BIT(OUT_ENDPOINT(0)->DOEPCTL,
		USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK
	);

	// Note: 64 bytes is the maximum packet size for full speed USB devices.
	configure_rxfifo_size(64);
	configure_txfifo_size(0, endpoint_size);

}

static void configure_in_endpoint(uint8_t endpoint_number, UsbEndpointType endpoint_type, uint16_t endpoint_size)
{
	// Unmasks all interrupts of the targeted IN endpoint.
	SET_BIT(USB_OTG_FS_DEVICE->DAINTMSK, 1 << endpoint_number);

	// Activates the endpoint, sets endpoint handshake to NAK (not ready to send data), sets DATA0 packet identifier,
	// configures its type, its maximum packet size, and assigns it a TxFIFO.
	MODIFY_REG(IN_ENDPOINT(endpoint_number)->DIEPCTL,
		USB_OTG_DIEPCTL_MPSIZ | USB_OTG_DIEPCTL_EPTYP | USB_OTG_DIEPCTL_TXFNUM,
		USB_OTG_DIEPCTL_USBAEP | _VAL2FLD(USB_OTG_DIEPCTL_MPSIZ, endpoint_size) | USB_OTG_DIEPCTL_SNAK |
		_VAL2FLD(USB_OTG_DIEPCTL_EPTYP, endpoint_type) | _VAL2FLD(USB_OTG_DIEPCTL_TXFNUM, endpoint_number) | USB_OTG_DIEPCTL_SD0PID_SEVNFRM
	);
	configure_txfifo_size(endpoint_number, endpoint_size);

}

static void deconfigure_endpoint(uint8_t endpoint_number)
{
    USB_OTG_INEndpointTypeDef *in_endpoint = IN_ENDPOINT(endpoint_number);
    USB_OTG_OUTEndpointTypeDef *out_endpoint = OUT_ENDPOINT(endpoint_number);

	// Masks all interrupts of the targeted IN and OUT endpoints.
	CLEAR_BIT(USB_OTG_FS_DEVICE->DAINTMSK,
		(1 << endpoint_number) | (1 << 16 << endpoint_number)
	);

	// Clears all interrupts of the endpoint.
	SET_BIT(in_endpoint->DIEPINT, 0x28FB);
    SET_BIT(out_endpoint->DOEPINT, 0x303B);

	// Disables the endpoints if possible.
    if (in_endpoint->DIEPCTL & USB_OTG_DIEPCTL_EPENA)
    {
		// Disables endpoint transmission.
		SET_BIT(in_endpoint->DIEPCTL, USB_OTG_DIEPCTL_EPDIS);
    }

	// Deactivates the endpoint.
	CLEAR_BIT(in_endpoint->DIEPCTL, USB_OTG_DIEPCTL_USBAEP);

    if (endpoint_number != 0)
    {
		if (out_endpoint->DOEPCTL & USB_OTG_DOEPCTL_EPENA)
		{
			// Disables endpoint transmission.
			SET_BIT(out_endpoint->DOEPCTL, USB_OTG_DOEPCTL_EPDIS);
		}

		// Deactivates the endpoint.
		CLEAR_BIT(out_endpoint->DOEPCTL, USB_OTG_DOEPCTL_USBAEP);
    }
	// Flushes the FIFOs.
	flush_txfifo(endpoint_number);
	flush_rxfifo();

}
/** \brief Updates the start addresses of all FIFOs according to the size of each FIFO.
 */
static void usbrst_handler()
{
	log_info("USB reset signal was detected.");

	for (uint8_t i = 0; i <= ENDPOINT_COUNT; i++)
	{
		deconfigure_endpoint(i);
	}

	usb_events.on_usb_reset_received();


}

static void enumdne_handler()
{
	log_info("USB device speed enumeration done.");
	configure_endpoint0(8);
}
static void rxflvl_handler()
{
	 // Pops the status information word from the RxFIFO.
	uint32_t receive_status = USB_OTG_FS_GLOBAL->GRXSTSP;

	// The endpoint that received the data.
	uint8_t endpoint_number = _FLD2VAL(USB_OTG_GRXSTSP_EPNUM, receive_status);
	// The count of bytes in the received packet.
	uint16_t bcnt = _FLD2VAL(USB_OTG_GRXSTSP_BCNT, receive_status);
	// The status of the received packet.
	uint16_t pktsts = _FLD2VAL(USB_OTG_GRXSTSP_PKTSTS, receive_status);

	switch (pktsts)
	{
	case 0x06: // SETUP packet (includes data).
    	usb_events.on_setup_data_received(endpoint_number, bcnt);
    	break;
    case 0x02: // OUT packet (includes data).
    	// ToDo
		break;
    case 0x04: // SETUP stage has completed.
    	// Re-enables the transmission on the endpoint.
        SET_BIT(OUT_ENDPOINT(endpoint_number)->DOEPCTL,
			USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA);
    	break;
    case 0x03: // OUT transfer has completed.
    	// Re-enables the transmission on the endpoint.
        SET_BIT(OUT_ENDPOINT(endpoint_number)->DOEPCTL,
			USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA);
    	break;
	}
}
/** \brief Handles the interrupt raised when an IN endpoint has a raised interrupt.
 */
static void iepint_handler()
{
	// Finds the endpoint caused the interrupt.
	uint8_t endpoint_number = ffs(USB_OTG_FS_DEVICE->DAINT) - 1;

    if (IN_ENDPOINT(endpoint_number)->DIEPINT & USB_OTG_DIEPINT_XFRC)
    {
        usb_events.on_in_transfer_completed(endpoint_number);
        // Clears the interrupt flag.
        SET_BIT(IN_ENDPOINT(endpoint_number)->DIEPINT, USB_OTG_DIEPINT_XFRC);
    }
}

/** \brief Handles the interrupt raised when an OUT endpoint has a raised interrupt.
 */
static void oepint_handler()
{
	// Finds the endpoint caused the interrupt.
	uint8_t endpoint_number = ffs(USB_OTG_FS_DEVICE->DAINT >> 16) - 1;

    if (OUT_ENDPOINT(endpoint_number)->DOEPINT & USB_OTG_DOEPINT_XFRC)
    {
        usb_events.on_out_transfer_completed(endpoint_number);
        // Clears the interrupt;
        SET_BIT(OUT_ENDPOINT(endpoint_number)->DOEPINT, USB_OTG_DOEPINT_XFRC);
    }
}

static void gintsts_handler()
{
	volatile uint32_t gintsts = USB_OTG_FS_GLOBAL->GINTSTS;

	if (gintsts & USB_OTG_GINTSTS_USBRST)
	{
		usbrst_handler();
		// Clears the interrupt.
		SET_BIT(USB_OTG_FS_GLOBAL->GINTSTS, USB_OTG_GINTSTS_USBRST);
	}
	else if (gintsts & USB_OTG_GINTSTS_ENUMDNE)
	{
		enumdne_handler();
		// Clears the interrupt.
		SET_BIT(USB_OTG_FS_GLOBAL->GINTSTS, USB_OTG_GINTSTS_ENUMDNE);
	}
	else if (gintsts & USB_OTG_GINTSTS_RXFLVL)
	{
	rxflvl_handler();
		// Clears the interrupt.
		SET_BIT(USB_OTG_FS_GLOBAL->GINTSTS, USB_OTG_GINTSTS_RXFLVL);
	}
	else if (gintsts & USB_OTG_GINTSTS_IEPINT)
	{
		iepint_handler();
		// Clears the interrupt.
		SET_BIT(USB_OTG_FS_GLOBAL->GINTSTS, USB_OTG_GINTSTS_IEPINT);
	}
	else if (gintsts & USB_OTG_GINTSTS_OEPINT)
	{
		oepint_handler();
//		 Clears the interrupt.
		SET_BIT(USB_OTG_FS_GLOBAL->GINTSTS, USB_OTG_GINTSTS_OEPINT);
	}
	usb_events.on_usb_polled();
}

const UsbDriver usb_driver = {
	.initialize_core = &initialize_core,
	.initialize_gpio_pins = &initialize_gpio_pins,
	.set_device_address = &set_device_address,
	.connect = &connect,
	.disconnect = &disconnect,
	.flush_rxfifo = &flush_rxfifo,
	.flush_txfifo = &flush_txfifo,
	.configure_in_endpoint = &configure_in_endpoint,
	.read_packet = &read_packet,
	.write_packet = &write_packet,
	.poll=&gintsts_handler
};

