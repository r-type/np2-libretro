//
// FDC μPD765A
//


#include	"compiler.h"
#include	"pccore.h"
#include	"iocore.h"
#include	"fddfile.h"


static const BYTE FDCCMD_TABLE[32] = {
						0, 0, 8, 2, 1, 8, 8, 1, 0, 8, 1, 0, 8, 5, 0, 2,
						0, 8, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0};


#define FDC_FORCEREADY (1)
#define	FDC_MAXDRIVE	2
#define	FDC_DELAYERROR7

void fdc_interrupt(void) {

	fdc.intreq = TRUE;
	if (CTRL_FDMEDIA == DISKTYPE_2HD) {
		pic_setirq(0x0b);
	}
	else {
		pic_setirq(0x0a);
	}
	nevent_forceexit();
}

static void fdc_interruptreset(void) {

	fdc.intreq = FALSE;
}

static BOOL fdc_isfdcinterrupt(void) {

	return(fdc.intreq);
}

BYTE DMACCALL fdc_dmafunc(BYTE func) {

	switch(func) {
		case DMAEXT_START:
			return(1);

		case DMAEXT_END:				// TC
			fdc.tc = 1;
			break;
	}
	return(0);
}

static void fdc_dmaready(BYTE enable) {

	if (CTRL_FDMEDIA == DISKTYPE_2HD) {
		dmac.dmach[DMA_2HD].ready = enable;
	}
	else {
		dmac.dmach[DMA_2DD].ready = enable;
	}
}

void fdcbusy_error7(NEVENTITEM item) {

	if (item->flag & NEVENT_SETEVENT) {
		if (fdc.event == FDCEVENT_BUSY) {
			fdcsend_error7();
		}
	}
}

#if 0		// ↑イベントは残しとく...
static void fdcderay_error7(SINT16 ms) {							// ver0.27

	fdc.busy = 1;
	fdc.event = FDCEVENT_BUSY;
	fdc.status &= ~FDCSTAT_RQM;
	nevent_setbyms(NEVENT_FDCBUSY, ms, fdcbusy_error7, NEVENT_ABSOLUTE);
}
#endif


// ----------------------------------------------------------------------

void fdcsend_error7(void) {

	fdc.tc = 0;
	fdc.event = FDCEVENT_BUFSEND;
	fdc.bufp = 0;
	fdc.bufcnt = 7;
	fdc.buf[0] = (BYTE)(fdc.stat[fdc.us] >>  0);
	fdc.buf[1] = (BYTE)(fdc.stat[fdc.us] >>  8);
	fdc.buf[2] = (BYTE)(fdc.stat[fdc.us] >> 16);
	fdc.buf[3] = fdc.C;
	fdc.buf[4] = fdc.H;
	fdc.buf[5] = fdc.R;
	fdc.buf[6] = fdc.N;
	fdc.status = FDCSTAT_RQM | FDCSTAT_CB | FDCSTAT_DIO;
	fdc.stat[fdc.us] = 0;										// ver0.29
	fdc_dmaready(0);
	dmac_check();
	fdc_interrupt();
}

void fdcsend_success7(void) {

	fdc.tc = 0;
	fdc.event = FDCEVENT_BUFSEND;
	fdc.bufp = 0;
	fdc.bufcnt = 7;
	fdc.buf[0] = (fdc.hd << 2) | fdc.us;
	fdc.buf[1] = 0;
	fdc.buf[2] = 0;
	fdc.buf[3] = fdc.C;
	fdc.buf[4] = fdc.H;
	fdc.buf[5] = fdc.R;
	fdc.buf[6] = fdc.N;
	fdc.status = FDCSTAT_RQM | FDCSTAT_CB | FDCSTAT_DIO;
	fdc.stat[fdc.us] = 0;										// ver0.29
	fdc_dmaready(0);
	dmac_check();
	fdc_interrupt();
}

#if 0
// FDCのタイムアウト			まぁ本当はこんなんじゃダメだけど…	ver0.29
void fdctimeoutproc(NEVENTITEM item) {

	if (item->flag & NEVENT_SETEVENT) {
		fdc.stat[fdc.us] = FDCRLT_IC0 | FDCRLT_EN | (fdc.hd << 2) | fdc.us;
		fdcsend_error7();
	}
}

