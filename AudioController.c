/*
 *
 * Heavily modified version 'MediaController' from LUFA Library.
 *
 * Copyright (c) 2012 Shigeru KANEMOTO
 *
 */

/*
  Copyright 2012  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include <stdbool.h>
#include <string.h>

#include <LUFA/Drivers/USB/USB.h>


/*
 *
 * Descriptors
 *
 */

const USB_Descriptor_Device_t PROGMEM deviceDescriptor = {
  .Header = {
    .Size = sizeof (USB_Descriptor_Device_t),
    .Type = DTYPE_Device
  },

  .USBSpecification = VERSION_BCD(01.10),
  .Class = USB_CSCP_NoDeviceClass,
  .SubClass = USB_CSCP_NoDeviceSubclass,
  .Protocol = USB_CSCP_NoDeviceProtocol,
  .Endpoint0Size = FIXED_CONTROL_ENDPOINT_SIZE,

  .VendorID = 0x2786,	// Switch Science
  .ProductID = 0xf000,	// for test
  .ReleaseNumber = VERSION_BCD(00.01),

  .ManufacturerStrIndex = 0x01,
  .ProductStrIndex = 0x02,
  .SerialNumStrIndex = NO_DESCRIPTOR,
  .NumberOfConfigurations = FIXED_NUM_CONFIGURATIONS
};

#define MY_EPADDR	(ENDPOINT_DIR_IN | 1)
#define MY_EPSIZE	1

// See http://msdn.microsoft.com/en-us/windows/hardware/gg462991.aspx
const USB_Descriptor_HIDReport_Datatype_t PROGMEM reportDescriptor[] = {
  HID_RI_USAGE_PAGE(8, 0x0c),	/* Consumer Page */
  HID_RI_USAGE(8, 0x01),	/* Consumer Controls */
  HID_RI_COLLECTION(8, 0x01),	/* Application */

    HID_RI_LOGICAL_MINIMUM(8, 0),
    HID_RI_LOGICAL_MAXIMUM(8, 1),
    HID_RI_USAGE(8, 0xE9),	/* Volume Up */
    HID_RI_USAGE(8, 0xEA),	/* Volume Down */
    HID_RI_REPORT_SIZE(8, 1),
    HID_RI_REPORT_COUNT(8, 2),
    HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    // Up/Down repeats on the host when you keep pressing.

    HID_RI_USAGE(8, 0xE2),	/* Mute */
    HID_RI_REPORT_COUNT(8, 1),
    HID_RI_INPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_RELATIVE),
    // Mute toggles the state on the host because this is 'Relative'.

    HID_RI_REPORT_COUNT(8, 5),
    HID_RI_INPUT(8, HID_IOF_CONSTANT),

  HID_RI_END_COLLECTION(0),
};

typedef struct {
  USB_Descriptor_Configuration_Header_t config;
  USB_Descriptor_Interface_t interface;
  USB_HID_Descriptor_HID_t hid;
  USB_Descriptor_Endpoint_t endpoint;
} MyDescriptorBlock_t;

const MyDescriptorBlock_t PROGMEM DescriptorBlock = {
  .config = {
    .Header = {
      .Size = sizeof (USB_Descriptor_Configuration_Header_t),
      .Type = DTYPE_Configuration
    },
    .TotalConfigurationSize = sizeof (MyDescriptorBlock_t),
    .TotalInterfaces = 1,
    .ConfigurationNumber = 1,
    .ConfigurationStrIndex = NO_DESCRIPTOR,
    .ConfigAttributes = \
	(USB_CONFIG_ATTR_RESERVED | USB_CONFIG_ATTR_SELFPOWERED),
    .MaxPowerConsumption = USB_CONFIG_POWER_MA(10)
  },

  .interface = {
    .Header = {
      .Size = sizeof (USB_Descriptor_Interface_t),
      .Type = DTYPE_Interface
    },
    .InterfaceNumber = 0,
    .AlternateSetting = 0,
    .TotalEndpoints = 1,
    .Class = HID_CSCP_HIDClass,
    .SubClass = HID_CSCP_NonBootSubclass,
    .Protocol = HID_CSCP_NonBootProtocol,
    .InterfaceStrIndex = NO_DESCRIPTOR
  },

  .hid = {
    .Header = {
      .Size = sizeof (USB_HID_Descriptor_HID_t),
      .Type = HID_DTYPE_HID
    },
    .HIDSpec = VERSION_BCD(01.11),
    .CountryCode = 0,
    .TotalReportDescriptors = 1,
    .HIDReportType = HID_DTYPE_Report,
    .HIDReportLength = sizeof (reportDescriptor)
  },

  .endpoint = {
    .Header = {
      .Size = sizeof (USB_Descriptor_Endpoint_t),
      .Type = DTYPE_Endpoint
    },
    .EndpointAddress = MY_EPADDR,
    .Attributes = \
	(EP_TYPE_INTERRUPT | ENDPOINT_ATTR_NO_SYNC | ENDPOINT_USAGE_DATA),
    .EndpointSize = MY_EPSIZE,
    .PollingIntervalMS = 5
  },
};

