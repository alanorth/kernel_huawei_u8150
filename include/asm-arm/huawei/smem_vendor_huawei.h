/*
 * Copyright (C) 2008 The HUAWEI ,inc
 * All rights reserved.
 *
 */
#ifndef _SMEM_VENDOR_HUAWEI_H_
#define _SMEM_VENDOR_HUAWEI_H_

#define VENDOR_PROP_CMDLINE_ID  " androidboot.localproppath="

#define APP_USB_SERIAL_LEN   16

#define VENDOR_NAME_LEN      32

typedef struct _app_usb_para_smem
{
  /* Stores usb serial number for apps */
  unsigned char usb_serial[APP_USB_SERIAL_LEN];
  unsigned usb_pid_index;
} app_usb_para_smem;

typedef struct _app_verder_name
{
  unsigned char vender_name[VENDOR_NAME_LEN];
  unsigned char country_name[VENDOR_NAME_LEN];
  /* del the update state */
}app_vender_name;

typedef struct
{
  app_usb_para_smem      usb_para;
  app_vender_name   vender_para;
} smem_huawei_vender;

#endif //_SMEM_VENDOR_HUAWEI_H_