static void fdc_timeoutset(void) {

	nevent_setbyms(NEVENT_FDCTIMEOUT, 166, fdctimeoutproc, NEVENT_ABSOLUTE);
}
#endif



static BOOL FDC_DriveCheck(BYTE protectcheck) {

	if (!fddfile[fdc.us].fname[0]) {
		fdc.stat[fdc.us] = FDCRLT_IC0 | FDCRLT_NR | (fdc.hd << 2) | fdc.us;
		fdcsend_error7();
		return(FALSE);
	}
	else if ((protectcheck) && (fddfile[fdc.us].protect)) {
		fdc.stat[fdc.us] = FDCRLT_IC0 | FDCRLT_NW | (fdc.hd << 2) | fdc.us;
		fdcsend_error7();
		return(FALSE);
	}
	return(TRUE);
}

// ----------------------------------------------------------------------

static void get_mtmfsk(void) {

	fdc.mt = (fdc.cmd >> 7) & 1;
	fdc.mf = fdc.cmd & 0x40;							// ver0.29
	fdc.sk = (fdc.cmd >> 5) & 1;
}

static void get_hdus(void) {

	fdc.hd = (fdc.cmds[0] >> 2) & 1;
	fdc.us = fdc.cmds[0] & 3;
}

static void get_chrn(void) {

	fdc.C = fdc.cmds[1];
	fdc.H = fdc.cmds[2];
	fdc.R = fdc.cmds[3];
	fdc.N = fdc.cmds[4];
}

static void get_eotgsldtl(void) {

	fdc.eot = fdc.cmds[5];
	fdc.gpl = fdc.cmds[6];
	fdc.dtl = fdc.cmds[7];
}

// --------------------------------------------------------------------------

static void FDC_Invalid(void) {							// cmd: xx

	fdc.event = FDCEVENT_BUFSEND;
	fdc.bufcnt = 1;
	fdc.bufp = 0;
	fdc.buf[0] = 0x80;
	fdc.status = FDCSTAT_RQM | FDCSTAT_CB | FDCSTAT_DIO;
}

#if 0
static void FDC_ReadDiagnostic(void) {					// cmd: 02

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			get_chrn();
			get_eotgsldtl();
			fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;

			if (FDC_DriveCheck(FALSE)) {
				fdc.event = FDCEVENT_BUFSEND;
//				fdc.bufcnt = makedianosedata();
				fdc.bufp = 0;
			}
			break;

		default:
			fdc.event = FDCEVENT_NEUTRAL;
			break;
	}
}
#endif

static void FDC_Specify(void) {							// cmd: 03

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			fdc.srt = fdc.cmds[0] >> 4;
			fdc.hut = fdc.cmds[0] & 0x0f;
			fdc.hlt = fdc.cmds[1] >> 1;
			fdc.nd = fdc.cmds[1] & 1;
			fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
			break;
	}
	fdc.event = FDCEVENT_NEUTRAL;
	fdc.status = FDCSTAT_RQM;
}

static void FDC_SenseDeviceStatus(void) {				// cmd: 04

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			fdc.buf[0] = (fdc.hd << 2) | fdc.us;
			fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
			if (fdc.us < FDC_MAXDRIVE) {
				fdc.buf[0] |= 0x20;
				if (!fddfile[fdc.us].fname[0]) {
					fdc.buf[0] |= 0x08;
				}
				else if (fddfile[fdc.us].protect) {
					fdc.buf[0] |= 0x40;
				}
				if (!fdc.treg[fdc.us]) {
					fdc.buf[0] |= 0x10;
				}
			}
			else {
				fdc.buf[0] |= 0x80;
			}
			fdc.event = FDCEVENT_BUFSEND;
			fdc.bufcnt = 1;
			fdc.bufp = 0;
			fdc.status = FDCSTAT_RQM | FDCSTAT_CB | FDCSTAT_DIO;
			break;

		default:
			fdc.event = FDCEVENT_NEUTRAL;
			fdc.status = FDCSTAT_RQM;
			break;
	}
}

