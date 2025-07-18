/*
 * bitstreamout.c:	VDR plugin for redirecting 16bit encoded audio streams
 *			(mainly AC3) received from a DVB card or VDR recording
 *			to S/P-DIF out of a sound card with ALSA (NO decoding!).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 * Copyright (C) 2002-2005 Werner Fink, <werner@suse.de>
 */

#include <stdint.h>
#include <time.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/rtc.h>
#include <linux/dvb/dmx.h>
#include <vdr/plugin.h>
#include <vdr/interface.h>
#include <vdr/dvbdevice.h>

#include "types.h"
#include "spdif.h"
#include "replay.h"
#include "channel.h"
#include "bitstreamout.h"
#include "ac3.h"
#include "dts.h"
#include "lpcm.h"
#include "mp2.h"
#include "shm_memory_tool.h"

static const char *version	 = VERSION;
static const char *description	 = "bit stream out to S/P-DIF of a sound card";
static const char *mainmenuentry = "Bitstreamout";
static ctrl_t setup = {
    NULL,	// buf
    ((1<<SETUP_ACTIVE)|(1<<SETUP_MP2DITHER)|(1<<SETUP_MP2ENABLE)|(1<<SETUP_CLEAR)),
    {
	0,	// opt.card
	2,	// opt.device
	0,	// opt.delay
	0,	// opt.ldelay
	7,	// opt.mdelay
	0,	// opt.adelay
	false,	// opt.type
	true,	// opt.variable
	false	// opt.mmap
    }
};

#define MAP_MEM		(MAP_SHARED|MAP_NORESERVE|MAP_LOCKED)
#define MAINMENUTIMEOUT	5000 //ms

class cBitStreamOut : public cPlugin {
private:
    cReplayOutSPDif  *ReplayOutSPDif;
    cChannelOutSPDif *ChannelOutSPDif;
    static spdif spdifDev;
    static cBounce * bounce;
    bool onoff;
    bool mp2dec;
    char *SPDIFmute;
    unsigned long rtc;
protected:
    int active;
    int mp2spdif;
    int mp2enable;
public:
    cBitStreamOut(void);
    virtual ~cBitStreamOut(void);
    virtual bool Start(void);
    virtual void Stop(void);
    virtual const char *Version(void);
    virtual const char *Description(void);
    virtual const char *CommandLineHelp(void);
    virtual bool ProcessArgs(int argc, char *argv[]);
    virtual cMenuSetupPage *SetupMenu(void);
    virtual bool SetupParse(const char *Name, const char *Value);
    virtual const char *MainMenuEntry(void);
    virtual cOsdObject *MainMenuAction(void);
};

class cMenuSetupBSO : public cMenuSetupPage {
private:
    virtual void Set(void);
protected:
    static opt_t opt;
    int active;
    int mp2spdif;
    int mp2enable;
    virtual void Store(void);
public:
    cMenuSetupBSO(void) { Set(); };
};

class cDisplayMainMenu : public cMenuSetupPage {
private:
    cChannelOutSPDif *ChannelOutSPDif;
    cReplayOutSPDif  *ReplayOutSPDif;
    char        apidstrs[MAXAPIDS][255];
    const char *apidstrsptr[MAXAPIDS+1];
    int         apidstrnum;
    virtual void Set(void);
protected:
    static opt_t opt;
    int active;
    int mp2spdif;
    int mp2enable;
    int apidstrid;
    virtual void Store(void);
public:
    cDisplayMainMenu(cChannelOutSPDif *COSPDif, cReplayOutSPDif *ROSPDif) :
    ChannelOutSPDif(COSPDif), ReplayOutSPDif(ROSPDif) { Set(); };
};

static const char * mp2out[] = {"Round", "Dither", "Nonlinear"};
static const int    mp2num   = sizeof(mp2out)/sizeof(*mp2out);

// --- cBitStreamOut : VDR interface for redirecting none audio streams ----------------

