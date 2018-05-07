/*
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Description: MMC driver for winner's mmc controller
 * Author: Aaron <leafy.myeh@allwinnertech.com>
 * Date: 2012-2-3 14:18:18
 */
#include "string.h"
#include "mmc_def.h"
#include "mmc.h"

/* winner's mmc controller definition */
struct sunxi_mmc {
volatile	u32_t gctrl;         /* (0x00) SMC Global Control Register */
volatile	u32_t clkcr;         /* (0x04) SMC Clock Control Register */
volatile	u32_t timeout;       /* (0x08) SMC Time Out Register */
volatile	u32_t width;         /* (0x0C) SMC Bus Width Register */
volatile	u32_t blksz;         /* (0x10) SMC Block Size Register */
volatile	u32_t bytecnt;       /* (0x14) SMC Byte Count Register */
volatile	u32_t cmd;           /* (0x18) SMC Command Register */
volatile	u32_t arg;           /* (0x1C) SMC Argument Register */
volatile	u32_t resp0;         /* (0x20) SMC Response Register 0 */
volatile	u32_t resp1;         /* (0x24) SMC Response Register 1 */
volatile	u32_t resp2;         /* (0x28) SMC Response Register 2 */
volatile	u32_t resp3;         /* (0x2C) SMC Response Register 3 */
volatile	u32_t imask;         /* (0x30) SMC Interrupt Mask Register */
volatile	u32_t mint;          /* (0x34) SMC Masked Interrupt Status Register */
volatile	u32_t rint;          /* (0x38) SMC Raw Interrupt Status Register */
volatile	u32_t status;        /* (0x3C) SMC Status Register */
volatile	u32_t ftrglevel;     /* (0x40) SMC FIFO Threshold Watermark Register */
volatile	u32_t funcsel;       /* (0x44) SMC Function Select Register */
volatile	u32_t cbcr;          /* (0x48) SMC CIU Byte Count Register */
volatile	u32_t bbcr;          /* (0x4C) SMC BIU Byte Count Register */
volatile	u32_t dbgc;          /* (0x50) SMC Debug Enable Register */
volatile 	u32_t res0[11];      /* (0x54~0x7c) */
volatile	u32_t dmac;          /* (0x80) SMC IDMAC Control Register */
volatile	u32_t dlba;          /* (0x84) SMC IDMAC Descriptor List Base Address Register */
volatile	u32_t idst;          /* (0x88) SMC IDMAC Status Register */
volatile	u32_t idie;          /* (0x8C) SMC IDMAC Interrupt Enable Register */
volatile	u32_t chda;          /* (0x90) */
volatile	u32_t cbda;          /* (0x94) */
volatile    u32_t res1[26];      /* (0x98~0xff) */
volatile	u32_t fifo;          /* (0x100) SMC FIFO Access Address */
};

struct sunxi_mmc_des {
	u32_t             :1,
		dic         :1, /* disable interrupt on completion */
		last_des    :1, /* 1-this data buffer is the last buffer */
		first_des   :1, /* 1-data buffer is the first buffer,
						   0-data buffer contained in the next descriptor is 1st buffer */
		des_chain   :1, /* 1-the 2nd address in the descriptor is the next descriptor address */
		end_of_ring :1, /* 1-last descriptor flag when using dual data buffer in descriptor */
					:24,
		card_err_sum:1, /* transfer error flag */
		own			:1; /* des owner:1-idma owns it, 0-host owns it */
#if defined MMC_SUN4I
#define SDXC_DES_NUM_SHIFT 12
#define SDXC_DES_BUFFER_MAX_LEN	(1 << SDXC_DES_NUM_SHIFT)
	u32_t	data_buf1_sz:13,
	    data_buf2_sz:13,
    				:6;
#else
#define SDXC_DES_NUM_SHIFT 15
#define SDXC_DES_BUFFER_MAX_LEN	(1 << SDXC_DES_NUM_SHIFT)
	u32_t data_buf1_sz:16,
	    data_buf2_sz:16;
#endif
	u32_t	buf_addr_ptr1;
	u32_t	buf_addr_ptr2;
};

struct sunxi_mmc_host {
	struct sunxi_mmc *reg;
	u32_t  mmc_no;
	u32_t  mclk;
	u32_t  hclkbase;
	u32_t  mclkbase;
	u32_t  database;
	u32_t  fatal_err;
	struct sunxi_mmc_des *pdes;
};

/* support 4 mmc hosts */
struct mmc mmc_dev[MAX_MMC_NUM];
struct sunxi_mmc_host mmc_host[MAX_MMC_NUM];