static BOOL writesector(void) {

	fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
	if (!FDC_DriveCheck(TRUE)) {
		return(FAILURE);
	}
	if (fdd_write()) {
		fdc.stat[fdc.us] = fdc.us | (fdc.hd << 2) | FDCRLT_IC0 | FDCRLT_ND;
		fdcsend_error7();
		return(FAILURE);
	}
	fdc.event = FDCEVENT_BUFRECV;
	fdc.bufcnt = 128 << fdc.N;
	fdc.bufp = 0;
	fdc.status = FDCSTAT_RQM | FDCSTAT_NDM | FDCSTAT_CB;
	fdc_dmaready(1);
	dmac_check();
	return(SUCCESS);
}

static void FDC_WriteData(void) {						// cmd: 05
														// cmd: 09
	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			get_chrn();
			get_eotgsldtl();
			fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
			if (FDC_DriveCheck(TRUE)) {
				fdc.event = FDCEVENT_BUFRECV;
				fdc.bufcnt = 128 << fdc.N;
				fdc.bufp = 0;
#if 1															// ver0.27 ??
				fdc.status = FDCSTAT_NDM | FDCSTAT_CB;
				if (!(fdc.ctrlreg & 0x10)) {
					fdc.status |= FDCSTAT_RQM;
				}
#else
				fdc.status = FDCSTAT_RQM | FDCSTAT_NDM | FDCSTAT_CB;
#endif
				fdc_dmaready(1);
				dmac_check();
			}
			break;

		case FDCEVENT_BUFRECV:
			if (writesector()) {
				return;
			}
			if (fdc.tc) {
				fdcsend_success7();
				return;
			}
			if (fdc.R++ == fdc.eot) {
				fdc.stat[fdc.us] = fdc.us | (fdc.hd << 2) |
													FDCRLT_IC0 | FDCRLT_EN;
				fdcsend_error7();
				break;
			}
			break;

		default:
			fdc.event = FDCEVENT_NEUTRAL;
			fdc.status = FDCSTAT_RQM;
			break;
	}
}

static void readsector(void) {

	fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
	if (!FDC_DriveCheck(FALSE)) {
		return;
	}
	if (fdd_read()) {
		fdc.stat[fdc.us] = fdc.us | (fdc.hd << 2) | FDCRLT_IC0 | FDCRLT_ND;
		fdcsend_error7();
		return;
	}
	fdc.event = FDCEVENT_BUFSEND2;
	fdc.bufp = 0;
#if 1															// ver0.27 ??
	fdc.status = FDCSTAT_NDM | FDCSTAT_CB;
	if (!(fdc.ctrlreg & 0x10)) {
		fdc.status |= FDCSTAT_RQM | FDCSTAT_DIO;
	}
#else
	fdc.status = FDCSTAT_RQM | FDCSTAT_DIO | FDCSTAT_NDM | FDCSTAT_CB;
#endif
	fdc_dmaready(1);
	dmac_check();
}

static void FDC_ReadData(void) {						// cmd: 06
														// cmd: 0c
	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			get_chrn();
			get_eotgsldtl();
			readsector();
			break;

		case FDCEVENT_NEXTDATA:
			fdc.bufcnt = 0;
			if (fdc.R++ == fdc.eot) {
				fdc.stat[fdc.us] = fdc.us | (fdc.hd << 2) |
													FDCRLT_IC0 | FDCRLT_EN;
				fdcsend_error7();
				break;
			}
			readsector();
			break;

#ifdef FDC_DELAYERROR7
		case FDCEVENT_BUSY:
			break;
#endif

		default:
			fdc.event = FDCEVENT_NEUTRAL;
			fdc.status = FDCSTAT_RQM;
			break;
	}
}

static void FDC_Recalibrate(void) {						// cmd: 07

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			fdc.ncn = 0;
			fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
			fdc.stat[fdc.us] |= FDCRLT_SE;
			if ((fdc.us >= FDC_MAXDRIVE) || (!fddfile[fdc.us].fname[0])) {
				fdc.stat[fdc.us] |= FDCRLT_NR | FDCRLT_IC0;
			}
			else {
				fdd_seek();
			}
			fdc_interrupt();
			break;
	}
	fdc.event = FDCEVENT_NEUTRAL;
	fdc.status = FDCSTAT_RQM;
}