spdif cBitStreamOut::spdifDev(setup);
cBounce * cBitStreamOut::bounce;

cBitStreamOut::cBitStreamOut(void)
{
    setup.buf = NULL;
    bounce = NULL;
    onoff = false;
    mp2dec = false;
    SPDIFmute = NULL;
    ChannelOutSPDif = NULL;
    ReplayOutSPDif = NULL;
    mp2spdif = 1;			// Default is `Dither'
    rtc = 0;
}

cBitStreamOut::~cBitStreamOut(void)
{
    if (SPDIFmute)
	free(SPDIFmute);
    SPDIFmute = NULL;

    if (bounce)
	delete bounce;
    bounce = NULL;

    if (setup.buf)
	shm_free(setup.buf);
    setup.buf = NULL;

    if (mp2dec)
	mp2.Release();
}

void cBitStreamOut::Stop(void)
{
    set_setup(CLEAR);
    if (ReplayOutSPDif) {
	ReplayOutSPDif->Clear();
#ifdef VDR_DOES_CHECK_OBJECT
	delete ReplayOutSPDif;
	ReplayOutSPDif = NULL;
#endif
    }

    if (ChannelOutSPDif) {
	ChannelOutSPDif->Clear();
	delete ChannelOutSPDif;
	ChannelOutSPDif = NULL;
    }

    if (spdifDev)
	spdifDev.Close();

#if 0
    if (rtc) {
        int fd;
        if ((fd = open ("/dev/rtc", O_RDONLY)) >= 0) {
	    ioctl(fd, RTC_IRQP_SET, rtc);
	    errno = 0;
	    close(fd);
	}

    }
#endif
}

const char *cBitStreamOut::Version(void)
{
    return version;
}

const char *cBitStreamOut::Description(void)
{
    return description;
}

const char *cBitStreamOut::MainMenuEntry(void)
{
    return onoff ? mainmenuentry : NULL;
}

bool cBitStreamOut::Start(void)
{
    setup.buf = (uint_8*)shm_malloc(sizeof(uint_8)*OVERALL_MEM, MAP_MEM);

    if (setup.buf == NULL) {
	esyslog("cBitStreamOut::Start() shm_malloc failed\n");
	goto err;
    }

    if (!(mp2dec = mp2.Initialize())) {
       esyslog("cBitStreamOut::Start() mp2 initialization failed\n");
       goto err;
    }

    if (!(bounce = new cBounce(setup.buf, BOUNCE_MEM)))
	goto err;

    ac3.SetBuffer(setup.buf + SPDIF_START);
    dts.SetBuffer(setup.buf + SPDIF_START);
    pcm.SetBuffer(setup.buf + SPDIF_START);
    mp2.SetBuffer(setup.buf + SPDIF_START);

    if (!(ReplayOutSPDif = new cReplayOutSPDif(spdifDev, setup, bounce, SPDIFmute)))
	goto err;

    if (!(ChannelOutSPDif = new cChannelOutSPDif(spdifDev, setup, bounce, SPDIFmute)))
	goto err;

    if (getuid() == 0) {			// Give maximum speed for ALSA
	int fd;

	if ((fd = open ("/dev/rtc", O_RDONLY)) >= 0) {
	    if (ioctl(fd, RTC_IRQP_READ, &rtc) == 0) {
		unsigned long tmp = 8192;
		ioctl(fd, RTC_IRQP_SET, tmp);
		errno = 0;
	    }
	    close(fd);
	}
    }

    clear_setup(CLEAR);
    return true;
err:
    set_setup(CLEAR);
    Stop();
    return false;
}

const char *cBitStreamOut::CommandLineHelp(void)
{
    return "  -o,        --onoff        enable an control entry in the main menu\n"
	   "  -m script, --mute=script  script for en/dis-able the spdif interface\n";
}