static int mmc_resource_init(int sdc_no)
{
	struct sunxi_mmc_host* mmchost = &mmc_host[sdc_no];

	mmcdbg("init mmc %d resource\r\n", sdc_no);
	mmchost->reg = (struct sunxi_mmc *)(MMC_REG_BASE + sdc_no * 0x1000);
	mmchost->database = (u32_t)mmchost->reg + MMC_REG_FIFO_OS;
	mmchost->hclkbase = CCMU_HCLKGATE0_BASE;
	if (sdc_no == 0)
		mmchost->mclkbase = CCMU_MMC0_CLK_BASE;
	else if (sdc_no == 1)
			mmchost->mclkbase = CCMU_MMC1_CLK_BASE;
	else if (sdc_no == 2)
		mmchost->mclkbase = CCMU_MMC2_CLK_BASE;
	else {
		mmcinfo("Wrong mmc NO.: %d\r\n", sdc_no);
		return -1;
	}
	mmchost->mmc_no = sdc_no;

	return 0;
}

static int mmc_update_clk(struct mmc *mmc)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	unsigned int cmd;
	unsigned timeout = 0xfffff;

	cmd = (1U << 31) | (1 << 21) | (1 << 13);
  	writel(cmd, &mmchost->reg->cmd);
	while((readl(&mmchost->reg->cmd)&0x80000000) && timeout--);
	if (!timeout)
		return -1;

	writel(readl(&mmchost->reg->rint), &mmchost->reg->rint);
	return 0;
}

static int mmc_config_clock(struct mmc *mmc, unsigned div)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	unsigned rval = readl(&mmchost->reg->clkcr);

	/*
	 * CLKCREG[7:0]: divider
	 * CLKCREG[16]:  on/off
	 * CLKCREG[17]:  power save
	 */
	/* Disable Clock */
	rval &= ~(1 << 16);
	writel(rval, &mmchost->reg->clkcr);
	if(mmc_update_clk(mmc))
		return -1;
/*
	if (clk <=400000) {
	    mmchost->mclk = 400000;
	    writel(0x8002000f, mmchost->mclkbase);
	} else {
	    mmchost->mclk = 24000000;
	    writel(0x80000001, mmchost->mclkbase);
	    //writel(0x80700001, mmchost->mclkbase);
	}
*/
	/*
	 * CLKCREG[7:0]: divider
	 * CLKCREG[16]:  on/off
	 * CLKCREG[17]:  power save
	 */
	/* Change Divider Factor */
	rval &= ~(0xFF);
	rval |= div;
	writel(rval, &mmchost->reg->clkcr);
	if(mmc_update_clk(mmc))
		return -1;
	/* Re-enable Clock */
	rval |= (3 << 16);
	writel(rval, &mmchost->reg->clkcr);
	if(mmc_update_clk(mmc))
		return -1;
	return 0;
}

static void mmc_set_ios(struct mmc *mmc)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	unsigned int clkdiv = 0;

	mmcdbg("ios: bus: %d, clock: %d\r\n", mmc->bus_width, mmc->clock);

	/* Change clock first */
	clkdiv = (mmchost->mclk + (mmc->clock>>1))/mmc->clock/2;
	if (mmc->clock)
		if (mmc_config_clock(mmc, clkdiv)) {
			mmchost->fatal_err = 1;
			return;
		}
	/* Change bus width */
	if (mmc->bus_width == 8)
		writel(2, &mmchost->reg->width);
	else if (mmc->bus_width == 4)
		writel(1, &mmchost->reg->width);
	else
		writel(0, &mmchost->reg->width);
}

static int mmc_core_init(struct mmc *mmc)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	/* Reset controller */
	writel(0x7, &mmchost->reg->gctrl);
	return 0;
}
static int mmc_trans_data_by_cpu(struct mmc *mmc, struct mmc_data *data)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	unsigned i;
	unsigned byte_cnt = data->blocksize * data->blocks;
	unsigned *buff;
	unsigned timeout = 0xffffff;

	if (data->flags & MMC_DATA_READ) {
		buff = (unsigned int *)data->b.dest;
		for (i=0; i<(byte_cnt>>2); i++) {
			while(--timeout && (readl(&mmchost->reg->status)&(1 << 2)));
			if (timeout <= 0)
				goto out;
			buff[i] = readl(mmchost->database);
			timeout = 0xffffff;
		}
	} else {
		buff = (unsigned int *)data->b.src;
		for (i=0; i<(byte_cnt>>2); i++) {
			while(--timeout && (readl(&mmchost->reg->status)&(1 << 3)));
			if (timeout <= 0)
				goto out;
			writel(buff[i], mmchost->database);
			timeout = 0xffffff;
		}
	}

