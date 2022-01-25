/*******************************************************************************
 * libretroshare/src/rsserver: p3serverconfig.h                                *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2011-2011 by Robert Fernie <retroshare@lunamutt.com>              *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/
#ifndef LIBRETROSHARE_CONFIG_IMPLEMENTATION_H
#define LIBRETROSHARE_CONFIG_IMPLEMENTATION_H

#include "retroshare/rsconfig.h"
#include "pqi/p3peermgr.h"
#include "pqi/p3linkmgr.h"
#include "pqi/p3netmgr.h"
#include "pqi/p3cfgmgr.h"
#include "pqi/pqihandler.h"

class p3ServerConfig: public RsServerConfig
{
	public:

	p3ServerConfig(p3PeerMgr *peerMgr, p3LinkMgr *linkMgr, p3NetMgr *netMgr, pqihandler *pqih, p3GeneralConfig *genCfg);
	virtual ~p3ServerConfig() = default;

	void load_config();

	/* From RsIface::RsConfig */

	virtual int 	getConfigNetStatus(RsConfigNetStatus &status) override;
	virtual int 	getConfigStartup(RsConfigStartup &params);
	//virtual int 	getConfigDataRates(RsConfigDataRates &params);

	/***** for RsConfig -> p3BandwidthControl ****/

	virtual int getTotalBandwidthRates(RsConfigDataRates &rates) override;
	virtual int getAllBandwidthRates(std::map<RsPeerId, RsConfigDataRates> &ratemap) override;
	virtual int getTrafficInfo(std::list<RSTrafficClue>& out_lst, std::list<RSTrafficClue> &in_lst) override;

	/* From RsInit */

	virtual std::string      RsConfigDirectory();
	virtual std::string      RsConfigKeysDirectory();

	virtual std::string  RsProfileConfigDirectory();
	virtual bool         getStartMinimised();
	virtual std::string  getRetroShareLink();

	virtual bool getAutoLogin();
	virtual void setAutoLogin(bool autoLogin);
	virtual bool RsClearAutoLogin();

	virtual std::string getRetroshareDataDirectory();

	/* New Stuff */

	virtual RsConfigUserLvl getUserLevel() override;

	virtual RsNetState getNetState() override;
	virtual RsNetworkMode getNetworkMode() override;
	virtual RsNatTypeMode getNatTypeMode() override;
	virtual RsNatHoleMode getNatHoleMode() override;
	virtual RsConnectModes getConnectModes() override;

	virtual bool getConfigurationOption(uint32_t key, std::string &opt) override;
	virtual bool setConfigurationOption(uint32_t key, const std::string &opt) override;

	/* Operating Mode */
	virtual RsOpMode getOperatingMode() override;
	virtual bool     setOperatingMode(RsOpMode opMode) override;
	virtual bool     setOperatingMode(const std::string &opModeStr) override;

	virtual int SetMaxDataRates(int inKb, int outKb) override {return setMaxDataRates(inKb,outKb,inKb,outKb);}
	virtual int GetMaxDataRates(int &inKb, int &outKb) override {int inKBWhenIdle; int outKBWhenIdle;return getMaxDataRates(inKb,outKb,inKBWhenIdle,outKBWhenIdle);}
	virtual int setMaxDataRates(int inKb, int outKb , int inKBWhenIdle, int outKBWhenIdle) override;
	virtual int getMaxDataRates(int &inKb, int &outKb , int &inKBWhenIdle, int &outKBWhenIdle) override;
	virtual int GetCurrentDataRates( float &inKb, float &outKb ) override;
	virtual int GetTrafficSum( uint64_t &inb, uint64_t &outb ) override;

	virtual void setIsIdle(bool isIdle) override;

	/********************* ABOVE is RsConfig Interface *******/

private:

	bool switchToOperatingMode(RsOpMode opMode);

	bool findConfigurationOption(uint32_t key, std::string &keystr);

	p3PeerMgr *mPeerMgr;
	p3LinkMgr *mLinkMgr;
	p3NetMgr  *mNetMgr;
	pqihandler *mPqiHandler;
	p3GeneralConfig *mGeneralConfig;

	RsMutex configMtx;
	RsConfigUserLvl mUserLevel; // store last one... will later be a config Item too.
	float mRateDownload;
	float mRateUpload;
	float mRateDownloadWhenIdle;
	float mRateUploadWhenIdle;
	bool mIsIdle;

	RsOpMode mOpMode;
};

#endif
