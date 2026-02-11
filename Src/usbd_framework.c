#include "usbd_framework.h"
#include "usbd_driver.h"
#include "usb_device.h"
#include "usbd_descriptors.h"
#include "usb_standards.h"

static UsbDevice *usbd_handle;

void usbd_initialize(UsbDevice *usb_device)
{

	usbd_handle=usb_device;
	usb_driver.initialize_gpio_pins();
	usb_driver.initialize_core();
	usb_driver.connect();
}
static void process_standard_device_request()
 {
	UsbRequest const *request = usbd_handle->ptr_out_buffer;

	switch (request->bRequest)
	{
	case USB_STANDARD_GET_DESCRIPTOR:
		log_info("Standard Get Descriptor request received");
		const uint8_t descriptor_type = request->wValue >> 8;
		const uint16_t descriptor_length=request->wValue;

		switch (descriptor_type)
		{
		case USB_DESCRIPTOR_TYPE_DEVICE:
			log_info("- Get Device Descriptor.");
			usbd_handle->ptr_in_buffer = &device_descriptor;
			usbd_handle->in_data_size = descriptor_length;

			log_info("Switching control transfer stage to IN-DATA.");
			usbd_handle->control_transfer_stage = USB_CONTROL_STAGE_DATA_IN;
			break;
		}
		break;

	}

}

static void process_request(){
	UsbRequest const *request = usbd_handle->ptr_out_buffer;

		switch(request->bmRequestType & (USB_BM_REQUEST_TYPE_TYPE_MASK | USB_BM_REQUEST_TYPE_RECIPIENT_MASK))
		{
		case USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIPIENT_DEVICE:
//			process_standard_device_request();
//		break;
//		case USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIPIENT_INTERFACE:
//			process_class_interface_request();
//		break;
//		case USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIPIENT_INTERFACE:
//			process_standard_interface_request();
		break;
		}
}
static void process_control_transfert_stage()
{
	switch(usbd_handle->control_transfer_stage)
	{
	case USB_CONTROL_STAGE_SETUP:
		break;
	case USB_CONTROL_STAGE_DATA_IN:
		break;
	}
}

void usbd_poll(){
	usb_driver.poll();
}

static void usb_reset_received_handler()
{
	usbd_handle->in_data_size = 0;
	usbd_handle->out_data_size = 0;
	usbd_handle->configuration_value = 0;
	usbd_handle->device_state = USB_DEVICE_STATE_DEFAULT;
	usbd_handle->control_transfer_stage = USB_CONTROL_STAGE_SETUP;
	usb_driver.set_device_address(0);
}
static void usb_polled_handler()
{
	process_control_transfert_stage();
}
static void setup_data_received_handler (uint8_t endpoint_number, uint16_t byte_coun){
	usb_driver.read_packet(usbd_handle->ptr_out_buffer,	byte_coun);

	// prints out the received data
	log_debug_array("SETUP data : ",usbd_handle->ptr_out_buffer, byte_coun);

	process_request();
}

UsbEvents usb_events={
		.on_usb_reset_received = &usb_reset_received_handler,
		.on_setup_data_received=&setup_data_received_handler,
		.on_usb_polled=&usb_polled_handler
};