static void FDC_SenceintStatus(void) {					// cmd: 08

	int		i;

	fdc.event = FDCEVENT_BUFSEND;
	fdc.bufp = 0;
	fdc.bufcnt = 0;
	fdc.status = FDCSTAT_RQM | FDCSTAT_CB | FDCSTAT_DIO;

	if (fdc_isfdcinterrupt()) {
		i = 0;
		if (fdc.stat[fdc.us]) {
			fdc.buf[0] = (BYTE)fdc.stat[fdc.us];
			fdc.buf[1] = fdc.treg[fdc.us];
			fdc.bufcnt = 2;
			fdc.stat[fdc.us] = 0;
		}
		else {
			for (; i<4; i++) {
				if (fdc.stat[i]) {
					fdc.buf[0] = (BYTE)fdc.stat[i];
					fdc.buf[1] = fdc.treg[i];
					fdc.bufcnt = 2;
					fdc.stat[i] = 0;
					break;
				}
			}
		}
		for (; i<4; i++) {
			if (fdc.stat[i]) {
				break;
			}
		}
		if (i>=4) {
			fdc_interruptreset();
		}
	}
	if (!fdc.bufcnt) {
		fdc.buf[0] = FDCRLT_IC1;
		fdc.bufcnt = 1;
	}
}

#if 0	// for test..
static void FDC_ReadID(void) {							// cmd: 0a

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			break;
	}
}
#endif

static void FDC_WriteID(void) {							// cmd: 0d

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
//			TRACE_("FDC_WriteID FDCEVENT_CMDRECV", 0);
			get_hdus();
			fdc.N = fdc.cmds[1];
			fdc.sc = fdc.cmds[2];
			fdc.gpl = fdc.cmds[3];
			fdc.d = fdc.cmds[4];
			if (FDC_DriveCheck(TRUE)) {
//				TRACE_("FDC_WriteID FDC_DriveCheck", 0);
				if (fdd_formatinit()) {
//					TRACE_("FDC_WriteID fdd_formatinit", 0);
					fdcsend_error7();
					break;
				}
//				TRACE_("FDC_WriteID FDCEVENT_BUFRECV", 0);
				fdc.event = FDCEVENT_BUFRECV;
				fdc.bufcnt = 4;
				fdc.bufp = 0;
#if 1															// ver0.27 ??
				fdc.status = FDCSTAT_NDM | FDCSTAT_CB;
				if (!(fdc.ctrlreg & 0x10)) {
					fdc.status |= FDCSTAT_RQM;
				}
#else
				fdc.status = FDCSTAT_RQM | FDCSTAT_NDM | FDCSTAT_CB;
#endif
				fdc_dmaready(1);
				dmac_check();
			}
			break;

		case FDCEVENT_BUFRECV:
			if (fdd_formating(fdc.buf)) {
				fdcsend_error7();
				break;
			}
			if ((fdc.tc) || (!fdd_isformating())) {
				fdcsend_success7();
				return;
			}
			fdc.event = FDCEVENT_BUFRECV;
			fdc.bufcnt = 4;
			fdc.bufp = 0;
#if 1															// ver0.27 ??
			fdc.status = FDCSTAT_NDM | FDCSTAT_CB;
			if (!(fdc.ctrlreg & 0x10)) {
				fdc.status |= FDCSTAT_RQM;
			}
#else
			fdc.status = FDCSTAT_RQM | FDCSTAT_NDM | FDCSTAT_CB;
#endif
			break;

		default:
			fdc.event = FDCEVENT_NEUTRAL;
			fdc.status = FDCSTAT_RQM;
			break;
	}
}

static void FDC_Seek(void) {							// cmd: 0f

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			fdc.ncn = fdc.cmds[1];
			fdc.stat[fdc.us] = (fdc.hd << 2) | fdc.us;
			fdc.stat[fdc.us] |= FDCRLT_SE;
			if ((fdc.us >= FDC_MAXDRIVE) || (!fddfile[fdc.us].fname[0])) {
				fdc.stat[fdc.us] |= FDCRLT_NR | FDCRLT_IC0;
			}
			else {
				fdd_seek();
			}
			fdc_interrupt();
			break;
	}
	fdc.event = FDCEVENT_NEUTRAL;
	fdc.status = FDCSTAT_RQM;
}

