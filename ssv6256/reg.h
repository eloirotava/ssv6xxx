/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SSV6256 (iComm "Turismo") register map: only what the driver uses.
 *
 * The chip has one flat 32-bit address space reached through the SDIO
 * register port.  Addresses are absolute, as in the hardware manual.
 *
 * From the vendor driver:
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 */
#ifndef SSV6256_REG_H
#define SSV6256_REG_H

/* Register blocks */
#define SYS_REG_BASE				0xc0000000
#define SPI_REG_BASE				0xc0000a00

/* System controller */
#define ADR_BRG_SW_RST				(SYS_REG_BASE + 0x0)
#define  MCU_ENABLE				BIT(0)
#define  MAC_SW_RST				BIT(1)
#define  PLF_SW_RST				BIT(20)
#define ADR_BOOT				(SYS_REG_BASE + 0x4)
#define  RG_REBOOT				BIT(0)
#define ADR_CHIP_ID_2				(SYS_REG_BASE + 0x10)
#define  DUAL_BAND_ID				0x30303643	/* "006C" */
#define ADR_CHIP_ID_3				(SYS_REG_BASE + 0x14)
#define ADR_CLOCK_SELECTION			(SYS_REG_BASE + 0x18)
#define  CLK_DIGI_SEL				GENMASK(3, 0)
#define   CLK_DIGI_40M				4
#define   CLK_DIGI_80M				8
#define ADR_PLATFORM_CLOCK_ENABLE		(SYS_REG_BASE + 0x1c)
#define  RESET_N_CPUN10				BIT(24)
#define ADR_MANUAL_RESET_N			(SYS_REG_BASE + 0xb4)
#define  CLK_EN_CPUN10				BIT(1)
#define ADR_N10CFG_DEF_IVB			(SYS_REG_BASE + 0xe8)
#define  N10CFG_DEFAULT_IVB			GENMASK(15, 0)
#define ADR_PRESCALER_USTIMER			(SYS_REG_BASE + 0x118)
#define  PRESCALER_US				GENMASK(8, 0)
#define ADR_SRAM_MODE				(SYS_REG_BASE + 0x128)
#define  SRAM_MODE_ILM_160K			BIT(1)
#define ADR_SRAM_WRITE_ADDR			(SYS_REG_BASE + 0x860)
#define ADR_MASK_TYPHOST_INT_MAP_15		(SYS_REG_BASE + 0x2074)
#define ADR_MASK_TYPHOST_INT_MAP		(SYS_REG_BASE + 0x208c)
#define ADR_TX_SEG				(SPI_REG_BASE + 0x10)

/* Host controller interface */
#define ADR_CONTROL				0xc1000000
#define ADR_HCI_TX_RX_INFO_SIZE			0xc1000030
#define  TX_PBOFFSET				GENMASK(7, 0)
#define  TX_INFO_SIZE				GENMASK(15, 8)
#define  RX_INFO_SIZE				GENMASK(23, 16)
#define  RX_LAST_PHY_SIZE			GENMASK(31, 24)
#define ADR_TX_ETHER_TYPE_0			0xc1000050
#define ADR_TX_ETHER_TYPE_1			0xc1000054
#define ADR_RX_ETHER_TYPE_0			0xc1000060
#define ADR_RX_ETHER_TYPE_1			0xc1000064

/* MAC receive engine */
#define ADR_MRX_FLT_TB0				0xc6000070
#define ADR_MRX_FLT_EN0				0xc60000b0
#define ADR_RX_FLOW_DATA			0xc60000e0
#define ADR_RX_FLOW_MNG				0xc60000e4
#define ADR_RX_FLOW_CTRL			0xc60000e8
#define ADR_RX_TIME_STAMP_CFG			0xc60000ec
#define  MRX_STP_OFST				GENMASK(15, 8)
#define ADR_BA_CTRL				0xc6000100
#define ADR_BA_TID				0xc600010c
#define  BA_TID					GENMASK(3, 0)
#define ADR_MRX_WATCH_DOG			0xc600011c
#define ADR_TRAP_HW_ID				0xc6000134
#define ADR_AMPDU_SCOREBOAD_SIZE		0xc600019c