out:
	if (timeout <= 0)
		return -1;

	return 0;
}

static int mmc_trans_data_by_dma(struct mmc *mmc, struct mmc_data *data)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	struct sunxi_mmc_des *pdes = mmchost->pdes;
	unsigned byte_cnt = data->blocksize * data->blocks;
	unsigned char *buff;
	unsigned des_idx = 0;
	unsigned buff_frag_num = 0;
	unsigned remain;
	unsigned i, rval;

	buff = data->flags & MMC_DATA_READ ?
			(unsigned char *)data->b.dest : (unsigned char *)data->b.src;
	buff_frag_num = byte_cnt >> SDXC_DES_NUM_SHIFT;
	remain = byte_cnt & (SDXC_DES_BUFFER_MAX_LEN-1);
	if (remain)
		buff_frag_num ++;
	else
		remain = SDXC_DES_BUFFER_MAX_LEN;

	//OSAL_CacheRangeFlush(buff, (unsigned long)byte_cnt, CACHE_CLEAN_FLUSH_D_CACHE_REGION);
	for (i=0; i < buff_frag_num; i++, des_idx++) {
		memset((void*)&pdes[des_idx], 0, sizeof(struct sunxi_mmc_des));
		pdes[des_idx].des_chain = 1;
		pdes[des_idx].own = 1;
		pdes[des_idx].dic = 1;
		if (buff_frag_num > 1 && i != buff_frag_num-1) {
			pdes[des_idx].data_buf1_sz = SDXC_DES_BUFFER_MAX_LEN;
		} else
			pdes[des_idx].data_buf1_sz = remain;

		pdes[des_idx].buf_addr_ptr1 = (u32_t)buff + i * SDXC_DES_BUFFER_MAX_LEN;
		if (i==0)
			pdes[des_idx].first_des = 1;

		if (i == buff_frag_num-1) {
			pdes[des_idx].dic = 0;
			pdes[des_idx].last_des = 1;
			pdes[des_idx].end_of_ring = 1;
			pdes[des_idx].buf_addr_ptr2 = 0;
		} else {
			pdes[des_idx].buf_addr_ptr2 = (u32_t)&pdes[des_idx+1];
		}
		mmcdbg("frag %d, remain %d, des[%d](%x): "
			"[0] = %x, [1] = %x, [2] = %x, [3] = %x\r\n",
			i, remain, des_idx, (u32_t)&pdes[des_idx],
			(u32_t)((u32_t*)&pdes[des_idx])[0], (u32_t)((u32_t*)&pdes[des_idx])[1],
			(u32_t)((u32_t*)&pdes[des_idx])[2], (u32_t)((u32_t*)&pdes[des_idx])[3]);
	}
	//OSAL_CacheRangeFlush(pdes, sizeof(struct sunxi_mmc_des) * (des_idx+1), CACHE_CLEAN_FLUSH_D_CACHE_REGION);

	/*
	 * GCTRLREG
	 * GCTRL[2]	: DMA reset
	 * GCTRL[5]	: DMA enable
	 *
	 * IDMACREG
	 * IDMAC[0]	: IDMA soft reset
	 * IDMAC[1]	: IDMA fix burst flag
	 * IDMAC[7]	: IDMA on
	 *
	 * IDIECREG
	 * IDIE[0]	: IDMA transmit interrupt flag
	 * IDIE[1]	: IDMA receive interrupt flag
	 */
	rval = readl(&mmchost->reg->gctrl);
	writel(rval|(1 << 5)|(1 << 2), &mmchost->reg->gctrl);	/* dma enable */
	writel((1 << 0), &mmchost->reg->dmac); /* idma reset */
	writel((1 << 1) | (1 << 7), &mmchost->reg->dmac); /* idma on */
	rval = readl(&mmchost->reg->idie) & (~3);
	if (data->flags & MMC_DATA_WRITE)
		rval |= (1 << 0);
	else
		rval |= (1 << 1);
	writel(rval, &mmchost->reg->idie);
	writel((u32_t)pdes, &mmchost->reg->dlba);
	writel((2U<<28)|(7<<16)|8, &mmchost->reg->ftrglevel);

	return 0;
}

static int mmc_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd,
			struct mmc_data *data)
{
	struct sunxi_mmc_host* mmchost = (struct sunxi_mmc_host *)mmc->priv;
	unsigned int cmdval = 0x80000000;
	signed int timeout = 0;
	int error = 0;
	unsigned int status = 0;
	unsigned int usedma = 0;
	unsigned int bytecnt = 0;

