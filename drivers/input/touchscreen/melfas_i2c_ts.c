/* drivers/input/touchscreen/melfas_i2c_ts.c
 *
 * Copyright (C) 2010 Huawei, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
 
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <mach/mpp.h>
#include <mach/gpio.h>
#include <mach/vreg.h>

#include <linux/hardware_self_adapt.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/namei.h>

#define TS_SCL_GPIO		60
#define TS_SDA_GPIO		61

//#define TS_DEBUG
#ifdef TS_DEBUG
#define MELFAS_DEBUG(fmt, args...) printk(KERN_ERR fmt, ##args)
#else
#define MELFAS_DEBUG(fmt, args...)
#endif

#define TS_X_OFFSET		1
#define TS_Y_OFFSET		TS_X_OFFSET

#define TS_INT_GPIO		29
#define TS_RESET_GPIO	96

#define MELFAS_I2C_NAME "melfas-ts"

enum
{
   INPUT_INFO_NONE     = 0,
   INPUT_INFO_TOUCH_UP = 1,
   INPUT_INFO_KEY_UP   = 2,
};

enum
{
   TOUCH_KEY_INDEX0     = 0,
   TOUCH_KEY_INDEX1     = 1,
   TOUCH_KEY_INDEX2     = 2,
   TOUCH_KEY_INDEX3     = 3,
   TOUCH_KEY_INDEX_NONE = 4
};

struct melfas_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
    struct input_dev *key_input;
    struct workqueue_struct *melfas_wq;
	struct work_struct  work;
	int use_irq;
	struct hrtimer timer;	
	int (*power)(struct i2c_client* client, int on);
	struct early_suspend early_suspend;
	
    bool is_first_point;
    bool use_touch_key;
    uint16_t last_x; 
	uint16_t last_y;
	uint8_t key_index_save;
	unsigned int x_max;
	unsigned int y_max;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_early_suspend(struct early_suspend *h);
static void melfas_ts_late_resume(struct early_suspend *h);
#endif

static int melfas_ts_power(struct i2c_client *client, int on);

static uint8_t touch_key_value[] = {KEY_BACK, KEY_MENU, KEY_HOME, KEY_SEARCH, KEY_RESERVED};
static char name[30]="melfas-touchscreen";
#ifdef CONFIG_MELFAS_UPDATE_TS_FIRMWARE

#define MELFAS_UPDATE_FIRMWARE_MODE_ADDR   0x7D
#define MCSDL_TRANSFER_LENGTH              128		/* Program & Read flash block size */
#define MCSDL_MAX_FILE_LENGTH              62*1024

struct gs_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_irq;
	struct mutex  mlock;
	struct hrtimer timer;
	struct work_struct  work;	
	uint32_t flags;
	struct early_suspend early_suspend;
};
extern struct gs_data *TS_updateFW_gs_data;

struct aps_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct mutex  mlock;
	struct hrtimer timer;
	struct work_struct  work;	
	int (*power)(int on);
};
extern struct aps_data *TS_updateFW_aps_data;
extern struct workqueue_struct *TS_updateFW_aps_wq;

static struct i2c_client *g_client = NULL;
static struct melfas_ts_data *ts = NULL;

static ssize_t update_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf);
static ssize_t update_firmware_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);

static int mcsdl_enter_download_mode(void);
static int mcsdl_erase_flash(void);
static int mcsdl_read_flash(uint8_t *pBuffer, uint16_t nAddr_start, uint8_t cLength);
static int mcsdl_prepare_program(void);
static int mcsdl_program_flash(uint8_t *pBuffer, uint16_t nAddr_start, uint8_t cLength);
static int mcsdl_download(uint8_t *pgm_data,  uint16_t length );
static int ts_firmware_file(void);
static int i2c_update_firmware(void); 

static struct kobj_attribute update_firmware_attribute = {
	.attr = {.name = "update_firmware", .mode = 0666},
	.show = update_firmware_show,
	.store = update_firmware_store,
};