/* MAC transmit engine */
#define ADR_MTX_MISC_EN				0xc6002008
#define  MTX_AMPDU_CRC8_AUTO			BIT(5)
#define  MTX_BLOCKTX_IGNORE_CCA_ED_SECONDARY	BIT(14)
#define ADR_MTX_RATERPT				0xc6002064
/* One EDCA parameter set per hardware queue, 0x100 apart */
#define ADR_TXQ0_MTX_Q_AIFSN			0xc6002104
#define  TXQ_AIFSN				GENMASK(3, 0)
#define  TXQ_ECWMIN				GENMASK(11, 8)
#define  TXQ_ECWMAX				GENMASK(15, 12)
#define  TXQ_TXOP_LIMIT				GENMASK(31, 16)
#define  TXQ_STRIDE				0x100
#define  MTX_RATERPT_HWID			GENMASK(3, 0)
/* Beacon: two slots in packet memory, and the timers that send them */
#define ADR_MTX_BCN_PKT_SET0			0xc6002088
#define ADR_MTX_BCN_PKT_SET1			0xc600208c
#define  MTX_BCN_PKT_ID				GENMASK(6, 0)
#define ADR_MTX_BCN_DTIM_SET0			0xc6002090
#define ADR_MTX_BCN_DTIM_SET1			0xc6002094
#define  MTX_DTIM_OFST				GENMASK(9, 0)
#define ADR_MTX_BCN_DTIM_CONFG			0xc6002098
#define  MTX_DTIM_NUM				GENMASK(7, 0)
#define ADR_MTX_BCN_EN_MISC			0xc60020a8
#define  MTX_BCN_TIMER_EN			BIT(0)
#define  MTX_TIME_STAMP_AUTO_FILL		BIT(1)
#define  MTX_DTIM_CNT_AUTO_FILL			BIT(3)
#define  MTX_TSF_TIMER_EN			BIT(5)
#define  MTX_BCN_AUTO_SEQ_NO			BIT(17)
#define ADR_MTX_BCN_MISC			0xc60020ac
#define  MTX_BCN_PKTID_CH_LOCK			BIT(0)
#define  MTX_BCN_CFG_VLD			GENMASK(2, 1)
#define  MTX_AUTO_BCN_ONGOING			BIT(3)
#define ADR_MTX_BCN_PRD				0xc60020b0
#define  MTX_BCN_PERIOD				GENMASK(15, 0)
#define ADR_MTX_TIME_IFS			0xc60020c4
#define  MTX_SIFS				GENMASK(20, 16)
#define ADR_MTX_TIME_FINETUNE			0xc60020c8
#define  MTX_SIGEXT				GENMASK(27, 24)
#define  PHYTXSTART_NCYCLE			GENMASK(22, 16)
#define  MAC_CLK_80M				BIT(28)

/* Response frame rate tables */
#define ADR_MTX_RESPFRM_RATE_TABLE_01		0xc6003008
#define ADR_MTX_RESPFRM_RATE_TABLE_02		0xc600300c
#define ADR_MTX_RESPFRM_RATE_TABLE_03		0xc6003010
#define ADR_MTX_RESPFRM_RATE_TABLE_11		0xc6003014
#define ADR_MTX_RESPFRM_RATE_TABLE_12		0xc6003018
#define ADR_MTX_RESPFRM_RATE_TABLE_13		0xc600301c
#define ADR_MTX_RESPFRM_RATE_TABLE_92_B2	0xc6003028
#define ADR_MTX_RESPFRM_RATE_TABLE_94_B4	0xc6003030
#define ADR_MTX_RESPFRM_RATE_TABLE_C1_E1	0xc6003044
#define ADR_MTX_RESPFRM_RATE_TABLE_C3_E3	0xc600304c
#define ADR_MTX_RESPFRM_RATE_TABLE_D1_F1	0xc6003064
#define ADR_MTX_RESPFRM_RATE_TABLE_D3_F3	0xc600306c