	if (mmchost->fatal_err)
		return -1;
	if (cmd->resp_type & MMC_RSP_BUSY)
		mmcdbg("mmc cmd %d check rsp busy\r\n", cmd->cmdidx);
	if (cmd->cmdidx == 12){
		timeout = 0x4ffffff;
		//mmcinfo("c12b");
		do {
			status = readl(&mmchost->reg->status);
			if (!timeout--) {
				error = -1;
				mmcinfo("cmd12 check busy timeout\r\n");
				goto out;
			}
			//mmcinfo("check busy\r\n");
		} while (status & (1 << 9));		
		return 0;
    }
	/*
	 * CMDREG
	 * CMD[5:0]	: Command index
	 * CMD[6]	: Has response
	 * CMD[7]	: Long response
	 * CMD[8]	: Check response CRC
	 * CMD[9]	: Has data
	 * CMD[10]	: Write
	 * CMD[11]	: Steam mode
	 * CMD[12]	: Auto stop
	 * CMD[13]	: Wait previous over
	 * CMD[14]	: About cmd
	 * CMD[15]	: Send initialization
	 * CMD[21]	: Update clock
	 * CMD[31]	: Load cmd
	 */
	if (!cmd->cmdidx)
		cmdval |= (1 << 15);
	if (cmd->resp_type & MMC_RSP_PRESENT)
		cmdval |= (1 << 6);
	if (cmd->resp_type & MMC_RSP_136)
		cmdval |= (1 << 7);
	if (cmd->resp_type & MMC_RSP_CRC)
		cmdval |= (1 << 8);
	if (data) {
		if ((u32_t)data->b.dest & 0x3) {
			error = -1;
			goto out;
		}

		cmdval |= (1 << 9) | (1 << 13);
		if (data->flags & MMC_DATA_WRITE)
			cmdval |= (1 << 10);
		if (data->blocks > 1)
			cmdval |= (1 << 12);
		writel(data->blocksize, &mmchost->reg->blksz);
		writel(data->blocks * data->blocksize, &mmchost->reg->bytecnt);
	}

	mmcdbg("mmc %d, cmd %d(0x%x), arg 0x%x\r\n", mmchost->mmc_no, cmd->cmdidx, cmdval|cmd->cmdidx, cmd->cmdarg);
	writel(cmd->cmdarg, &mmchost->reg->arg);
	if (!data)
		writel(cmdval|cmd->cmdidx, &mmchost->reg->cmd);