bool cBitStreamOut::ProcessArgs(int argc, char *argv[])
{
    bool ret = true;
    const struct option long_option[] =
    {
	{ "onoff", no_argument,		NULL, 'o' },
	{ "mute",  required_argument,	NULL, 'm' },
	{  NULL,   no_argument,		NULL,  0  },
    };

    int c = 0;

    // Reset all opt variables to their initial values because vdr has its
    // own options already scanned.
    optarg = NULL;
    optind = opterr = optopt = 0;
    while ((c = getopt_long(argc, argv, "om:", long_option, NULL)) > 0) {
	switch (c) {
	case 'o':
	    onoff = true;
	    break;
	case 'm':
	    if (SPDIFmute)
		free(SPDIFmute);
	    if (!(SPDIFmute = strdup(optarg))) {
		esyslog("ERROR: out of memory");
		ret = false;
	    }
	    break;
	default:
	    ret = false;
	    break;
	}
    }
    return ret;
}

cMenuSetupPage *cBitStreamOut::SetupMenu(void)
{
    return new cMenuSetupBSO;
}

bool cBitStreamOut::SetupParse(const char *Name, const char *Value)
{
    bool mp2new = false;
    bool ret = true;
    if      (!strcasecmp(Name, "Card"))       setup.opt.card     = atoi(Value);
    else if (!strcasecmp(Name, "Device"))     setup.opt.device   = atoi(Value);
    else if (!strcasecmp(Name, "Delay"))      setup.opt.delay    = atoi(Value);
    else if (!strcasecmp(Name, "LiveDelay"))  setup.opt.ldelay   = atoi(Value);
    else if (!strcasecmp(Name, "PCMinit"))    setup.opt.mdelay   = atoi(Value);
    else if (!strcasecmp(Name, "Mp2offset"))  setup.opt.adelay   = atoi(Value);
    else if (!strcasecmp(Name, "IEC958"))     setup.opt.type     = atoi(Value);
    else if (!strcasecmp(Name, "VariableIO")) setup.opt.variable = atoi(Value);
    else if (!strcasecmp(Name, "MemoryMap"))  setup.opt.mmap     = atoi(Value);
    else if (!strcasecmp(Name, "Active")) {
	(active    = atoi(Value)) ? set_setup(ACTIVE)    : clear_setup(ACTIVE);
    } else if (!strcasecmp(Name, "Mp2Enable")) {
	(mp2enable = atoi(Value)) ? set_setup(MP2ENABLE) : clear_setup(MP2ENABLE);
    } else if (!strcasecmp(Name, "Mp2Dither") && !mp2new) {
	(mp2spdif = atoi(Value)) ? set_setup(MP2DITHER)  : clear_setup(MP2DITHER);
    } else if (!strcasecmp(Name, "Mp2Out")) {
	mp2new = true;
	for (mp2spdif = 0; mp2spdif < mp2num; mp2spdif++) {
	    if (!strcasecmp(Value, mp2out[mp2spdif]))
		break;
	}
	switch (mp2spdif) {
	default: mp2spdif = 0;
	case 0:  clear_setup(MP2DITHER); clear_setup(MP2SPDIF); break;
	case 1:  set_setup  (MP2DITHER); clear_setup(MP2SPDIF); break;
	case 2:  set_setup  (MP2DITHER); set_setup  (MP2SPDIF); break;
	}
    } else
	ret = false;
    if (setup.opt.mdelay < 4)
	setup.opt.mdelay = 4;

    if (active)
	cDvbDevice::SetTransferModeForDolbyDigital(false);
    else
	cDvbDevice::SetTransferModeForDolbyDigital(true);

    return ret;
}

cOsdObject *cBitStreamOut::MainMenuAction(void)
{
    cOsdObject * ret = NULL;
    if (onoff)
	ret = new cDisplayMainMenu(ChannelOutSPDif, ReplayOutSPDif);
    return ret;
}

// --- cDisplayMainMenu ----------------------------------------------------------------

opt_t cDisplayMainMenu::opt;

