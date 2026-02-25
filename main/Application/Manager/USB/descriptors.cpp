#include <class/msc/msc.h>
#include <class/video/video.h>

#include "descriptors.h"

/* MSC Endpoint numbers */
#define EPNUM_MSC_OUT                   0x01
#define EPNUM_MSC_IN                    0x81

/* CDC Endpoint numbers */
#define EPNUM_CDC_OUT                   0x02
#define EPNUM_CDC_NOTIFY				0x82
#define EPNUM_CDC_IN                    0x83

/* UVC Endpoint numbers */
#define EPNUM_UVC_VIDEO_IN              0x84

/** @brief UVC MJPEG Video Capture Descriptor
 *
 * +------------------------------+-------+
 * | Descriptor                   | Bytes |
 * +------------------------------+-------+
 * | IAD (Interface Association)  | 8     |
 * | VideoControl Interface       | 9     |
 * | Class-Specific VC Header     | 13    |
 * | Camera Terminal              | 18    |
 * | Output Terminal              | 9     |
 * | VideoStreaming Interface Alt0| 9     |
 * | VS Input Header              | 13    |
 * | MJPEG Format Descriptor      | 11    |
 * | MJPEG Frame Descriptor       | 38    |
 * | Color Matching Descriptor    | 6     |
 * | VideoStreaming Interface Alt1| 9     |
 * | Isochronous Endpoint         | 7     |
 * +------------------------------+-------+
 * | Total                        | 150   |
 */
#define TUD_UVC_DESC_LEN ( \
    TUD_VIDEO_DESC_IAD_LEN \
    /* control */ \
    + TUD_VIDEO_DESC_STD_VC_LEN \
    + TUD_VIDEO_DESC_CS_VC_LEN \
	/* bInCollection */ \
	+ 1 \
    + TUD_VIDEO_DESC_CAMERA_TERM_LEN \
    + TUD_VIDEO_DESC_OUTPUT_TERM_LEN \
    /* Interface 1, Alternate 0 */ \
    + TUD_VIDEO_DESC_STD_VS_LEN \
	/* bNumFormats x bControlSize */ \
    + (TUD_VIDEO_DESC_CS_VS_IN_LEN + 1) \
    + TUD_VIDEO_DESC_CS_VS_FMT_MJPEG_LEN \
    + TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_CONT_LEN \
    + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN \
    /* Interface 1, Alternate 1 */ \
    + TUD_VIDEO_DESC_STD_VS_LEN \
	/* Endpoint */ \
    + 7 \
)

/**
 * @brief 				UVC MJPEG Video Capture Descriptor Macro
 * @param _itf          Interface number (will use _itf and _itf + 1)
 * @param _stridx       String descriptor index
 * @param _epin         Endpoint IN address (e.g., 0x81 for EP1 IN)
 * @param _width        Frame width in pixels
 * @param _height       Frame height in pixels
 * @param _fps          Frame rate in frames per second
 * @param _epsize       Maximum packet size for isochronous endpoint
 */
#define TUD_VIDEO_CAPTURE_DESCRIPTOR_MJPEG(_itf, _stridx, _epin, _width, _height, _fps, _epsize) \
	/* Interface Association Descriptor (IAD) */ \
	TUD_VIDEO_DESC_IAD(_itf, 0x02, _stridx), \
	\
	/* Video Control Interface (Interface _itf, Alternate 0) */ \
	TUD_VIDEO_DESC_STD_VC(_itf, 0, _stridx), \
		/* Class-Specific VC Interface Header */ \
		TUD_VIDEO_DESC_CS_VC( \
			0x0150,                                                      /* bcdUVC: UVC 1.5 */ \
			TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN, /* wTotalLength */ \
			27000000,                                                    /* dwClockFrequency: 27 MHz */ \
			_itf + 1),                                                   /* baInterfaceNr: VideoStreaming interface */ \
		/* Input Terminal Descriptor (Camera) */ \
		TUD_VIDEO_DESC_CAMERA_TERM( \
			0x01,                                                        /* bTerminalID */ \
			0,                                                           /* wTerminalType: Vendor specific (0x0000) */ \
			0,                                                           /* bAssocTerminal: No association */ \
			0,                                                           /* wObjectiveFocalLengthMin */ \
			0,                                                           /* wObjectiveFocalLengthMax */ \
			0,                                                           /* wObjectiveFocalLength */ \
			0),                                                          /* bmControls: No controls */ \
		/* Output Terminal Descriptor */ \
		TUD_VIDEO_DESC_OUTPUT_TERM( \
			0x02,                                                        /* bTerminalID */ \
			VIDEO_TT_STREAMING,                                          /* wTerminalType: USB streaming */ \
			0,                                                           /* bAssocTerminal: No association */ \
			1,                                                           /* bSourceID: Connected to Input Terminal 1 */ \
			0),                                                          /* iTerminal: No string descriptor */ \
	\
	/* Video Streaming Interface (Interface _itf+1, Alternate 0 - Zero Bandwidth) */ \
	TUD_VIDEO_DESC_STD_VS(_itf + 1, 0, 0, _stridx), \
		/* Class-Specific VS Interface Input Header */ \
		TUD_VIDEO_DESC_CS_VS_INPUT( \
			1,                                                           /* bNumFormats: 1 format (MJPEG) */ \
			TUD_VIDEO_DESC_CS_VS_FMT_MJPEG_LEN \
			+ TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_CONT_LEN \
			+ TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN,                   /* wTotalLength */ \
			_epin,                                                       /* bEndpointAddress */ \
			0,                                                           /* bmInfo: No dynamic format change */ \
			0x02,                                                        /* bTerminalLink: Connected to Output Terminal 2 */ \
			0,                                                           /* bStillCaptureMethod: No still capture */ \
			0,                                                           /* bTriggerSupport: No hardware trigger */ \
			0,                                                           /* bTriggerUsage */ \
			0),                                                          /* bmaControls: No per-format controls */ \
		/* MJPEG Format Descriptor */ \
		TUD_VIDEO_DESC_CS_VS_FMT_MJPEG( \
			1,                                                           /* bFormatIndex */ \
			1,                                                           /* bNumFrameDescriptors: 1 frame resolution */ \
			0,                                                           /* bmFlags: Fixed size samples */ \
			1,                                                           /* bDefaultFrameIndex */ \
			0,                                                           /* bAspectRatioX: Not specified */ \
			0,                                                           /* bAspectRatioY: Not specified */ \
			0,                                                           /* bmInterlaceFlags: Non-interlaced */ \
			0),                                                          /* bCopyProtect: No restrictions */ \
			/* MJPEG Frame Descriptor */ \
			TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_CONT( \
				1,                                                       /* bFrameIndex */ \
				0,                                                       /* bmCapabilities: Still image not supported */ \
				_width,                                                  /* wWidth: Frame width */ \
				_height,                                                 /* wHeight: Frame height */ \
				_width * _height * 16,                                   /* dwMinBitRate: bits/sec */ \
				_width * _height * 16 * _fps,                            /* dwMaxBitRate: bits/sec */ \
				_width * _height * 16 / 8,                               /* dwMaxVideoFrameBufferSize: bytes */ \
				(10000000 / _fps),                                       /* dwDefaultFrameInterval: 100ns units */ \
				(10000000 / _fps),                                       /* dwMinFrameInterval: 100ns units */ \
				(10000000 / _fps) * _fps,                                /* dwMaxFrameInterval: 100ns units */ \
				(10000000 / _fps)),                                      /* dwFrameIntervalStep: 100ns units */ \
			/* Color Matching Descriptor */ \
			TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING( \
				VIDEO_COLOR_PRIMARIES_BT709,                             /* bColorPrimaries: BT.709, sRGB */ \
				VIDEO_COLOR_XFER_CH_BT709,                               /* bTransferCharacteristics: BT.709 */ \
				VIDEO_COLOR_COEF_SMPTE170M),                             /* bMatrixCoefficients: SMPTE 170M */ \
	\
	/* Video Streaming Interface (Interface _itf+1, Alternate 1 - Operational) */ \
	TUD_VIDEO_DESC_STD_VS(_itf + 1, 1, 1, _stridx), \
		/* Isochronous Video Data Endpoint */ \
		TUD_VIDEO_DESC_EP_ISO(_epin, _epsize, 1)                         /* bInterval: 1 (one frame per microframe) */