	/*
	 * transfer data and check status
	 * STATREG[2] : FIFO empty
	 * STATREG[3] : FIFO full
	 */
	if (data) {
		int ret = 0;
		bytecnt = data->blocksize * data->blocks;
		mmcdbg("trans data %d bytes\r\n", bytecnt);
#ifdef MMC_TRANS_BY_DMA
		if (bytecnt > 512) {
#else
		if (0) {
#endif
			usedma = 1;
			writel(readl(&mmchost->reg->gctrl)&(~0x80000000), &mmchost->reg->gctrl);
			ret = mmc_trans_data_by_dma(mmc, data);
			writel(cmdval|cmd->cmdidx, &mmchost->reg->cmd);
		} else {
			writel(readl(&mmchost->reg->gctrl)|0x80000000, &mmchost->reg->gctrl);
			writel(cmdval|cmd->cmdidx, &mmchost->reg->cmd);
			ret = mmc_trans_data_by_cpu(mmc, data);
		}
		if (ret) {
			error = readl(&mmchost->reg->rint) & 0xbbc2;
			if(!error)
				error = 0xffffffff;
			goto out;
		}
	}

	timeout = 0xffffff;
	do {
		status = readl(&mmchost->reg->rint);
		if (!timeout-- || (status & 0xbbc2)) {
			error = status & 0xbbc2;
			if(!error)
				error = 0xffffffff;//represet software timeout
			mmcinfo("cmd %d timeout, err %x\r\n", cmd->cmdidx, error);
			goto out;
		}
	} while (!(status&0x4));

	if (data) {
		unsigned done = 0;
		timeout = usedma ? 0xffff*bytecnt : 0xffff;
		if(timeout<=0)
			timeout = 0x7fffffff;
		do {
			status = readl(&mmchost->reg->rint);
			if (!timeout-- || (status & 0xbbc2)) {
				error = status & 0xbbc2;
				if(!error)
					error = 0xffffffff;//represet software timeout
				mmcinfo("data timeout, err %x\r\n", error);
				goto out;
			}
			if (data->blocks > 1)
				done = status & (1 << 14);
			else
				done = status & (1 << 3);
		} while (!done);
	}

	if (cmd->resp_type & MMC_RSP_BUSY) {
		timeout = 0x4ffffff;
		do {
			status = readl(&mmchost->reg->status);
			if (!timeout--) {
				error = -1;
				mmcinfo("busy timeout\r\n");
				goto out;
			}
		} while (status & (1 << 9));
	}
	if (cmd->resp_type & MMC_RSP_136) {
		cmd->response[0] = readl(&mmchost->reg->resp3);
		cmd->response[1] = readl(&mmchost->reg->resp2);
		cmd->response[2] = readl(&mmchost->reg->resp1);
		cmd->response[3] = readl(&mmchost->reg->resp0);
		mmcdbg("mmc resp 0x%x 0x%x 0x%x 0x%x\r\n",
			cmd->response[3], cmd->response[2],
			cmd->response[1], cmd->response[0]);
	} else {
		cmd->response[0] = readl(&mmchost->reg->resp0);
		mmcdbg("mmc resp 0x%x\r\n", cmd->response[0]);
	}
out:
	if (data && usedma) {
	/* IDMASTAREG
	 * IDST[0] : idma tx int
	 * IDST[1] : idma rx int
	 * IDST[2] : idma fatal bus error
	 * IDST[4] : idma descriptor invalid
	 * IDST[5] : idma error summary
	 * IDST[8] : idma normal interrupt sumary
	 * IDST[9] : idma abnormal interrupt sumary
	 */
		status = readl(&mmchost->reg->idst);
		writel(status, &mmchost->reg->idst);
        writel(0, &mmchost->reg->idie);
        writel(0, &mmchost->reg->dmac);
        writel(readl(&mmchost->reg->gctrl)&(~(1 << 5)), &mmchost->reg->gctrl);
	}
	if (error) {
		writel(0x7, &mmchost->reg->gctrl);
		mmc_update_clk(mmc);
		mmcinfo("mmc cmd %d err %x\r\n", cmd->cmdidx, error);
	}
	writel(0xffffffff, &mmchost->reg->rint);

	if (error)
		return -1;
	else
		return 0;
}

int sunxi_mmc_init(int sdc_no, unsigned bus_width)
{
	struct mmc *mmc;
	int ret;

	memset(&mmc_dev[sdc_no], 0, sizeof(struct mmc));
	memset(&mmc_host[sdc_no], 0, sizeof(struct sunxi_mmc_host));
	mmc = &mmc_dev[sdc_no];

	//strcpy(mmc->name, "SUNXI SD/MMC");
	mmc->name[0] = 'S';
	mmc->name[1] = 'U';
	mmc->name[2] = 'N';
	mmc->name[3] = 'X';
	mmc->name[4] = 'I';
	mmc->name[5] = ' ';
	mmc->name[6] = 'S';
	mmc->name[7] = 'D';
	mmc->name[8] = '/';
	mmc->name[9] = 'M';
	mmc->name[10] = 'M';
	mmc->name[11] = 'C';
	mmc->name[12] = '\0';
	mmc->priv = &mmc_host[sdc_no];
	mmc->send_cmd = mmc_send_cmd;
	mmc->set_ios = mmc_set_ios;
	mmc->init = mmc_core_init;

	mmc->voltages = MMC_VDD_29_30|MMC_VDD_30_31|MMC_VDD_31_32|MMC_VDD_32_33|
	                MMC_VDD_33_34|MMC_VDD_34_35|MMC_VDD_35_36;
	mmc->host_caps = MMC_MODE_HS_52MHz|MMC_MODE_HS|MMC_MODE_HC;
	if (bus_width==4)
		mmc->host_caps |= MMC_MODE_4BIT;

	mmc->f_min = 400000;
	mmc->f_max = 52000000;
	
    mmc_host[sdc_no].pdes = (struct sunxi_mmc_des*)0x42000000;
	if (mmc_resource_init(sdc_no))
		return -1;

	ret = mmc_register(sdc_no, mmc);
	if (ret < 0)
		return -1;

	return mmc->lba;
}

int sunxi_mmc_exit(int sdc_no)
{
	mmc_unregister(sdc_no);
	memset(&mmc_dev[sdc_no], 0, sizeof(struct mmc));
	memset(&mmc_host[sdc_no], 0, sizeof(struct sunxi_mmc_host));

	return 0;
}