/* Station and BSS table */
#define ADR_WSID0				0xca000000
#define ADR_WSID1				0xca000050
#define ADR_WSID2				0xca010000
#define ADR_WSID3				0xca010050
#define ADR_WSID4				0xca0100a0
#define ADR_WSID5				0xca0100f0
#define ADR_WSID6				0xca010140
#define ADR_WSID7				0xca010190
#define ADR_BSSID_0				0xca000328
#define ADR_BSSID_1				0xca00032c
#define ADR_STA_MAC_0				0xca000330
#define ADR_STA_MAC_1				0xca000334
#define ADR_GLBLE_SET				0xca00031c
#define  OP_MODE				GENMASK(1, 0)
#define   OPMODE_STA				0
#define   OPMODE_AP				1
#define  PB_OFFSET				GENMASK(15, 8)
#define  SNIFFER_MODE				BIT(16)
#define  QOS_EN					BIT(4)
#define  DUP_FLT				BIT(17)
#define  TX_PKT_RSVD				GENMASK(20, 18)
#define  CCMP_H_SEL				BIT(22)
#define  SEC_LUT_SEL				BIT(23)
#define ADR_REASON_TRAP0			0xca000320
#define ADR_REASON_TRAP1			0xca000324

/* Packet buffer and mailbox */
#define ADR_CH0_TRIG_1				0xcd000010
#define ADR_MCU_STATUS				0xcd000018
#define  CH0_FULL				BIT(0)
#define ADR_WR_ALC				0xcd010000
#define  PBUF_SIZE				GENMASK(15, 0)
#define  PBUF_TYPE				GENMASK(18, 16)
#define   PBUF_TX				1
#define   PBUF_RX				2
/* A buffer address carries its packet id in bits 27..16. */
#define  PBUF_ADDR_ID				GENMASK(27, 16)
#define ADR_MBOX_HALT_CFG			0xcd00002c
#define  MB_ERR_AUTO_HALT_EN			BIT(20)
#define ADR_MB_DBG_CFG1				0xcd000030
#define  MB_DBG_EN				BIT(31)
#define ADR_TRX_ID_THRESHOLD			0xcd010020
#define  TX_ID_THOLD				GENMASK(7, 0)
#define  RX_ID_THOLD				GENMASK(15, 8)
#define ADR_ID_LEN_THREADSHOLD1			0xcd010038
#define  ID_TX_LEN_THOLD			GENMASK(12, 4)
#define  ID_RX_LEN_THOLD			GENMASK(21, 13)
#define ADR_TX_LIMIT_INTR			0xcd01004c
#define  TX_PAGE_LIMIT				GENMASK(8, 0)
#define  TX_COUNT_LIMIT				GENMASK(23, 16)
#define  TX_LIMIT_INT_EN			BIT(31)