#ifdef CONFIG_TINYUSB_UVC_ENABLED
	#define _UVC_DESC_LEN    TUD_UVC_DESC_LEN
#else
	#define _UVC_DESC_LEN    0
#endif

#ifdef CONFIG_TINYUSB_CDC_ENABLED
	#define _CDC_DESC_LEN    TUD_CDC_DESC_LEN
#else
	#define _CDC_DESC_LEN    0
#endif

#if(defined CONFIG_TINYUSB_MSC_ENABLED) && (CONFIG_TINYUSB_MSC_ENABLED == 1)
	#define _MSC_DESC_LEN    TUD_MSC_DESC_LEN
#else
	#define _MSC_DESC_LEN    0
#endif

#define DESCRIPTOR_TOTAL_LENGTH ( \
    TUD_CONFIG_DESC_LEN \
    + _UVC_DESC_LEN     \
    + _CDC_DESC_LEN     \
    + _MSC_DESC_LEN     \
)

const tusb_desc_device_t descriptor_dev_default = {
    .bLength = sizeof(descriptor_dev_default),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    /* Use Interface Association Descriptor (IAD) for CDC or VIDEO
     As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1) */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = 0x55AA,
    .bcdDevice = CONFIG_TINYUSB_DESC_BCD_DEVICE,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

/** @brief 	USB Configuration mit UVC Video + CDC Metadaten-Stream
 * 			Interface 0: Video Control
 * 			Interface 1: Video Streaming (Endpoint 0x81)
 * 			Interface 2: CDC Control (Endpoint 0x82 - Notification)
 * 			Interface 3: CDC Data (Endpoint 0x83 OUT, 0x84 IN)
 * 			Interface 4: MSC Control (Endpoint 0x01 - OUT, 0x84 - IN)
 */
uint8_t const descriptor_fs_cfg_default[] = {
    TUD_CONFIG_DESCRIPTOR(1, 5, 0,
		DESCRIPTOR_TOTAL_LENGTH, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 100),

#ifdef CONFIG_TINYUSB_UVC_ENABLED
    /* UVC Video Interface (Interface 0 - 1) */
    TUD_VIDEO_CAPTURE_DESCRIPTOR_MJPEG(0, 0, EPNUM_UVC_VIDEO_IN, 640, 480, 15, CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE),
#endif

#ifdef CONFIG_TINYUSB_CDC_ENABLED
    /* CDC Metadata Interface (Interface 2 - 3) */
    TUD_CDC_DESCRIPTOR(2, 0, EPNUM_CDC_NOTIFY, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif

#if(defined CONFIG_TINYUSB_MSC_ENABLED) && (CONFIG_TINYUSB_MSC_ENABLED == 1)
    /* MSC Interface (Interface 4) */
    TUD_MSC_DESCRIPTOR(4, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
#endif
};

const tusb_desc_device_t* get_Desc_Device(void) {
    return &descriptor_dev_default;
}

const uint8_t* get_Desc_Config(void)
{
    return descriptor_fs_cfg_default;
}