static int mcsdl_enter_download_mode(void)
{
	int i;
	int ret = 0;
	int read_data = 0;
	uint8_t enter_code[14] = { 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1 };

	/* CE set output and low */
	ret = gpio_tlmm_config(GPIO_CFG(TS_RESET_GPIO, 0, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_2MA), GPIO_ENABLE);
	ret = gpio_direction_output(TS_RESET_GPIO, 0);

	/* config SCL/SDA output mode and set low  */
	ret = gpio_tlmm_config(GPIO_CFG(TS_SCL_GPIO, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	ret = gpio_tlmm_config(GPIO_CFG(TS_SDA_GPIO, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	ret = gpio_direction_output(TS_SCL_GPIO, 0);
	ret = gpio_direction_output(TS_SDA_GPIO, 0);

	/* INTR set output and low */
	ret = gpio_tlmm_config(GPIO_CFG(TS_INT_GPIO, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	ret = gpio_direction_output(TS_INT_GPIO, 0);
	mdelay(100);

	ret = gpio_direction_output(TS_RESET_GPIO, 1);			/* CE set high */
	ret = gpio_direction_output(TS_SDA_GPIO, 1);			/* SDA set high */
	mdelay(25);												/* must delay 25msec */

	/* input download mode single */
	for (i=0; i<14; i++) {
		if( enter_code[i] ){
			ret = gpio_direction_output(TS_INT_GPIO, 1);	/* INTR set high */
		} else {
			ret = gpio_direction_output(TS_INT_GPIO, 0);	/* INTR set low */
		}

		ret = gpio_direction_output(TS_SCL_GPIO, 1);		/* SCL set high */
		udelay(15);
		ret = gpio_direction_output(TS_SCL_GPIO, 0);		/* SCL set low */
		ret = gpio_direction_output(TS_INT_GPIO, 0);		/* INTR set low */
		udelay(100);
	}
	ret = gpio_direction_output(TS_SCL_GPIO, 1);			/* SCL set high */
	udelay(100);
	ret = gpio_direction_output(TS_INT_GPIO, 1);			/* INTR set high */
	mdelay(1);

	/* config I/O to i2c mode */
	ret = gpio_tlmm_config(GPIO_CFG(TS_SCL_GPIO, 1, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	ret = gpio_tlmm_config(GPIO_CFG(TS_SDA_GPIO, 1, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	mdelay(1);

	read_data = i2c_smbus_read_byte(g_client);
	if (read_data != 0x55) {							/* 0x55 is i2c slave ready status */
		printk("mcsdl enter download mode error\n");
		return -1;
	}

	return 0;		/* success */
}

static int mcsdl_erase_flash(void)
{
	int i;
	int ret = 0;
	int read_data = 0;
	uint8_t i2c_buffer[4] = {0x0F, 						/* isp erase timing cmd */
							0x01, 						/* isp erase timing value 0 */
							0xD4, 						/* isp erase timing value 1 */
							0xC0};						/* isp erase timing value 2 */

	/* Send Erase Setting code */
	for(i=0; i<4; i++){
		ret = i2c_smbus_write_byte(g_client, i2c_buffer[i]);
		if( 0 != ret ){
			printk("mcsdl prepare erase flash error\n");
			return -1;
		}
		udelay(15);
	}
	udelay(500);

	/* Read Result */
	read_data = i2c_smbus_read_byte(g_client);
	if (read_data != 0x8F) {							/* isp ack prepare erase done */
		printk("mcsdl erase flash error0\n");
		return -1;
	}
	mdelay(1);

	/*Send Erase code */
	ret = i2c_smbus_write_byte(g_client, 0x02);
	if( 0 != ret ){
		printk("mcsdl send erase code error\n");
		return -1;
	}
	mdelay(45);
	read_data = i2c_smbus_read_byte(g_client);
	if (read_data != 0x82) {
		printk("mcsdl erase flash error1\n");
		return -1;
	}
	return 0;
}


static int mcsdl_read_flash(uint8_t *pBuffer, uint16_t nAddr_start, uint8_t cLength)
{
	int i;
	int ret = 0;
	int read_data = 0;
	uint8_t   cmd[4];

	cmd[0] = 0x04;											/* isp read flash cmd */
	cmd[1] = (uint8_t)((nAddr_start >> 8) & 0xFF);
	cmd[2] = (uint8_t)((nAddr_start     ) & 0xFF);
	cmd[3] = cLength;

	/* send read command */
 	for(i=0; i<4; i++){
		ret = i2c_smbus_write_byte(g_client, cmd[i]);
		udelay(15);
		if( 0 != ret ){
			printk("mcsdl send read flash cmd error0\n");
			return -1;
		}
 	}

	/* Read 'Result of command' */
	read_data = i2c_smbus_read_byte(g_client);
	if (read_data != 0x84) {								/* 0x84 is prepare read flash OK status */
		printk("mcsdl send read flash cmd error1\n");
		return -1;
	}

	/* Read Data  [ Cmd[3] = Size ] */
	for(i=0; i<(int)cmd[3]; i++){
		udelay(100);
		read_data = i2c_smbus_read_byte(g_client);
		if ( read_data<0 ) {
			printk("mcsdl read flash error\n");
			return -1;
		}
		*pBuffer++ = read_data;
	}
	return 0;
}

static int mcsdl_prepare_program(void)
{
	int i;
	int ret = 0;
	int read_data = 0;
	uint8_t i2c_buffer[4] = {0x0F,							/* isp program flash cmd */
							0x00,							/* isp program timing value 0 */
							0x00,							/* isp program timing value 1 */
							0x78};							/* isp program timing value 2 */

	/* Write Program timing information */
	for(i=0; i<4; i++){
		ret = i2c_smbus_write_byte(g_client, i2c_buffer[i]);
		if( 0 != ret ){
			printk("mcsdl Write Program timing information error\n");
			return -1;
		}
		udelay(15);
	}
	udelay(500);

	/* Read Result */
	read_data = i2c_smbus_read_byte(g_client);
	if (read_data != 0x8F) {								/* 0x8F is prepare program flash OK status */	
		printk("mcsdl prepare program error\n");
		return -1;
	}
	ndelay(100);

	return 0;
}

static int mcsdl_program_flash(uint8_t *pBuffer, uint16_t nAddr_start, uint8_t cLength)
{
	int i;
	int ret = 0;
	int read_data = 0;
	uint8_t   cmd[4];

	cmd[0] = 0x03;											/* isp program flash cmd */
	cmd[1] = (uint8_t)((nAddr_start >> 8) & 0xFF);
	cmd[2] = (uint8_t)((nAddr_start     ) & 0xFF);
	cmd[3] = cLength;

	/* send PGM flash command */
 	for(i=0; i<4; i++){
		ret = i2c_smbus_write_byte(g_client, cmd[i]);
		udelay(15);
		if( 0 != ret ){
			printk("mcsdl send PGM flash cmd error0\n");
			return -1;
		}
 	}

	/* Check command result */
	read_data = i2c_smbus_read_byte(g_client);
	if (read_data != 0x83) {
		printk("mcsdl send PGM flash cmd error1\n");
		return -1;
	}
	udelay(150);

	/* Program Data */
	for(i=0; i<(int)cmd[3]; i+=2){
		ret = i2c_smbus_write_byte(g_client, pBuffer[i+1]);
		if( 0 != ret ){
			printk("mcsdl Program Data error0\n");
			return -1;
		}
		udelay(100);
		ret = i2c_smbus_write_byte(g_client, pBuffer[i]);
		if( 0 != ret ){
			printk("mcsdl Program Data error1\n");
			return -1;
		}
		udelay(150);
	}
	return 0;
}

static int mcsdl_download(uint8_t *pgm_data,  uint16_t length )
{
	int i;
	int ret;
	uint8_t   cLength;
	uint16_t  nStart_address=0;
	uint8_t   buffer[MCSDL_TRANSFER_LENGTH];

	/* Enter Download mode */
	ret = mcsdl_enter_download_mode();
	if (0 != ret) {
		return -1;
	} 
	printk("mcsdl enter download mode success!\n");
	mdelay(1);

	/* erase flash */
	printk("Erasing...\n");
	ret = mcsdl_erase_flash();
	if (0 != ret) {
		return -1;
	}
	printk("Erase OK!\n");
	mdelay(1);

	/* Verify erase */
	printk("Verifying erase...\n");
	ret = mcsdl_read_flash(buffer, 0x00, 16);		/* Must be '0xFF' after erase */
	if (0 != ret) {
		return -1;
	}
	for(i=0; i<16; i++){
		if( buffer[i] != 0xFF ){
			printk("Verify flash error\n");
			return -1;
		}
	}
	mdelay(1);

	/* Prepare for Program flash */
	printk("Preparing Program...\n");
	ret = mcsdl_prepare_program();
	if (0 != ret) {
		return -1;
	}
	mdelay(1);

	/* Program flash */
	printk("Programing flash...");
	nStart_address = 0;
	cLength  = MCSDL_TRANSFER_LENGTH;
	for( nStart_address = 0; nStart_address < length; nStart_address+=cLength ){
		printk("#");
		if( ( length - nStart_address ) < MCSDL_TRANSFER_LENGTH ){
			cLength  = (uint8_t)(length - nStart_address);
			cLength += (cLength%2);									/* For odd length */
		}
		ret = mcsdl_program_flash(&pgm_data[nStart_address], nStart_address, cLength);
		if (0 != ret) {
			printk("\nProgram flash failed.\n");
			return -1;
		}
		ndelay(500);
	}

	/* Verify flash */
	printk("\nVerifying flash...");
	nStart_address = 0;
	cLength  = MCSDL_TRANSFER_LENGTH;
	for( nStart_address = 0; nStart_address < length; nStart_address+=cLength ){
		printk("#");
		if( ( length - nStart_address ) < MCSDL_TRANSFER_LENGTH ){
			cLength  = (uint8_t)(length - nStart_address);
			cLength += (cLength%2);									/* For odd length */
		}
		ret = mcsdl_read_flash(buffer, nStart_address, cLength);

		if (0 != ret) {
			printk("\nVerify flash read failed.\n");
			return -1;
		}
		for(i=0; i<(int)cLength; i++){
			if( buffer[i] != pgm_data[nStart_address+i] ){
				printk("\nVerify flash compare failed.\n");
				return -1;
			}
		}
		ndelay(500);
	}
	printk("\n");

	/* Reset command */
	mdelay(1);
	ret = i2c_smbus_write_byte(g_client, 0x07);						/* 0x07 is reset cmd */
	if( 0 != ret ){
		printk("reset error\n");
		return -1;
	}
	mdelay(180);

	return 0;
}

static int ts_firmware_file(void)
{
	int ret;
	struct kobject *kobject_ts;
	kobject_ts = kobject_create_and_add("touch_screen", NULL);
	if (!kobject_ts) {
		printk("create kobjetct error!\n");
		return -1;
	}
	ret = sysfs_create_file(kobject_ts, &update_firmware_attribute.attr);
	if (ret) {
		kobject_put(kobject_ts);
		printk("create file error\n");
		return -1;
	}
	return 0;	
}

/*
 * The "update_firmware" file where a static variable is read from and written to.
 */
static ssize_t update_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf)
{
	return 1;
}

static ssize_t 
update_firmware_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int i;
	int ret = -1;
	int ret1;
	int *p = &i;

	printk("Update_firmware_store.\n");
#ifndef CONFIG_MELFAS_RESTORE_FIRMWARE
	if ((buf[0] == '2')&&(buf[1] == '\0')) {

		/* driver detect its device  */
		for (i = 0; i < 3; i++) {
			ret = i2c_smbus_read_byte_data(g_client, 0x00);
			if (ret >= 0) {
				goto firmware_find_device;
			}
		}
		printk("Dont find melfas_ts device\n");
		return -1;

firmware_find_device: 
		ret = i2c_smbus_read_byte_data(g_client, 0x21);			/* read firmware version */
		printk("%s: reg21 = 0x%x\n", __FUNCTION__, ret);
#else
	if (buf[0] == '2') {
#endif
		disable_irq(g_client->irq);
		free_irq(g_client->irq, ts);
		g_client->addr = MELFAS_UPDATE_FIRMWARE_MODE_ADDR;

		/*update firmware*/
		ret = i2c_update_firmware();

		ret1 = gpio_tlmm_config(GPIO_CFG(TS_INT_GPIO, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA), GPIO_ENABLE);
		ret1 = gpio_configure(TS_INT_GPIO, GPIOF_INPUT | IRQF_TRIGGER_FALLING);
		mdelay(10);
		g_client->addr = 0x23;									/* touchscreen i2c addr */
		enable_irq(g_client->irq);

		if( 0 != ret ){
			printk("Update firmware failed!\n\n");
		} else {
			printk("update firmware success!\n\n");
			mdelay(200);										/* for "printk" */
#ifdef CONFIG_MELFAS_RESTORE_FIRMWARE
			mdelay(8000);
#else
			arm_pm_restart(0,p);
#endif
		}
	}

	return ret;
 }

static int i2c_update_firmware(void)
{
	char *buf;
	struct file	*filp;
    struct inode *inode = NULL;
	mm_segment_t oldfs;
    uint16_t	length;
	int ret = 0;           //"/data/melfas_ts_update_firmware.bin";
	const char filename[]="/sdcard/update/melfas_ts_update_firmware.bin";

	/* open file */
    oldfs = get_fs();
    set_fs(KERNEL_DS);
    filp = filp_open(filename, O_RDONLY, S_IRUSR);
    if (IS_ERR(filp)) {
        printk("%s: file %s filp_open error\n", __FUNCTION__,filename);
        set_fs(oldfs);
        return -1;
    }

    if (!filp->f_op) {
        printk("%s: File Operation Method Error\n", __FUNCTION__);
        filp_close(filp, NULL);
        set_fs(oldfs);
        return -1;
    }

    inode = filp->f_path.dentry->d_inode;
    if (!inode) {
        printk("%s: Get inode from filp failed\n", __FUNCTION__);
        filp_close(filp, NULL);
        set_fs(oldfs);
        return -1;
    }
    printk("%s file offset opsition: %u\n", __FUNCTION__, (unsigned)filp->f_pos);

    /* file's size */
    length = i_size_read(inode->i_mapping->host);
   	printk("%s: length=%d\n", __FUNCTION__, length);
	if (!( length > 0 && length < MCSDL_MAX_FILE_LENGTH )){
		printk("file size error\n");
		filp_close(filp, NULL);
        set_fs(oldfs);
		return -1;
	}

	/* allocation buff size */
	buf = vmalloc((length+(length%2)));		/* buf size if even */
	if (!buf) {
		printk("alloctation memory failed\n");
		filp_close(filp, NULL);
        set_fs(oldfs);
		return -1;
	}
	if ( length%2 == 1 ) {
		buf[length] = 0xFF;						  		/* Fill Empty space */
	}

    /* read data */
    if (filp->f_op->read(filp, buf, length, &filp->f_pos) != length) {
        printk("%s: file read error\n", __FUNCTION__);
        filp_close(filp, NULL);
        set_fs(oldfs);
		vfree(buf);
        return -1;
    }

#ifndef CONFIG_MELFAS_RESTORE_FIRMWARE
	/* disable other I2C device */
	if (&TS_updateFW_gs_data->timer != NULL) {
		if (TS_updateFW_gs_data->use_irq) {
			disable_irq(TS_updateFW_gs_data->client->irq);
		} else {
			hrtimer_cancel(&TS_updateFW_gs_data->timer);
		}
		cancel_work_sync(&TS_updateFW_gs_data->work);
		mutex_lock(&TS_updateFW_gs_data->mlock);
		i2c_smbus_write_byte_data(TS_updateFW_gs_data->client, 0x20, 0);
		mutex_unlock(&TS_updateFW_gs_data->mlock);
		printk("hrtimer_cancel_GS\n");
	}
	if (&TS_updateFW_aps_data->timer != NULL) {
		hrtimer_cancel(&TS_updateFW_aps_data->timer);
		cancel_work_sync(&TS_updateFW_aps_data->work);
		if (TS_updateFW_aps_wq) {
			printk("destroy_aps_wq\n");
			destroy_workqueue(TS_updateFW_aps_wq);
		}
		mutex_lock(&TS_updateFW_aps_data->mlock);
		i2c_smbus_write_byte_data(TS_updateFW_aps_data->client, 0, 0);
		mutex_unlock(&TS_updateFW_aps_data->mlock);
		printk("hrtimer_cancel_APS\n");
	}
	mdelay(1000);
#endif

	ret = mcsdl_download(buf, length+(length%2));

 	filp_close(filp, NULL);
    set_fs(oldfs);
	vfree(buf);
	printk("%s: free file buffer\n", __FUNCTION__);
	return ret;
}
#endif

static void melfas_ts_work_func(struct work_struct *work)
{
	int i,k;
	int ret;
	int bad_data = 0;	
	struct i2c_msg msg[2];
	uint8_t start_reg;
	/* varible z is press value*/
	uint8_t z = 0;
	uint8_t input_info = 0;
	uint8_t buf[9];
	uint8_t key_info = 0;
	uint8_t key_index = 0;
	uint8_t key_pressed = 0;
	uint16_t position[2][2];
	uint8_t w = 0;

	struct melfas_ts_data *ts = container_of(work, struct melfas_ts_data, work);
	if(machine_is_msm7x25_u8500())
	{
		start_reg = 0x02;
	}
	else
	{
		start_reg = 0x10;
	}
	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	
	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(buf);
	msg[1].buf = buf;

	for (i = 0; i < ((ts->use_irq && !bad_data)? 1 : 5 ); i++)
	{
		ret = i2c_transfer(ts->client->adapter, msg, 2);
		if (ret < 0) 
		{
			MELFAS_DEBUG("%d times i2c_transfer failed\n",i);
			bad_data = 1;
			continue;
		}
		else
		{
		    bad_data = 0;
		}
    	if (i == 5) 
		{
			pr_err("five times i2c_transfer error\n");
			break;
		}

#ifdef TS_DEBUG
        /*printf debug info*/
        for(k=0; k < sizeof(buf); k++)
        {
            MELFAS_DEBUG("%s:register[0x%x]= 0x%x \n",__FUNCTION__, 0x10+k, buf[k]);
        }
#endif

	    input_info = buf[0] & 0x07;
		position[0][0] = buf[2] | (uint16_t)(buf[1] & 0x03) << 8;
		position[0][1] = buf[4] | (uint16_t)(buf[3] & 0x03) << 8;
		/*reading pressure register */
		z = buf[5]; 
		key_info = buf[8] & 0x3;
		key_index = (buf[8] & 0x0C) >> 2;
		key_pressed = (buf[0] & 0xC0) >> 6;
		w = buf[6];
		
        MELFAS_DEBUG("input_type = %d touch_key_status = %d \n",input_info, key_info);
		
		if (input_info == 1) //single point touch
	    {     
	        MELFAS_DEBUG("Touch_Area: X = %d Y = %d \n",position[0][0],position[0][1]);
	        
			if (ts->is_first_point) 
			{
				input_report_abs(ts->input_dev, ABS_X, position[0][0]);
				input_report_abs(ts->input_dev, ABS_Y, position[0][1]);
				/* reports pressure value for inputdevice*/
				input_report_abs(ts->input_dev, ABS_PRESSURE, z);
				input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
				input_report_key(ts->input_dev, BTN_TOUCH, 1);
				input_sync(ts->input_dev);
				ts->last_x = position[0][0];
				ts->last_y = position[0][1];
				ts->is_first_point = false;
			}
			else 
			{
				 if (((position[0][0]-ts->last_x) >= TS_X_OFFSET) 
					     || ((ts->last_x-position[0][0]) >= TS_X_OFFSET) 			
					     || ((position[0][1]-ts->last_y) >= TS_Y_OFFSET) 
					     || ((ts->last_y-position[0][1]) >= TS_Y_OFFSET)) 
				 {
					input_report_abs(ts->input_dev, ABS_X, position[0][0]);
					input_report_abs(ts->input_dev, ABS_Y, position[0][1]);	
					/* reports pressure value for inputdevice*/
					input_report_abs(ts->input_dev, ABS_PRESSURE, z);
   				    input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
					input_report_key(ts->input_dev, BTN_TOUCH, 1);
					input_sync(ts->input_dev);
					ts->last_x = position[0][0];
					ts->last_y = position[0][1];
				}
            }
        }
        else if (input_info == 0)
        {
            if(!ts->use_touch_key)
            {
                MELFAS_DEBUG("Touch_Area: touch release!! \n");
                ts->is_first_point = true;
                /* reports pressure value for inputdevice*/
                input_report_abs(ts->input_dev, ABS_PRESSURE, z);
        	    input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
        	    input_report_key(ts->input_dev, BTN_TOUCH, 0);	
        	    input_sync(ts->input_dev);
            }
            else
            {
                if(key_info != 0)
                {
                    if(ts->key_index_save != key_index)
                    {
                        ts->key_index_save = key_index;
                        MELFAS_DEBUG("Touch_key_Area: touch_key_value[%d]= %d , pressed = %d\n",key_index,touch_key_value[key_index],key_info);
                        input_report_key(ts->key_input, touch_key_value[key_index], key_info);	
                        input_sync(ts->key_input);
                    }
                }
                else
                {
                    if(key_pressed == INPUT_INFO_KEY_UP)
                    {
                        if(ts->key_index_save < TOUCH_KEY_INDEX_NONE)
                        {
                            MELFAS_DEBUG("Touch_key release, touch_key_value[%d]= %d, released = %d \n",ts->key_index_save,touch_key_value[ts->key_index_save], key_info);
                	        input_report_key(ts->key_input, touch_key_value[ts->key_index_save], key_info);	
                            input_sync(ts->key_input);
                        }
                    }
                    else
                    {
                        MELFAS_DEBUG("Touch_Area: touch release!! \n");
                        ts->is_first_point = true;
                        /* reports pressure value for inputdevice*/
                        input_report_abs(ts->input_dev, ABS_PRESSURE, z);
                	    input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
                	    input_report_key(ts->input_dev, BTN_TOUCH, 0);	
                	    input_sync(ts->input_dev);

                	    if(ts->key_index_save < TOUCH_KEY_INDEX_NONE)
                	    {
                	        MELFAS_DEBUG("Touch_key release, touch_key_value[%d]= %d, released = %d \n",ts->key_index_save,touch_key_value[ts->key_index_save], key_info);
                	        input_report_key(ts->key_input, touch_key_value[ts->key_index_save], key_info);	
                            input_sync(ts->key_input);
                	    }
                    }

                    ts->key_index_save = TOUCH_KEY_INDEX_NONE;
                }
            }
        }
	}

	if (ts->use_irq)
	{
	    enable_irq(ts->client->irq);
	    MELFAS_DEBUG("melfas_ts_work_func,enable_irq\n");
	}
}

static enum hrtimer_restart melfas_ts_timer_func(struct hrtimer *timer)
{
	struct melfas_ts_data *ts = container_of(timer, struct melfas_ts_data, timer);
	MELFAS_DEBUG("melfas_ts_timer_func\n");
	queue_work(ts->melfas_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static irqreturn_t melfas_ts_irq_handler(int irq, void *dev_id)
{
	struct melfas_ts_data *ts = dev_id;
	disable_irq_nosync(ts->client->irq);
//	disable_irq(ts->client->irq);
 	MELFAS_DEBUG("melfas_ts_irq_handler: disable irq\n");
	queue_work(ts->melfas_wq, &ts->work);
	return IRQ_HANDLED;
}

static int melfas_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
#ifndef CONFIG_MELFAS_UPDATE_TS_FIRMWARE
	struct melfas_ts_data *ts;
#endif
	int ret = 0;
	int gpio_config;
	int i;
	struct vreg *v_gp5;
  
	MELFAS_DEBUG(" In melfas_ts_probe: \n");
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		pr_err(KERN_ERR "melfas_ts_probe: need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}
	
	/* power on touchscreen */
    v_gp5 = vreg_get(NULL,"gp5");
    ret = IS_ERR(v_gp5);
    if(ret) 
        return ret;

    ret = vreg_set_level(v_gp5,2800);
    if (ret)
        return ret;
    ret = vreg_enable(v_gp5);
    if (ret)
    {
        goto err_power_on_failed; 
    }

	ret = gpio_tlmm_config(GPIO_CFG(TS_RESET_GPIO, 0, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_2MA), GPIO_ENABLE);

	ret = gpio_direction_output(TS_RESET_GPIO, 0);
	
	mdelay(50);
#ifdef CONFIG_MELFAS_RESTORE_FIRMWARE
	goto restore_firmware;
#endif
	
	ret = gpio_direction_output(TS_RESET_GPIO, 1);
	mdelay(300);
	
	/* driver  detect its device  */  
	for(i = 0; i < 3; i++) 
	{		
		ret = i2c_smbus_read_byte_data(client, 0x00);
		MELFAS_DEBUG("id:%d\n",ret);
		if (ret < 0)
			continue;
		else
			goto  succeed_find_device;
	}
	if (i == 3) 
	{	
		pr_err("%s:check %d times, but dont find melfas_ts device\n",__FUNCTION__, i);	
		ret = gpio_tlmm_config(GPIO_CFG(TS_RESET_GPIO, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), GPIO_ENABLE);	/* for synaptics ts */
		if (ret < 0) 
		{
			printk("synaptics config TS_RESET_GPIO failed.\n");
		}
		goto err_find_touchpanel_failed;
	}

succeed_find_device:
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) 
	{
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->power = melfas_ts_power;
	if (ts->power) 
	{
		ret = ts->power(ts->client, 1);
		if (ret < 0) 
		{
			pr_err("melfas_ts_probe: reset failed\n");
			goto err_power_failed;
		}
	}

	ts->melfas_wq = create_singlethread_workqueue("melfas_wq");
	if (!ts->melfas_wq) 
	{
		pr_err("%s:create melfas_wq error\n",__FUNCTION__);
		ret = -ENOMEM;
		goto err_destroy_wq;
	}
	INIT_WORK(&ts->work, melfas_ts_work_func);

    ts->is_first_point = true;
    ts->use_touch_key = false;
    ts->key_index_save = TOUCH_KEY_INDEX_NONE;
    if(board_use_tssc_touch(&ts->use_touch_key))
    {
        printk(KERN_ERR "%s: Cannot support melfas touch_keypad!\n", __FUNCTION__);
        ret = -ENODEV;
        goto err_destroy_wq;
    }
	if(machine_is_msm7x25_c8600() || machine_is_msm7x25_u8500() || machine_is_msm7x25_m860())
	{
		ts->x_max = 320;
		ts->y_max = 480;
	}
	else
	{
		ts->x_max = 240;
		ts->y_max = 320;
	}
    	
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		pr_err("melfas_ts_probe: Failed to allocate touch input device\n");
		goto err_input_dev_alloc_failed;
	}
	/*reading u8150/c8150 touch version and displaying in input_dev->name*/
	if (machine_is_msm7x25_c8150() || machine_is_msm7x25_u8150() \
		|| machine_is_msm7x25_u8159() )
	{
		memset(name,0,30);
		ret = i2c_smbus_read_byte_data(client, 0x21);	
		sprintf(name, "melfas-touchscreen.Ver%x",ret);		
		ts->input_dev->name =name;		
	}
	else
		ts->input_dev->name = "melfas-touchscreen";	
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	input_set_abs_params(ts->input_dev, ABS_X, 0, ts->x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, ts->y_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	/*modify width reported max value*/
	input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH, 0, 64, 0, 0);

	ret = input_register_device(ts->input_dev);
	if (ret) 
	{
		pr_err("melfas_ts_probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}   

    if(ts->use_touch_key)
    {
    	ts->key_input = input_allocate_device();
    	if (!ts->key_input  || !ts) {
    		ret = -ENOMEM;
    		pr_err("melfas_ts_probe: Failed to allocate key input device\n");
    		goto err_key_input_dev_alloc_failed;
    	}

	    ts->key_input->name = "melfas-touch-keypad";
		set_bit(EV_KEY, ts->key_input->evbit);
		for (i = 0; i < sizeof(touch_key_value); i++)
		{
			set_bit(touch_key_value[i] & KEY_MAX, ts->key_input->keybit);
		}

		ret = input_register_device(ts->key_input);
		if (ret)
			goto err_key_input_register_device_failed;
	}
  
	gpio_config = GPIO_CFG(TS_INT_GPIO, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA);
	ret = gpio_tlmm_config(gpio_config, GPIO_ENABLE);
	if (ret < 0) 
	{
	    pr_err("%s: gpio_tlmm_config(%#x)=%d\n", __func__, TS_INT_GPIO, ret);
		ret = -EIO;
		goto err_key_input_register_device_failed; 
	}
	
	if (gpio_request(TS_INT_GPIO, "melfas_ts_int\n"))
		pr_err("failed to request gpio melfas_ts_int\n");
	
	ret = gpio_configure(TS_INT_GPIO, GPIOF_INPUT | IRQF_TRIGGER_FALLING);/*gpio 29 is interupt for touchscreen.*/
	if (ret) 
	{
		pr_err("melfas_ts_probe: gpio_configure %d irq failed\n", TS_INT_GPIO);
		goto err_key_input_register_device_failed;
	}

	if (client->irq) 
	{
		ret = request_irq(client->irq, melfas_ts_irq_handler, 0, client->name, ts);		
		if (ret == 0)
			ts->use_irq = 1;
		else
			dev_err(&client->dev, "melfas_ts_probe: request_irq failed\n");
	}
	if (!ts->use_irq) 
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = melfas_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = melfas_ts_early_suspend;
	ts->early_suspend.resume = melfas_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	printk(KERN_INFO "melfas_ts_probe: Start touchscreen %s in %s mode\n", ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

#ifdef CONFIG_MELFAS_UPDATE_TS_FIRMWARE 
restore_firmware:
	g_client = client;
	if (machine_is_msm7x25_c8150() || machine_is_msm7x25_u8150() \
		|| machine_is_msm7x25_u8159() )
	{
		for (i = 0 ; i < 3; i++) {
			ret= ts_firmware_file();   
			if (!ret)
				break;
		}
	}
#endif

	return 0;

err_key_input_register_device_failed:
    if(ts->use_touch_key)
    {
	    input_free_device(ts->key_input);
	}
err_key_input_dev_alloc_failed:
err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
err_destroy_wq:
	destroy_workqueue(ts->melfas_wq);
	
err_power_failed:
	kfree(ts);
err_alloc_data_failed:
err_find_touchpanel_failed:
err_power_on_failed:
    (void)vreg_disable(v_gp5);
err_check_functionality_failed:
	return ret;
}

static int melfas_ts_power(struct i2c_client *client, int on)
{
    int ret = 0;
    MELFAS_DEBUG("melfas_ts_power on = %d\n", on);

    ret = gpio_tlmm_config(GPIO_CFG(TS_RESET_GPIO, 0, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_2MA), GPIO_ENABLE);
    if(ret < 0)
    {
        pr_err("%s: fail to config TS_RESET_GPIO(#%d)\n", __func__,TS_RESET_GPIO);
    }

    if (on) 
    {			  
    	ret = gpio_direction_output(TS_RESET_GPIO, 1);
    	if (ret) 
    	{
    		pr_err("%s: Failed to configure power on = (%d)\n",__func__, ret);
    	} 
    }
    else 
    {
    	ret = gpio_direction_output(TS_RESET_GPIO, 0);
    	if (ret) 
    	{
    		pr_err("%s: Failed to configure power off = (%d)\n",__func__, ret);
    	}       	
    }	

	return ret;	
}

static int melfas_ts_remove(struct i2c_client *client)
{
	struct melfas_ts_data *ts = i2c_get_clientdata(client);
	unregister_early_suspend(&ts->early_suspend);
	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

static int melfas_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct melfas_ts_data *ts = i2c_get_clientdata(client);
	MELFAS_DEBUG("In melfas_ts_suspend\n");
	
	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	ret = cancel_work_sync(&ts->work);
	if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
	{
		enable_irq(client->irq);
	}
	ret = melfas_ts_power(client,0);	
	if (ret < 0) {
        pr_err("melfas_ts_suspend: power off failed\n");			
	}
	return 0;
}

static int melfas_ts_resume(struct i2c_client *client)
{
	int ret;
	struct melfas_ts_data *ts = i2c_get_clientdata(client);
	MELFAS_DEBUG("In melfas_ts_resume\n");
	
	ret = melfas_ts_power(client,1);	
	if (ret < 0) 
	{
	    pr_err("melfas_ts_resume: power on failed\n");			
	}

	msleep(200);  /* wait for device reset; */
	
	if (ts->use_irq) 
	{
		enable_irq(client->irq);
	}

	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_early_suspend(struct early_suspend *h)
{
	struct melfas_ts_data *ts;

	MELFAS_DEBUG("melfas_ts_early_suspend\n");
	ts = container_of(h, struct melfas_ts_data, early_suspend);
	melfas_ts_suspend(ts->client, PMSG_SUSPEND);  
}

static void melfas_ts_late_resume(struct early_suspend *h)
{
	struct melfas_ts_data *ts;

	MELFAS_DEBUG("melfas_ts_late_resume\n");
	ts = container_of(h, struct melfas_ts_data, early_suspend);
	melfas_ts_resume(ts->client);	
}
#endif

static const struct i2c_device_id melfas_ts_id[] = {
	{ MELFAS_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver melfas_ts_driver = {
	.probe		= melfas_ts_probe,
	.remove		= melfas_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= melfas_ts_suspend,
	.resume		= melfas_ts_resume,
#endif
	.id_table	= melfas_ts_id,
	.driver = {
		.name	= MELFAS_I2C_NAME,
	},
};

static int __devinit melfas_ts_init(void)
{
	MELFAS_DEBUG(KERN_ERR "melfas_ts_init\n ");
	return i2c_add_driver(&melfas_ts_driver);
}

static void __exit melfas_ts_exit(void)
{
	i2c_del_driver(&melfas_ts_driver);
}

module_init(melfas_ts_init);
module_exit(melfas_ts_exit);

MODULE_DESCRIPTION("Melfas Touchscreen Driver");
MODULE_LICENSE("GPL");