/* RF front end */
#define ADR_MODE_REGISTER			0xccb0a400
#define  RG_MODE_MANUAL				BIT(2)
#define  RG_TXGAIN_PHYCTRL			BIT(6)
#define  RG_MODE				GENMASK(10, 8)
#define   MODE_STANDBY				0
#define   MODE_CALIBRATION			1
#define   MODE_WIFI2P4G_RX			3
#define   MODE_WIFI5G_RX			7
#define  RG_CAL_INDEX				GENMASK(15, 12)
#define   CAL_IDX_NONE				0
#define   CAL_IDX_WIFI2P4G_RXDC			1
#define   CAL_IDX_BW20_RXRC			3
#define   CAL_IDX_WIFI2P4G_TXLO			4
#define   CAL_IDX_WIFI2P4G_TXIQ			5
#define   CAL_IDX_WIFI2P4G_RXIQ			6
#define   CAL_IDX_WIFI5G_RXDC			9
#define   CAL_IDX_BW40_RXRC			11
#define   CAL_IDX_WIFI5G_TXLO			12
#define   CAL_IDX_WIFI5G_TXIQ			13
#define   CAL_IDX_WIFI5G_RXIQ			14
#define ADR_2_4G_LDO_REGISTER			0xccb0a40c
#define  RG_SX_LDO_LO_LEVEL			GENMASK(22, 20)
#define ADR_WIFI_HT20_RX_FILTER_REGISTER	0xccb0a410
#define  RG_WF_RX_ABBCTUNE			GENMASK(5, 0)
#define ADR_WIFI_HT40_RX_FILTER_REGISTER	0xccb0a414
#define  RG_WF_N_RX_ABBCTUNE			GENMASK(5, 0)
#define ADR_WIFI_TX_DAC_REGISTER		0xccb0a450
#define  RG_WF_TX_DAC_IOFFSET			GENMASK(23, 20)
#define  RG_WF_TX_DAC_QOFFSET			GENMASK(27, 24)
#define ADR_SX_CH_TABLE				0xccb0a464
#define  RG_SX_RFCH_MAP_EN			BIT(3)
#define  RG_SX_CHANNEL				GENMASK(18, 11)
#define  RG_SX_XTAL_FREQ			GENMASK(23, 20)
#define ADR_SX_2_4GB_PFD_CHP			0xccb0a468
#define  RG_SX_CP_ISEL_WF			GENMASK(10, 7)
#define ADR_SX_2_4GB_LPF			0xccb0a46c
#define  RG_SX_LPF_C2_WF			GENMASK(23, 20)
#define ADR_SX_2_4GB_VCOBF			0xccb0a474
#define  RG_SX_VCO_TXOB_AW			BIT(20)
#define  RG_SX_VCO_RXOB_AW			BIT(21)
#define ADR_WF_DCOC_IDAC_REGISTER1		0xccb0a494
#define ADR_5G_TX_DAC_REGISTER			0xccb0a578
#define  RG_5G_TX_DAC_IOFFSET			GENMASK(23, 20)
#define  RG_5G_TX_DAC_QOFFSET			GENMASK(27, 24)
#define ADR_SX_5GB_CH_TABLE			0xccb0a580
#define  RG_SX5GB_RFCH_MAP_EN			BIT(4)
#define  RG_SX5GB_CHANNEL			GENMASK(15, 8)
#define ADR_5G_DCOC_IDAC_REGISTER1		0xccb0a5a8
#define ADR_5G_CALIBRATION_TIMER_GAIN_REGISTER	0xccb0a608
#define  RG_5G_PGAG_TXCAL			GENMASK(23, 20)
#define  RG_5G_TX_GAIN_TXCAL			GENMASK(30, 24)
#define ADR_5G_CALIBRATION_GAIN_REGISTER1	0xccb0a60c
#define  RG_5G_RFG_RXIQCAL			GENMASK(5, 4)
#define  RG_5G_PGAG_RXIQCAL			GENMASK(9, 6)
#define ADR_WIFI_PADPD_5G_BB_GAIN_REG		0xccb0ada8
#define  WF_DCOC_IDAC_REGISTERS			21
#define ADR_CALIBRATION_TIMER_REGISTER		0xccb0a534
#define  RG_RX_RCCAL_DELAY			GENMASK(10, 8)
#define  RG_RX_N_RCCAL_DELAY			GENMASK(26, 24)
#define ADR_CALIBRATION_GAIN_REGISTER0		0xccb0a538
#define  RG_PGAG_RCCAL				GENMASK(3, 0)
#define  RG_PGAG_TXCAL				GENMASK(7, 4)
#define  RG_TX_GAIN_TXCAL			GENMASK(14, 8)
#define  RG_RFG_RXIQCAL				GENMASK(17, 16)
#define  RG_PGAG_RXIQCAL			GENMASK(21, 18)
#define  RG_TX_GAIN_RXIQCAL			GENMASK(28, 22)
#define ADR_DIGITAL_ADD_ON_0			0xccb0a800
#define  RG_40M_MODE				BIT(24)
#define  RG_LO_UP_CH				BIT(28)
#define ADR_DIGITAL_ADD_ON_3			0xccb0a80c
#define  RG_TX_IQCAL_TIME			GENMASK(21, 20)
#define ADR_DIGITAL_ADD_ON_4			0xccb0a810
#define  RG_TONE_SCALE				GENMASK(24, 16)
#define ADR_TRX_IQ_COMP_2G			0xccb0a820
#define  RG_RX_IQ_2500_ALPHA			GENMASK(4, 0)
#define  RG_RX_IQ_2500_THETA			GENMASK(12, 8)
#define  RG_TX_IQ_2500_ALPHA			GENMASK(20, 16)
#define  RG_TX_IQ_2500_THETA			GENMASK(28, 24)
#define ADR_RF_D_CAL_TOP_0			0xccb0a834
#define  RG_PHASE_STEP_VALUE			GENMASK(15, 0)
#define  RG_ALPHA_SEL				GENMASK(21, 20)
#define ADR_RF_D_CAL_TOP_1			0xccb0a838
#define  RO_WF_DCCAL_DONE			BIT(16)
#define  RO_RCCAL_DONE				BIT(18)
#define  RO_TXDC_DONE				BIT(19)
#define  RO_TXIQ_DONE				BIT(20)
#define  RO_RXIQ_DONE				BIT(21)
#define  RO_5G_TXDC_DONE			BIT(22)
#define  RO_5G_TXIQ_DONE			BIT(23)
#define  RO_5G_RXIQ_DONE			BIT(24)
#define  RO_5G_DCCAL_DONE			BIT(25)
#define ADR_RF_D_CAL_TOP_2			0xccb0a83c
#define  RG_PHASE_17P5M				GENMASK(15, 0)
#define ADR_RF_D_CAL_TOP_3			0xccb0a840
#define  RG_PHASE_RXIQ_1M			GENMASK(15, 0)
#define  RG_PHASE_1M				GENMASK(31, 16)
#define ADR_RF_D_CAL_TOP_4			0xccb0a844
#define  RG_PHASE_35M				GENMASK(31, 16)
#define ADR_RF_D_CAL_TOP_6			0xccb0a84c
#define  RG_RX_RCCAL_TARG			GENMASK(9, 0)
#define  RG_RCCAL_POLAR_INV			BIT(13)
#define  RG_RX_RCCAL_40M_TARG			GENMASK(25, 16)
#define ADR_RF_D_CAL_TOP_9			0xccb0a858
#define  RG_PRE_DC_AUTO				BIT(6)
#define ADR_WIFI_PADPD_2G_CONTROL_REG		0xccb0ad1c
#define  RG_DPD_AM_EN				BIT(0)
#define ADR_PMU_REG_1				0xccb0b000
#define  RG_XO_LDO_LEVEL			GENMASK(2, 0)
#define ADR_PMU_REG_2				0xccb0b004
#define  RG_LOAD_RFTABLE_RDY			BIT(31)
#define ADR_PMU_STATE_REG			0xccb0b044
#define  PMU_STATE_READY			0x13
#define ADR_PMU_DPLL_REG_0			0xccb0b080
#define  RG_EN_IOTADC_160M			BIT(8)
#define  RG_DP_XTAL_FREQ			GENMASK(19, 16)