void cDisplayMainMenu::Set(void)
{
    char title[255];
    cPlugin * bso = cPluginManager::GetPlugin("bitstreamout");

    SetPlugin(bso);

    memcpy(&opt, &(setup.opt), sizeof(opt_t));
    active    = ((test_setup(ACTIVE))    ? true : false);
    mp2enable = ((test_setup(MP2ENABLE)) ? true : false);
    mp2spdif  = ((test_setup(MP2SPDIF)) ? 2 : ((test_setup(MP2DITHER)) ? 1 : 0));

    snprintf(title, 255, "%s (%s%s)", mainmenuentry,
	     (active) ? "AC3" : "Off", (mp2enable) ? "/MP2" : "");
    SetTitle(title);

    Add(new cMenuEditBoolItem("On/Off",     &(active),       "Off", "On" ));
    Add(new cMenuEditBoolItem("Mp2Enable",  &(mp2enable),    "Off", "On" ));
    Add(new cMenuEditStraItem("Mp2Out",     &(mp2spdif),    mp2num, mp2out));
    Add(new cMenuEditIntItem ("Delay",      &(opt.delay),     0,     32  ));
    Add(new cMenuEditIntItem ("LiveDelay",  &(opt.ldelay),    0,     32  ));
    Add(new cMenuEditIntItem ("PCMinital",  &(opt.mdelay),    4,     12  ));
    Add(new cMenuEditIntItem ("Mp2offset",  &(opt.adelay),    0,     12  ));

    (active)    ? set_setup(ACTIVE)    : clear_setup(ACTIVE);
    (mp2enable) ? set_setup(MP2ENABLE) : clear_setup(MP2ENABLE);
    switch (mp2spdif) {
    default: mp2spdif = 0;
    case 0:  clear_setup(MP2DITHER); clear_setup(MP2SPDIF); break;
    case 1:  set_setup  (MP2DITHER); clear_setup(MP2SPDIF); break;
    case 2:  set_setup  (MP2DITHER); set_setup  (MP2SPDIF); break;
    }

    if (active)
	cDvbDevice::SetTransferModeForDolbyDigital(false);
    else
	cDvbDevice::SetTransferModeForDolbyDigital(true);

    debug("cDisplayMainMenu::Set\n");
}

void cDisplayMainMenu::Store(void)
{
    SetupStore("Delay",      setup.opt.delay    = opt.delay);
    SetupStore("LiveDelay",  setup.opt.ldelay   = opt.ldelay);
    SetupStore("PCMinit",    setup.opt.mdelay   = opt.mdelay);
    SetupStore("Mp2offset",  setup.opt.adelay   = opt.adelay);
    SetupStore("Active",     ((active)    ? true : false));
    SetupStore("Mp2Enable",  ((mp2enable) ? true : false));
    SetupStore("Mp2Out",     mp2out[mp2spdif]);
    (active)    ? set_setup(ACTIVE)    : clear_setup(ACTIVE);
    (mp2enable) ? set_setup(MP2ENABLE) : clear_setup(MP2ENABLE);
    switch (mp2spdif) {
    default:
    case 0: clear_setup(MP2DITHER); clear_setup(MP2SPDIF); break;
    case 1: set_setup  (MP2DITHER); clear_setup(MP2SPDIF); break;
    case 2: clear_setup(MP2DITHER); set_setup  (MP2SPDIF); break;
    }
    set_setup(RESET);

    debug("cDisplayMainMenu::Store set apid idx: %d/%d\n", apidstrid, apidstrnum);

    if (active)
	cDvbDevice::SetTransferModeForDolbyDigital(false);
    else
	cDvbDevice::SetTransferModeForDolbyDigital(true);
    debug("cDisplayMainMenu::Store\n");
}

// --- cMenuSetupBSO -------------------------------------------------------------------

opt_t cMenuSetupBSO::opt;