#if 0
static void FDC_ScanEqual(void) {						// cmd: 11, 19, 1d

	switch(fdc.event) {
		case FDCEVENT_CMDRECV:
			get_hdus();
			get_chrn();
			fdc.eot = fdc.cmds[5];
			fdc.gpl = fdc.cmds[6];
			fdc.stp = fdc.cmds[7];
			break;
	}
}
#endif

// --------------------------------------------------------------------------

typedef void (*FDCOPE)(void);

static const FDCOPE FDC_Ope[0x20] = {
				FDC_Invalid,
				FDC_Invalid,
				FDC_ReadData,			// FDC_ReadDiagnostic,
				FDC_Specify,
				FDC_SenseDeviceStatus,
				FDC_WriteData,
				FDC_ReadData,
				FDC_Recalibrate,
				FDC_SenceintStatus,
				FDC_WriteData,
				FDC_Invalid,			// FDC_ReadID,
				FDC_Invalid,
				FDC_ReadData,
				FDC_WriteID,
				FDC_Invalid,
				FDC_Seek,
				FDC_Invalid,					// 10
				FDC_Invalid,			// FDC_ScanEqual,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,			// FDC_ScanEqual,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,
				FDC_Invalid,			// FDC_ScanEqual,
				FDC_Invalid,
				FDC_Invalid};


// --------------------------------------------------------------------------

static void fdcstatusreset(void) {

	fdc.event = FDCEVENT_NEUTRAL;
	fdc.status = FDCSTAT_RQM;
}

void DMACCALL fdc_DataRegWrite(BYTE data) {

//	if ((fdc.status & (FDCSTAT_RQM | FDCSTAT_DIO)) == FDCSTAT_RQM) {
		switch(fdc.event) {
			case FDCEVENT_BUFRECV:
//				TRACE_("write", fdc.bufp);
				fdc.buf[fdc.bufp++] = data;
				if ((!(--fdc.bufcnt)) || (fdc.tc)) {
					fdc.status &= ~FDCSTAT_RQM;
					FDC_Ope[fdc.cmd & 0x1f]();
				}
				break;

			case FDCEVENT_CMDRECV:
				fdc.cmds[fdc.cmdp++] = data;
				if (!(--fdc.cmdcnt)) {
					fdc.status &= ~FDCSTAT_RQM;
					FDC_Ope[fdc.cmd & 0x1f]();
				}
				break;

			default:
				fdc.cmd = data;
				get_mtmfsk();
				if (FDCCMD_TABLE[data & 0x1f]) {
					fdc.event = FDCEVENT_CMDRECV;
					fdc.cmdp = 0;
					fdc.cmdcnt = FDCCMD_TABLE[data & 0x1f];
					fdc.status = FDCSTAT_RQM | FDCSTAT_CB;
				}
				else {
					fdc.status &= ~FDCSTAT_RQM;
					FDC_Ope[fdc.cmd & 0x1f]();
				}
				break;
		}
//	}
}


BYTE DMACCALL fdc_DataRegRead(void) {

//	if ((fdc.status & (FDCSTAT_RQM | FDCSTAT_DIO))
//									== (FDCSTAT_RQM | FDCSTAT_DIO)) {
		switch(fdc.event) {
			case FDCEVENT_BUFSEND:
				fdc.lastdata = fdc.buf[fdc.bufp++];
				if (!(--fdc.bufcnt)) {
					fdc.event = FDCEVENT_NEUTRAL;
					fdc.status = FDCSTAT_RQM;
				}
				break;

			case FDCEVENT_BUFSEND2:
				if (fdc.bufcnt) {
//					TRACE_("read data", fdc.bufp);
					fdc.lastdata = fdc.buf[fdc.bufp++];
					fdc.bufcnt--;
				}
				if (fdc.tc) {
					if (!fdc.bufcnt) {						// ver0.26
						fdc.R++;
						if ((fdc.cmd & 0x80) && fdd_seeksector()) {
							fdc.C += fdc.hd;
							fdc.H = fdc.hd ^ 1;
							fdc.R = 1;
						}
					}
					fdcsend_success7();
				}
				if (!fdc.bufcnt) {
					fdc.event = FDCEVENT_NEXTDATA;
					fdc.status &= ~(FDCSTAT_RQM | FDCSTAT_NDM);
					FDC_Ope[fdc.cmd & 0x1f]();
				}
				break;
		}
//	}
	return(fdc.lastdata);
}