/* Baseband */
#define ADR_WIFI_PHY_COMMON_SYS_REG		0xccb0e000
#define  RG_RF_5G_BAND				BIT(11)
#define  RG_PRIMARY_CH_SIDE			BIT(14)
#define  RG_SYSTEM_BW				BIT(15)
#define ADR_WIFI_PHY_COMMON_ENABLE_REG		0xccb0e004
#define  RG_PHY_MD_EN				BIT(0)
#define  RG_PHYRX_MD_EN				BIT(1)
#define  RG_PHYTX_MD_EN				BIT(2)
#define  RG_PHY11GN_MD_EN			BIT(3)
#define  RG_PHY11B_MD_EN			BIT(4)
#define  RG_PHYRXFIFO_MD_EN			BIT(5)
#define  RG_PHYTXFIFO_MD_EN			BIT(6)
#define  RG_PHY11BGN_MD_EN			BIT(8)
#define ADR_WIFI_11B_RX_REG_255			0xccb0ebfc
#define  RG_SOFT_RST_N_11B_RX			BIT(0)
#define ADR_WIFI_11GN_RX_REG_255		0xccb0f3fc
#define  RG_SOFT_RST_N_11GN_RX			BIT(0)

/* Crystal frequency codes for RG_*_XTAL_FREQ */
#define XTAL16M					0
#define XTAL24M					1
#define XTAL26M					2
#define XTAL40M					3
#define XTAL12M					4
#define XTAL20M					5
#define XTAL25M					6
#define XTAL32M					7

#endif