void cMenuSetupBSO::Set(void)
{
    memcpy(&opt, &(setup.opt), sizeof(opt_t));
    active    = ((test_setup(ACTIVE))    ? true : false);
    mp2enable = ((test_setup(MP2ENABLE)) ? true : false);
    mp2spdif  = ((test_setup(MP2SPDIF)) ? 2 : ((test_setup(MP2DITHER)) ? 1 : 0));
    Add(new cMenuEditBoolItem("On/Off",     &(active),       "Off", "On" ));
    Add(new cMenuEditBoolItem("Mp2Enable",  &(mp2enable),    "Off", "On" ));
    Add(new cMenuEditStraItem("Mp2Out",     &(mp2spdif),    mp2num, mp2out));
    Add(new cMenuEditIntItem ("Card",       &(opt.card),      0,     8-1 ));
    Add(new cMenuEditIntItem ("Device",     &(opt.device),    0,    32-1 ));
    Add(new cMenuEditIntItem ("Delay",      &(opt.delay),     0,     32  ));
    Add(new cMenuEditIntItem ("LiveDelay",  &(opt.ldelay),    0,     32  ));
    Add(new cMenuEditIntItem ("PCMinital",  &(opt.mdelay),    4,     12  ));
    Add(new cMenuEditIntItem ("Mp2offset",  &(opt.adelay),    0,     12  ));
    Add(new cMenuEditBoolItem("IEC958",     &(opt.type),     "Con", "Pro"));
    Add(new cMenuEditBoolItem("VariableIO", &(opt.variable), "No",  "Yes"));
    Add(new cMenuEditBoolItem("MemoryMap",  &(opt.mmap),     "No",  "Yes"));
    (active)    ? set_setup(ACTIVE)    : clear_setup(ACTIVE);
    (mp2enable) ? set_setup(MP2ENABLE) : clear_setup(MP2ENABLE);
    switch (mp2spdif) {
    default:
    case 0: clear_setup(MP2DITHER); clear_setup(MP2SPDIF); break;
    case 1: set_setup  (MP2DITHER); clear_setup(MP2SPDIF); break;
    case 2: clear_setup(MP2DITHER); set_setup  (MP2SPDIF); break;
    }

    if (active)
	cDvbDevice::SetTransferModeForDolbyDigital(false);
    else
	cDvbDevice::SetTransferModeForDolbyDigital(true);
}

void cMenuSetupBSO::Store(void)
{
    SetupStore("Card",       setup.opt.card     = opt.card);
    SetupStore("Device",     setup.opt.device   = opt.device);
    SetupStore("Delay",      setup.opt.delay    = opt.delay);
    SetupStore("LiveDelay",  setup.opt.ldelay   = opt.ldelay);
    SetupStore("PCMinit",    setup.opt.mdelay   = opt.mdelay);
    SetupStore("Mp2offset",  setup.opt.adelay   = opt.adelay);
    SetupStore("IEC958",     setup.opt.type     = opt.type);
    SetupStore("VariableIO", setup.opt.variable = opt.variable);
    SetupStore("MemoryMap",  setup.opt.mmap     = opt.mmap);
    SetupStore("Active",     ((active)    ? true : false));
    SetupStore("Mp2Enable",  ((mp2enable) ? true : false));
    SetupStore("Mp2Out",     mp2out[mp2spdif]);
    (active)    ? set_setup(ACTIVE)    : clear_setup(ACTIVE);
    (mp2enable) ? set_setup(MP2ENABLE) : clear_setup(MP2ENABLE);
    switch (mp2spdif) {
    default:
    case 0: clear_setup(MP2DITHER); clear_setup(MP2SPDIF); break;
    case 1: set_setup  (MP2DITHER); clear_setup(MP2SPDIF); break;
    case 2: clear_setup(MP2DITHER); set_setup  (MP2SPDIF); break;
    }

    if (active)
	cDvbDevice::SetTransferModeForDolbyDigital(false);
    else
	cDvbDevice::SetTransferModeForDolbyDigital(true);

    set_setup(RESET);
}

VDRPLUGINCREATOR(cBitStreamOut);