// ---- I/O

static void IOOUTCALL fdc_o92(UINT port, BYTE dat) {

	CTRL_FDMEDIA = DISKTYPE_2HD;
	if ((fdc.status & (FDCSTAT_RQM | FDCSTAT_DIO)) == FDCSTAT_RQM) {
		fdc_DataRegWrite(dat);
	}
	(void)port;
}

static void IOOUTCALL fdc_o94(UINT port, BYTE dat) {

	CTRL_FDMEDIA = DISKTYPE_2HD;
	if ((fdc.ctrlreg ^ dat) & 0x10) {
		fdcstatusreset();
		fdc_dmaready(0);
		dmac_check();
	}
	fdc.ctrlreg = dat;
	(void)port;
}

static BYTE IOINPCALL fdc_i90(UINT port) {

	CTRL_FDMEDIA = DISKTYPE_2HD;

	(void)port;
	return(fdc.status);
}

static BYTE IOINPCALL fdc_i92(UINT port) {

	BYTE	ret;

	CTRL_FDMEDIA = DISKTYPE_2HD;
	if ((fdc.status & (FDCSTAT_RQM | FDCSTAT_DIO))
										== (FDCSTAT_RQM | FDCSTAT_DIO)) {
		ret = fdc_DataRegRead();
	}
	else {
		ret = fdc.lastdata;
	}
	(void)port;
	return(ret);
}

static BYTE IOINPCALL fdc_i94(UINT port) {

	CTRL_FDMEDIA = DISKTYPE_2HD;

	(void)port;
	return(0x40);
}


static void IOOUTCALL fdc_oca(UINT port, BYTE dat) {

	CTRL_FDMEDIA = DISKTYPE_2DD;
	fdc_DataRegWrite(dat);
	(void)port;
}

static void IOOUTCALL fdc_occ(UINT port, BYTE dat) {

	CTRL_FDMEDIA = DISKTYPE_2DD;
	fdc.ctrlreg = dat;
	(void)port;
}

static BYTE IOINPCALL fdc_ic8(UINT port) {

	CTRL_FDMEDIA = DISKTYPE_2DD;

	(void)port;
	return(fdc.status);
}

static BYTE IOINPCALL fdc_ica(UINT port) {

	BYTE	ret;

	CTRL_FDMEDIA = DISKTYPE_2DD;
	ret = fdc_DataRegRead();

	(void)port;
	return(ret);
}

static BYTE IOINPCALL fdc_icc(UINT port) {

	CTRL_FDMEDIA = DISKTYPE_2DD;

	(void)port;
	return(0x74);
}


static void IOOUTCALL fdc_obe(UINT port, BYTE dat) {

	fdc.chgreg = dat;
	(void)port;
}

static BYTE IOINPCALL fdc_ibe(UINT port) {

	(void)port;
	return((fdc.chgreg & 3) | 8);
}


// ---- I/F

static const IOOUT fdco90[4] = {
					NULL,		fdc_o92,	fdc_o94,	NULL};
static const IOINP fdci90[4] = {
					fdc_i90,	fdc_i92,	fdc_i94,	NULL};
static const IOOUT fdcoc8[4] = {
					NULL,		fdc_oca,	fdc_occ,	NULL};
static const IOINP fdcic8[4] = {
					fdc_ic8,	fdc_ica,	fdc_icc,	NULL};

static const IOOUT fdcobe[1] = {fdc_obe};
static const IOINP fdcibe[1] = {fdc_ibe};

void fdc_reset(void) {

	ZeroMemory(&fdc, sizeof(fdc));
	fdcstatusreset();
	CTRL_FDMEDIA = DISKTYPE_2HD;
}

void fdc_bind(void) {

	iocore_attachcmnoutex(0x0090, 0x00f9, fdco90, 4);
	iocore_attachcmninpex(0x0090, 0x00f9, fdci90, 4);
	iocore_attachcmnoutex(0x00c8, 0x00f9, fdcoc8, 4);
	iocore_attachcmninpex(0x00c8, 0x00f9, fdcic8, 4);

	iocore_attachsysoutex(0x00be, 0x0cff, fdcobe, 1);
	iocore_attachsysinpex(0x00be, 0x0cff, fdcibe, 1);
}