const USB_Descriptor_String_t PROGMEM LanguageString = {
  .Header = {
    .Size = USB_STRING_LEN(1),
    .Type = DTYPE_String
  },
  .UnicodeString = {
    LANGUAGE_ID_ENG
  }
};

#define USBSTRING_INITIALIZER(str)	\
  {					\
    .Header = {				\
      .Size = USB_STRING_LEN(sizeof (str) / sizeof (str[0]) - 1),	\
      .Type = DTYPE_String		\
    },					\
    .UnicodeString = L##str		\
  }

const USB_Descriptor_String_t PROGMEM
    ManufacturerString = USBSTRING_INITIALIZER("Switch Science");
const USB_Descriptor_String_t PROGMEM
    ProductString = USBSTRING_INITIALIZER("Audio Controller");


/*
 * The Report Data
 */
typedef struct {
  unsigned int volumeUp : 1;
  unsigned int volumeDown : 1;
  unsigned int mute : 1;
  unsigned int RESERVED : 5;
} ATTR_PACKED MyReportData_t;

uint8_t PrevReportDataBuffer[sizeof (MyReportData_t)];


/*
 * State Machine
 */
USB_ClassInfo_HID_Device_t stateMachine = {
  .Config = {
    .InterfaceNumber = 0,
    .ReportINEndpoint = {
      .Address = MY_EPADDR,
      .Size = MY_EPSIZE,
      .Banks = 1,
    },
    .PrevReportINBuffer = PrevReportDataBuffer,
    .PrevReportINBufferSize = sizeof (PrevReportDataBuffer),
  },
};


/*
 *
 * Event Handlers
 *
 * Put some LED control into them.
 *
 */

void
EVENT_USB_Device_Connect(void) {
}

void
EVENT_USB_Device_Disconnect(void) {
}

void
EVENT_USB_Device_ConfigurationChanged(void) {
  bool ok;
  ok = HID_Device_ConfigureEndpoints(&stateMachine);
  USB_Device_EnableSOFEvents();
}

void
EVENT_USB_Device_ControlRequest(void) {
  HID_Device_ProcessControlRequest(&stateMachine);
}

void
EVENT_USB_Device_StartOfFrame(void) {
  HID_Device_MillisecondElapsed(&stateMachine);
}


/*
 *
 * Required Callbacks
 *
 */

uint16_t CALLBACK_USB_GetDescriptor(
  const uint16_t wValue,
  const uint8_t wIndex,
  const void** const DescriptorAddress
) {
  const uint8_t type = (wValue >> 8);
  const uint8_t number = (wValue & 0xff);
  const void* address = NULL;
  uint16_t length = NO_DESCRIPTOR;

  switch (type) {
    case DTYPE_Device:
      address = &deviceDescriptor;
      length = sizeof (USB_Descriptor_Device_t);
      break;

    case DTYPE_Configuration:
      address = &DescriptorBlock;
      length = sizeof (MyDescriptorBlock_t);
      break;

    case DTYPE_String:
      switch (number) {
	case 0x00:
	  address = &LanguageString;
	  length = LanguageString.Header.Size;
	  break;
	case 0x01:
	  address = &ManufacturerString;
	  length = ManufacturerString.Header.Size;
	  break;
	case 0x02:
	  address = &ProductString;
	  length = ProductString.Header.Size;
	  break;
      }
      break;

    case HID_DTYPE_HID:
      address = &DescriptorBlock.hid;
      length = sizeof (DescriptorBlock.hid);
      break;

    case HID_DTYPE_Report:
      address = &reportDescriptor;
      length = sizeof (reportDescriptor);
      break;
  }

  *DescriptorAddress = address;
  return length;
}

bool
CALLBACK_HID_Device_CreateHIDReport(
  USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo,
  uint8_t* const ReportID,
  const uint8_t ReportType,
  void* ReportData,
  uint16_t* const ReportSize
) {
  MyReportData_t* report = (MyReportData_t*)ReportData;
  report->volumeUp = ((PIND & _BV(PIND5)) == 0);
  report->volumeDown = ((PIND & _BV(PIND6)) == 0);
  report->mute = ((PIND & _BV(PIND7)) == 0);
  *ReportSize = sizeof (MyReportData_t);
  return false;	// don't force the report
}

void
CALLBACK_HID_Device_ProcessHIDReport(
  USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo,
  const uint8_t ReportID,
  const uint8_t ReportType,
  const void* ReportData,
  const uint16_t ReportSize
) {
  // Required only for Host.
}


/*
 *
 * Main
 *
 */

int
main(void) {
  MCUSR &= ~(1 << WDRF);
  wdt_disable();
  clock_prescale_set(clock_div_1);
  USB_Init();
  PORTD |= _BV(PORTD5) | _BV(PORTD6) | _BV(PORTD7);	// pull up

  sei();
  for (;;) {
    HID_Device_USBTask(&stateMachine);
    USB_USBTask();
  }
}
