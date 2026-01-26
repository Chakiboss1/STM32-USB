#include "Helpers/logger.h"
#include "usbd_framework.h"




int main(void)
{

	log_info("Program entry point.");
	usbd_initialize();
	for(;;);
}
