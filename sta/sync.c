/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include "rt_config.h"


#define ADHOC_ENTRY_BEACON_LOST_TIME	(2*OS_HZ)	/* 2 sec */

/*
	==========================================================================
	Description:
		The sync state machine,
	Parameters:
		Sm - pointer to the state machine
	Note:
		the state machine looks like the following

	==========================================================================
 */
void SyncStateMachineInit(
	IN struct rtmp_adapter *pAd,
	IN STATE_MACHINE *Sm,
	OUT STATE_MACHINE_FUNC Trans[])
{
	StateMachineInit(Sm, Trans, MAX_SYNC_STATE, MAX_SYNC_MSG, (STATE_MACHINE_FUNC)Drop, SYNC_IDLE, SYNC_MACHINE_BASE);

	/* column 1 */
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeScanReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_FORCE_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeForceScanReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_FORCE_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeForceJoinReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)MlmeStartReqAction);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeacon);
	StateMachineSetAction(Sm, SYNC_IDLE, MT2_PEER_PROBE_REQ, (STATE_MACHINE_FUNC)PeerProbeReqAction);

	/* column 2 */
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenStart);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeaconAtJoinAction);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_BEACON_TIMEOUT, (STATE_MACHINE_FUNC)BeaconTimeoutAtJoinAction);
	StateMachineSetAction(Sm, JOIN_WAIT_BEACON, MT2_PEER_PROBE_RSP, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);

	/* column 3 */
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_JOIN_REQ, (STATE_MACHINE_FUNC)MlmeJoinReqAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_START_REQ, (STATE_MACHINE_FUNC)InvalidStateWhenStart);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_PEER_PROBE_RSP, (STATE_MACHINE_FUNC)PeerBeaconAtScanAction);
	StateMachineSetAction(Sm, SCAN_LISTEN, MT2_SCAN_TIMEOUT, (STATE_MACHINE_FUNC)ScanTimeoutAction);
	/* StateMachineSetAction(Sm, SCAN_LISTEN, MT2_MLME_SCAN_CNCL, (STATE_MACHINE_FUNC)ScanCnclAction); */

	/* resume scanning for fast-roaming */
	StateMachineSetAction(Sm, SCAN_PENDING, MT2_MLME_SCAN_REQ, (STATE_MACHINE_FUNC)MlmeScanReqAction);
       StateMachineSetAction(Sm, SCAN_PENDING, MT2_PEER_BEACON, (STATE_MACHINE_FUNC)PeerBeacon);

	/* timer init */
	RTMPInitTimer(pAd, &pAd->MlmeAux.BeaconTimer, GET_TIMER_FUNCTION(BeaconTimeout), pAd, false);
	RTMPInitTimer(pAd, &pAd->MlmeAux.ScanTimer, GET_TIMER_FUNCTION(ScanTimeout), pAd, false);
}

/*
	==========================================================================
	Description:
		Beacon timeout handler, executed in timer thread

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void BeaconTimeout(
	IN void *SystemSpecific1,
	IN void *FunctionContext,
	IN void *SystemSpecific2,
	IN void *SystemSpecific3)
{
	struct rtmp_adapter*pAd = (struct rtmp_adapter*)FunctionContext;

	DBGPRINT(RT_DEBUG_TRACE,("SYNC - BeaconTimeout\n"));

	/*
	    Do nothing if the driver is starting halt state.
	    This might happen when timer already been fired before cancel timer with mlmehalt
	*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;

#ifdef DOT11_N_SUPPORT
	if ((pAd->CommonCfg.BBPCurrentBW == BW_40)
	)
	{
		rtmp_bbp_set_bw(pAd, BW_40);

		AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, false);
		AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - End of SCAN, restore to 40MHz channel %d, Total BSS[%02d]\n",
									pAd->CommonCfg.CentralChannel, pAd->ScanTab.BssNr));
	}
#endif /* DOT11_N_SUPPORT */

	MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_BEACON_TIMEOUT, 0, NULL, 0);
	RTMP_MLME_HANDLER(pAd);
}

/*
	==========================================================================
	Description:
		Scan timeout handler, executed in timer thread

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void ScanTimeout(
	IN void *SystemSpecific1,
	IN void *FunctionContext,
	IN void *SystemSpecific2,
	IN void *SystemSpecific3)
{
	struct rtmp_adapter*pAd = (struct rtmp_adapter*)FunctionContext;


	/*
	    Do nothing if the driver is starting halt state.
	    This might happen when timer already been fired before cancel timer with mlmehalt
	*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		return;

	if (MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_SCAN_TIMEOUT, 0, NULL, 0))
	{
		RTMP_MLME_HANDLER(pAd);
	}
	else
	{
		/* To prevent SyncMachine.CurrState is SCAN_LISTEN forever. */
		pAd->MlmeAux.Channel = 0;
		ScanNextChannel(pAd, OPMODE_STA);
	}
}


void MlmeForceJoinReqAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	bool        TimerCancelled;
	HEADER_802_11 Hdr80211;
	ULONG         FrameLen = 0;
	u8 *       pOutBuffer = NULL;
	u8 *       pSupRate = NULL;
	u8         SupRateLen;
	u8 *       pExtRate = NULL;
	u8         ExtRateLen;
	u8         ASupRate[] = {0x8C, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6C};
	u8         ASupRateLen = sizeof(ASupRate)/sizeof(u8);
	MLME_JOIN_REQ_STRUCT *pInfo = (MLME_JOIN_REQ_STRUCT *)(Elem->Msg);

	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeForeJoinReqAction(BSS #%ld)\n", pInfo->BssIdx));

	/* reset all the timers */
	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
	RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

	{
		memset(pAd->MlmeAux.Ssid, 0, MAX_LEN_OF_SSID);
		memmove(pAd->MlmeAux.Ssid, pAd->StaCfg.ConnectinfoSsid, pAd->StaCfg.ConnectinfoSsidLen);
		pAd->MlmeAux.SsidLen = pAd->StaCfg.ConnectinfoSsidLen;
	}

	pAd->MlmeAux.BssType = pAd->StaCfg.ConnectinfoBssType;
	pAd->MlmeAux.Channel = pAd->StaCfg.ConnectinfoChannel;


	/* Let BBP register at 20MHz to do scan */
	AsicSetChannel(pAd, pAd->MlmeAux.Channel, BW_20, EXTCHA_NONE, false);
	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));

	RTMPSetTimer(&pAd->MlmeAux.BeaconTimer, JOIN_TIMEOUT);

    do
	{
		/*
	    send probe request
	*/
	pOutBuffer = kmalloc(MGMT_DMA_BUFFER_SIZE, GFP_ATOMIC);
	if (pOutBuffer) {
		if (pAd->MlmeAux.Channel <= 14)
		{
			pSupRate = pAd->CommonCfg.SupRate;
			SupRateLen = pAd->CommonCfg.SupRateLen;
			pExtRate = pAd->CommonCfg.ExtRate;
			ExtRateLen = pAd->CommonCfg.ExtRateLen;
		}
		else
		{
			/*
		           Overwrite Support Rate, CCK rate are not allowed
		*/
			pSupRate = ASupRate;
			SupRateLen = ASupRateLen;
			ExtRateLen = 0;
		}

		if ((pAd->MlmeAux.BssType == BSS_INFRA)  && (!MAC_ADDR_EQUAL(ZERO_MAC_ADDR, pAd->StaCfg.ConnectinfoBssid)))
		{
			memcpy(pAd->MlmeAux.Bssid, pAd->StaCfg.ConnectinfoBssid, ETH_ALEN);
			MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, pAd->MlmeAux.Bssid,
								pAd->MlmeAux.Bssid);
		}
		else
			MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR,
								BROADCAST_ADDR);

		MakeOutgoingFrame(pOutBuffer,               &FrameLen,
						  sizeof(HEADER_802_11),    &Hdr80211,
						  1,                        &SsidIe,
						  1,                        &pAd->MlmeAux.SsidLen,
						  pAd->MlmeAux.SsidLen,	    pAd->MlmeAux.Ssid,
						  1,                        &SupRateIe,
						  1,                        &SupRateLen,
						  SupRateLen,               pSupRate,
						  END_OF_ARGS);

		if (ExtRateLen)
		{
			ULONG Tmp;
			MakeOutgoingFrame(pOutBuffer + FrameLen,            &Tmp,
							  1,                                &ExtRateIe,
							  1,                                &ExtRateLen,
							  ExtRateLen,                       pExtRate,
							  END_OF_ARGS);
			FrameLen += Tmp;
	}



#ifdef WPA_SUPPLICANT_SUPPORT
		if ((pAd->OpMode == OPMODE_STA) &&
			(pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) &&
			(pAd->StaCfg.WpsProbeReqIeLen != 0))
	{
			ULONG 		WpsTmpLen = 0;

			MakeOutgoingFrame(pOutBuffer + FrameLen,              &WpsTmpLen,
							pAd->StaCfg.WpsProbeReqIeLen,	pAd->StaCfg.pWpsProbeReqIe,
							END_OF_ARGS);

			FrameLen += WpsTmpLen;
		}
#endif /* WPA_SUPPLICANT_SUPPORT */

		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		kfree(pOutBuffer);
	}
    } while (false);

	DBGPRINT(0, ("FORCE JOIN SYNC - Switch to ch %d, Wait BEACON from %02x:%02x:%02x:%02x:%02x:%02x\n",
		pAd->StaCfg.ConnectinfoChannel, PRINT_MAC(pAd->StaCfg.ConnectinfoBssid)));

	pAd->Mlme.SyncMachine.CurrState = JOIN_WAIT_BEACON;
}


void MlmeForceScanReqAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	u8          Ssid[MAX_LEN_OF_SSID], SsidLen, ScanType, BssType;
	bool        TimerCancelled;
	ULONG		   Now;
	USHORT         Status;

#ifdef RTMP_MAC_USB
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
			ASIC_RADIO_ON(pAd, MLME_RADIO_ON);
#endif /* RTMP_MAC_USB */
       /*
	    Check the total scan tries for one single OID command
	    If this is the CCX 2.0 Case, skip that!
	*/
	if ( !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeForceScanReqAction before Startup\n"));
		return;
	}


	/* first check the parameter sanity */
	if (MlmeScanReqSanity(pAd,
						  Elem->Msg,
						  Elem->MsgLen,
						  &BssType,
						  (char *)Ssid,
						  &SsidLen,
						  &ScanType))
	{

		/*
		     Check for channel load and noise hist request
		     Suspend MSDU only at scan request, not the last two mentioned
		     Suspend MSDU transmission here
		*/
		RTMPSuspendMsduTransmission(pAd);

		/*
		    To prevent data lost.
		    Send an NULL data with turned PSM bit on to current associated AP before SCAN progress.
		    And should send an NULL data with turned PSM bit off to AP, when scan progress done
		*/
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && (INFRA_ON(pAd)))
		{
			RTMPSendNullFrame(pAd,
							  pAd->CommonCfg.TxRate,
							  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? true:false),
							  PWR_SAVE);


			DBGPRINT(RT_DEBUG_TRACE, ("MlmeForceScanReqAction -- Send PSM Data frame for off channel RM, SCAN_IN_PROGRESS=%d!\n",
											RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)));
				OS_WAIT(20);
		}

		NdisGetSystemUpTime(&Now);
		pAd->StaCfg.LastScanTime = Now;
		/* reset all the timers */
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);

		/* record desired BSS parameters */
		pAd->MlmeAux.BssType = BssType;
		pAd->MlmeAux.ScanType = ScanType;
		pAd->MlmeAux.SsidLen = SsidLen;
       	 memset(pAd->MlmeAux.Ssid, 0, MAX_LEN_OF_SSID);
		memmove(pAd->MlmeAux.Ssid, Ssid, SsidLen);

		/*
			Scanning was pending (for fast scanning)
		*/
		if ((pAd->StaCfg.bImprovedScan) && (pAd->Mlme.SyncMachine.CurrState == SCAN_PENDING))
		{
			pAd->MlmeAux.Channel = pAd->StaCfg.LastScanChannel;
		}
		else
		{
			if (pAd->StaCfg.bFastConnect && (pAd->CommonCfg.Channel != 0) && !pAd->StaCfg.bNotFirstScan)
			{
		pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;
			}
			else
				/* start from the first channel */
				pAd->MlmeAux.Channel = FirstChannel(pAd);
		}

		/* Let BBP register at 20MHz to do scan */
		rtmp_bbp_set_bw(pAd, BW_20);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
		/* Before scan, reset trigger event table. */
		TriEventInit(pAd);
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

		ScanNextChannel(pAd, OPMODE_STA);
		if(pAd->StaCfg.ConnectinfoChannel != 0)
			pAd->MlmeAux.Channel = 0;
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_SCAN_FOR_CONNECT;
	}
	else
	{
		DBGPRINT_ERR(("SYNC - MlmeForceScanReqAction() sanity check fail\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status, 0);
	}
}



/*
	==========================================================================
	Description:
		MLME SCAN req state machine procedure
	==========================================================================
 */
void MlmeScanReqAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	u8          Ssid[MAX_LEN_OF_SSID], SsidLen, ScanType, BssType;
	bool        TimerCancelled;
	ULONG		   Now;
	USHORT         Status;

#ifdef RTMP_MAC_USB
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
			ASIC_RADIO_ON(pAd, MLME_RADIO_ON);
#endif /* RTMP_MAC_USB */
       /*
	    Check the total scan tries for one single OID command
	    If this is the CCX 2.0 Case, skip that!
	*/
	if ( !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeScanReqAction before Startup\n"));
		return;
	}

	/* first check the parameter sanity */
	if (MlmeScanReqSanity(pAd,
						  Elem->Msg,
						  Elem->MsgLen,
						  &BssType,
						  (char *)Ssid,
						  &SsidLen,
						  &ScanType))
	{
		/*
		     Check for channel load and noise hist request
		     Suspend MSDU only at scan request, not the last two mentioned
		     Suspend MSDU transmission here
		*/
		RTMPSuspendMsduTransmission(pAd);

		/*
		    To prevent data lost.
		    Send an NULL data with turned PSM bit on to current associated AP before SCAN progress.
		    And should send an NULL data with turned PSM bit off to AP, when scan progress done
		*/
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && (INFRA_ON(pAd)))
		{
			RTMPSendNullFrame(pAd,
							  pAd->CommonCfg.TxRate,
							  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? true:false),
							  PWR_SAVE);
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeScanReqAction -- Send PSM Data frame for off channel RM, SCAN_IN_PROGRESS=%d!\n",
											RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)));
			OS_WAIT(20);
		}

		NdisGetSystemUpTime(&Now);
		pAd->StaCfg.LastScanTime = Now;
		/* reset all the timers */
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);

		/* record desired BSS parameters */
		pAd->MlmeAux.BssType = BssType;
		pAd->MlmeAux.ScanType = ScanType;
		pAd->MlmeAux.SsidLen = SsidLen;
		memset(pAd->MlmeAux.Ssid, 0, MAX_LEN_OF_SSID);
		memmove(pAd->MlmeAux.Ssid, Ssid, SsidLen);

		/*
			Scanning was pending (for fast scanning)
		*/
		if ((pAd->StaCfg.bImprovedScan) && (pAd->Mlme.SyncMachine.CurrState == SCAN_PENDING))
		{
			pAd->MlmeAux.Channel = pAd->StaCfg.LastScanChannel;
		}
		else
		{
			if (pAd->StaCfg.bFastConnect && (pAd->CommonCfg.Channel != 0) && !pAd->StaCfg.bNotFirstScan)
			{
				pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;
			}
			else
			{
				{
					/* start from the first channel */
					pAd->MlmeAux.Channel = FirstChannel(pAd);
				}
			}
		}

		/* Let BBP register at 20MHz to do scan */
		rtmp_bbp_set_bw(pAd, BW_20);
		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BBP R4 to 20MHz.l\n"));

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
		/* Before scan, reset trigger event table. */
		TriEventInit(pAd);
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */


		ScanNextChannel(pAd, OPMODE_STA);
	}
	else
	{
		DBGPRINT_ERR(("SYNC - MlmeScanReqAction() sanity check fail\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status, 0);
	}
}

/*
	==========================================================================
	Description:
		MLME JOIN req state machine procedure
	==========================================================================
 */
void MlmeJoinReqAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	BSS_ENTRY    *pBss;
	bool       TimerCancelled;
	HEADER_802_11 Hdr80211;
	ULONG         FrameLen = 0;
	u8 *       pOutBuffer = NULL;
	u8 *       pSupRate = NULL;
	u8         SupRateLen;
	u8 *       pExtRate = NULL;
	u8         ExtRateLen;
	u8         ASupRate[] = {0x8C, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6C};
	u8         ASupRateLen = sizeof(ASupRate)/sizeof(u8);
	MLME_JOIN_REQ_STRUCT *pInfo = (MLME_JOIN_REQ_STRUCT *)(Elem->Msg);

	bool       bChangeInitBW = false;

	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeJoinReqAction(BSS #%ld)\n", pInfo->BssIdx));

	/* reset all the timers */
	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
	RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

	pBss = &pAd->MlmeAux.SsidBssTab.BssEntry[pInfo->BssIdx];

	/* record the desired SSID & BSSID we're waiting for */
	memcpy(pAd->MlmeAux.Bssid, pBss->Bssid, ETH_ALEN);

	/* If AP's SSID is not hidden, it is OK for updating ssid to MlmeAux again. */
	if (pBss->Hidden == 0)
	{
		memset(pAd->MlmeAux.Ssid, 0, MAX_LEN_OF_SSID);
		memmove(pAd->MlmeAux.Ssid, pBss->Ssid, pBss->SsidLen);
		pAd->MlmeAux.SsidLen = pBss->SsidLen;
	}

	pAd->MlmeAux.BssType = pBss->BssType;
	pAd->MlmeAux.Channel = pBss->Channel;
	pAd->MlmeAux.CentralChannel = pBss->CentralChannel;

#ifdef EXT_BUILD_CHANNEL_LIST
	/* Country IE of the AP will be evaluated and will be used. */
	if ((pAd->StaCfg.IEEE80211dClientMode != Rt802_11_D_None) &&
		(pBss->bHasCountryIE == true))
	{
		memmove(&pAd->CommonCfg.CountryCode[0], &pBss->CountryString[0], 2);
		if (pBss->CountryString[2] == 'I')
			pAd->CommonCfg.Geography = IDOR;
		else if (pBss->CountryString[2] == 'O')
			pAd->CommonCfg.Geography = ODOR;
		else
			pAd->CommonCfg.Geography = BOTH;
		BuildChannelListEx(pAd);
	}
#endif /* EXT_BUILD_CHANNEL_LIST */

	{
		bChangeInitBW = true;
	}


	if (bChangeInitBW == true)
	{
		/* Let BBP register at 20MHz to do scan */
		rtmp_bbp_set_bw(pAd, BW_20);
		DBGPRINT(RT_DEBUG_TRACE, ("%s(): Set BBP BW=20MHz\n", __FUNCTION__));

		/* switch channel and waiting for beacon timer */
		AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, false);
		AsicLockChannel(pAd, pAd->MlmeAux.Channel);
	}


	RTMPSetTimer(&pAd->MlmeAux.BeaconTimer, JOIN_TIMEOUT);

	do
	{
		if (((pAd->CommonCfg.bIEEE80211H == 1) &&
			(pAd->MlmeAux.Channel > 14) &&
			RadarChannelCheck(pAd, pAd->MlmeAux.Channel))
		)
		{
			/* We can't send any Probe request frame to meet 802.11h. */
			if (pBss->Hidden == 0)
				break;
		}

		/*
		    send probe request
		*/
		pOutBuffer = kmalloc(MGMT_DMA_BUFFER_SIZE, GFP_ATOMIC);
		if (pOutBuffer)
		{
			if (pAd->MlmeAux.Channel <= 14)
			{
				pSupRate = pAd->CommonCfg.SupRate;
				SupRateLen = pAd->CommonCfg.SupRateLen;
				pExtRate = pAd->CommonCfg.ExtRate;
				ExtRateLen = pAd->CommonCfg.ExtRateLen;
			}
			else
			{
				/* Overwrite Support Rate, CCK rate are not allowed */
				pSupRate = ASupRate;
				SupRateLen = ASupRateLen;
				ExtRateLen = 0;
			}

			if (pAd->MlmeAux.BssType == BSS_INFRA)
				MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, pAd->MlmeAux.Bssid,
									pAd->MlmeAux.Bssid);
			else
				MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR,
									BROADCAST_ADDR);

			MakeOutgoingFrame(pOutBuffer, &FrameLen,
							  sizeof(HEADER_802_11), &Hdr80211,
							  1,                        &SsidIe,
							  1,                        &pAd->MlmeAux.SsidLen,
							  pAd->MlmeAux.SsidLen,	    pAd->MlmeAux.Ssid,
							  1,                        &SupRateIe,
							  1,                        &SupRateLen,
							  SupRateLen,               pSupRate,
							  END_OF_ARGS);

			if (ExtRateLen)
			{
				ULONG Tmp;
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &Tmp,
								  1,                                &ExtRateIe,
								  1,                                &ExtRateLen,
								  ExtRateLen,                       pExtRate,
								  END_OF_ARGS);
				FrameLen += Tmp;
			}



#ifdef WPA_SUPPLICANT_SUPPORT
			if ((pAd->OpMode == OPMODE_STA) &&
				(pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) &&
				(pAd->StaCfg.WpsProbeReqIeLen != 0))
			{
				ULONG 		WpsTmpLen = 0;

				MakeOutgoingFrame(pOutBuffer + FrameLen,              &WpsTmpLen,
								pAd->StaCfg.WpsProbeReqIeLen,	pAd->StaCfg.pWpsProbeReqIe,
								END_OF_ARGS);

				FrameLen += WpsTmpLen;
			}
#endif /* WPA_SUPPLICANT_SUPPORT */

			MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
			kfree(pOutBuffer);
		}
	} while (false);

	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - Switch to ch %d, Wait BEACON from %02x:%02x:%02x:%02x:%02x:%02x\n",
		pBss->Channel, pBss->Bssid[0], pBss->Bssid[1], pBss->Bssid[2], pBss->Bssid[3], pBss->Bssid[4], pBss->Bssid[5]));

	pAd->Mlme.SyncMachine.CurrState = JOIN_WAIT_BEACON;
}

/*
	==========================================================================
	Description:
		MLME START Request state machine procedure, starting an IBSS
	==========================================================================
 */
void MlmeStartReqAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	u8 Ssid[MAX_LEN_OF_SSID], SsidLen;
	bool TimerCancelled;
	u8 *VarIE = NULL;		/* New for WPA security suites */
	NDIS_802_11_VARIABLE_IEs *pVIE = NULL;
	LARGE_INTEGER TimeStamp;
	bool Privacy;
	USHORT Status;


	/* allocate memory */
	VarIE = kmalloc(MAX_VIE_LEN, GFP_ATOMIC);
	if (VarIE == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		return;
	}

	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;
	TimeStamp.u.LowPart  = 0;
	TimeStamp.u.HighPart = 0;

	if ((MlmeStartReqSanity(pAd, Elem->Msg, Elem->MsgLen, (char *)Ssid, &SsidLen)) &&
		(CHAN_PropertyCheck(pAd, pAd->MlmeAux.Channel, CHANNEL_NO_IBSS) == false))
	{
		/* reset all the timers */
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &TimerCancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

		/* Start a new IBSS. All IBSS parameters are decided now */
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeStartReqAction - Start a new IBSS. All IBSS parameters are decided now.... \n"));
		pAd->MlmeAux.BssType = BSS_ADHOC;
		memmove(pAd->MlmeAux.Ssid, Ssid, SsidLen);
		pAd->MlmeAux.SsidLen = SsidLen;

		{
			/* generate a radom number as BSSID */
			MacAddrRandomBssid(pAd, pAd->MlmeAux.Bssid);
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeStartReqAction - generate a radom number as BSSID \n"));
		}

		Privacy = (pAd->StaCfg.WepStatus == Ndis802_11Encryption1Enabled) ||
				  (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) ||
				  (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled);
		pAd->MlmeAux.CapabilityInfo = CAP_GENERATE(0,1,Privacy, (pAd->CommonCfg.TxPreamble == Rt802_11PreambleShort), 1, 0);
		pAd->MlmeAux.BeaconPeriod = pAd->CommonCfg.BeaconPeriod;
		pAd->MlmeAux.AtimWin = pAd->StaCfg.AtimWin;
		pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;

		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
		pAd->MlmeAux.CentralChannel = pAd->CommonCfg.CentralChannel;

		pAd->MlmeAux.SupRateLen= pAd->CommonCfg.SupRateLen;
		memmove(pAd->MlmeAux.SupRate, pAd->CommonCfg.SupRate, MAX_LEN_OF_SUPPORTED_RATES);
		RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);
		pAd->MlmeAux.ExtRateLen = pAd->CommonCfg.ExtRateLen;
		memmove(pAd->MlmeAux.ExtRate, pAd->CommonCfg.ExtRate, MAX_LEN_OF_SUPPORTED_RATES);
		RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);
#ifdef DOT11_N_SUPPORT
		if (WMODE_CAP_N(pAd->CommonCfg.PhyMode) && (pAd->StaCfg.bAdhocN == true))
		{
			RTMPUpdateHTIE(&pAd->CommonCfg.DesiredHtPhy, &pAd->StaCfg.DesiredHtPhyInfo.MCSSet[0], &pAd->MlmeAux.HtCapability, &pAd->MlmeAux.AddHtInfo);
			pAd->MlmeAux.HtCapabilityLen = sizeof(HT_CAPABILITY_IE);
			/* Not turn pAd->StaActive.SupportedHtPhy.bHtEnable = true here. */
			DBGPRINT(RT_DEBUG_TRACE, ("SYNC -pAd->StaActive.SupportedHtPhy.bHtEnable = true\n"));
#ifdef DOT11_VHT_AC
			if (WMODE_CAP_AC(pAd->CommonCfg.PhyMode) &&
				(pAd->MlmeAux.Channel > 14))
			{
				build_vht_cap_ie(pAd, (u8 *)&pAd->MlmeAux.vht_cap);
				pAd->MlmeAux.vht_cap_len = sizeof(VHT_CAP_IE);
			}
#endif /* DOT11_VHT_AC */
		}
		else
#endif /* DOT11_N_SUPPORT */
		{
			pAd->MlmeAux.HtCapabilityLen = 0;
			pAd->StaActive.SupportedPhyInfo.bHtEnable = false;
			memset(&pAd->StaActive.SupportedPhyInfo.MCSSet[0], 0, 16);
		}
		/* temporarily not support QOS in IBSS */
		memset(&pAd->MlmeAux.APEdcaParm, 0, sizeof(EDCA_PARM));
		memset(&pAd->MlmeAux.APQbssLoad, 0, sizeof(QBSS_LOAD_PARM));
		memset(&pAd->MlmeAux.APQosCapability, 0, sizeof(QOS_CAPABILITY_PARM));

		AsicSwitchChannel(pAd, pAd->MlmeAux.Channel, false);
		AsicLockChannel(pAd, pAd->MlmeAux.Channel);

		DBGPRINT(RT_DEBUG_TRACE, ("SYNC - MlmeStartReqAction(ch= %d,sup rates= %d, ext rates=%d)\n",
			pAd->MlmeAux.Channel, pAd->MlmeAux.SupRateLen, pAd->MlmeAux.ExtRateLen));

		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_SUCCESS;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status, 0);
	}
	else
	{
		DBGPRINT_ERR(("SYNC - MlmeStartReqAction() sanity check fail.\n"));
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_INVALID_FORMAT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status, 0);
	}

	if (VarIE != NULL)
		kfree(VarIE);
}


//+++Add by shiang to check correctness of new sanity function
void rtmp_dbg_sanity_diff(struct rtmp_adapter*pAd, MLME_QUEUE_ELEM *Elem)
{
	/* Parameters used for old sanity function */
	u8 Bssid[ETH_ALEN], Addr2[ETH_ALEN];
	u8 *Ssid = NULL;
	u8 SsidLen=0, DtimCount, DtimPeriod, BcastFlag, MessageToMe, NewChannel, Channel = 0, BssType;
	CF_PARM CfParm = {0};
	USHORT BeaconPeriod, AtimWin, CapabilityInfo;
	LARGE_INTEGER TimeStamp;
	u8 SupRate[MAX_LEN_OF_SUPPORTED_RATES], ExtRate[MAX_LEN_OF_SUPPORTED_RATES];
	u8 CkipFlag;
	EDCA_PARM EdcaParm = {0};
	u8 AironetCellPowerLimit;
	u8 SupRateLen, ExtRateLen;
	QBSS_LOAD_PARM QbssLoad;
	QOS_CAPABILITY_PARM QosCapability = {0};
	ULONG RalinkIe;
	u8 		AddHtInfoLen;
	EXT_CAP_INFO_ELEMENT	ExtCapInfo;
	HT_CAPABILITY_IE		*pHtCapability = NULL;
	ADD_HT_INFO_IE		*pAddHtInfo = NULL;	/* AP might use this additional ht info IE */
	u8 		HtCapabilityLen = 0, PreNHtCapabilityLen = 0;
	u8 Erp;
	u8 		NewExtChannelOffset = 0xff;
	USHORT LenVIE;
	u8 *VarIE = NULL;
	NDIS_802_11_VARIABLE_IEs *pVIE = NULL;


	BCN_IE_LIST *ie_list = NULL;
	bool sanity_new, sanity_old;

	/* allocate memory */
	Ssid = kmalloc(MAX_LEN_OF_SSID, GFP_ATOMIC);
	if (Ssid == NULL)
		goto LabelErr;
	pHtCapability = kmalloc(sizeof(HT_CAPABILITY_IE), GFP_ATOMIC);
	if (pHtCapability == NULL)
		goto LabelErr;
	pAddHtInfo = kmalloc(sizeof(ADD_HT_INFO_IE), GFP_ATOMIC);
	if (pAddHtInfo == NULL)
		goto LabelErr;


	memset(&QbssLoad, 0, sizeof(QBSS_LOAD_PARM)); /* woody */
#ifdef DOT11_N_SUPPORT
    memset(pHtCapability, 0, sizeof(HT_CAPABILITY_IE));
	memset(pAddHtInfo, 0, sizeof(ADD_HT_INFO_IE));
#endif /* DOT11_N_SUPPORT */

	memset(Ssid, 0, MAX_LEN_OF_SSID);

	ie_list = kmalloc(sizeof(BCN_IE_LIST), GFP_ATOMIC);
	if (ie_list == NULL)
		goto LabelErr;
	memset(ie_list, 0, sizeof(BCN_IE_LIST));


	sanity_new = PeerBeaconAndProbeRspSanity(pAd,
						&Elem->Msg[0], Elem->MsgLen,
						Elem->Channel,
						ie_list, &LenVIE, pVIE);

	sanity_old = PeerBeaconAndProbeRspSanity_Old(pAd,
								Elem->Msg,
								Elem->MsgLen,
								Elem->Channel,
								Addr2,
								Bssid,
								(char *)Ssid,
								&SsidLen,
								&BssType,
								&BeaconPeriod,
								&Channel,
								&NewChannel,
								&TimeStamp,
								&CfParm,
								&AtimWin,
								&CapabilityInfo,
								&Erp,
								&DtimCount,
								&DtimPeriod,
								&BcastFlag,
								&MessageToMe,
								SupRate,
								&SupRateLen,
								ExtRate,
								&ExtRateLen,
								&CkipFlag,
								&AironetCellPowerLimit,
								&EdcaParm,
								&QbssLoad,
								&QosCapability,
								&RalinkIe,
								&HtCapabilityLen,
#ifdef CONFIG_STA_SUPPORT
								&PreNHtCapabilityLen,
#endif /* CONFIG_STA_SUPPORT */
								pHtCapability,
								&ExtCapInfo,
								&AddHtInfoLen,
								pAddHtInfo,
								&NewExtChannelOffset,
								&LenVIE,
								pVIE);

		if (sanity_old != sanity_new)
		{
			DBGPRINT(RT_DEBUG_ERROR, ("sanity mismatch, old=%d, new=%d\n", sanity_old, sanity_new));
		}
		else
		{
			if (memcmp(&ie_list->Addr2[0], &Addr2[0], ETH_ALEN) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("Add2 mismatch!Old=%02x:%02x:%02x:%02x:%02x:%02x!New=%02x:%02x:%02x:%02x:%02x:%02x!\n",
									PRINT_MAC(Addr2), PRINT_MAC(ie_list->Addr2)));
			}

			if (memcmp(&ie_list->Bssid[0], &Bssid[0], ETH_ALEN) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("Bssid mismatch!Old=%02x:%02x:%02x:%02x:%02x:%02x!New=%02x:%02x:%02x:%02x:%02x:%02x!\n",
									PRINT_MAC(Bssid), PRINT_MAC(ie_list->Bssid)));
			}

			if (SsidLen != ie_list->SsidLen)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("SsidLen mismatch!Old=%d, New=%d\n", SsidLen, ie_list->SsidLen));
			}

			if (memcmp(&ie_list->Ssid[0], &Ssid[0], SsidLen) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("Ssid mismatch!Old=%s, New=%s\n", Ssid, ie_list->Ssid));
			}

			if (BssType != ie_list->BssType)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("BssType mismatch!Old=%d, New=%d\n", BssType, ie_list->BssType));
			}

			if (BeaconPeriod != ie_list->BeaconPeriod)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("BeaconPeriod mismatch!Old=%d, New=%d\n", BeaconPeriod, ie_list->BeaconPeriod));
			}

			if (Channel != ie_list->Channel)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("Channel mismatch!Old=%d, New=%d\n", Channel, ie_list->Channel));
			}

			if (NewChannel != ie_list->NewChannel)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("NewChannel mismatch!Old=%d, New=%d\n", NewChannel, ie_list->NewChannel));
			}

			if (memcmp(&ie_list->TimeStamp, &TimeStamp, sizeof(LARGE_INTEGER)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("TimeStamp mismatch!Old=%d - %d, New=%d - %d\n",
							TimeStamp.u.LowPart, TimeStamp.u.HighPart,
							ie_list->TimeStamp.u.LowPart, ie_list->TimeStamp.u.HighPart));
			}

			if (memcmp(&ie_list->CfParm, &CfParm, sizeof(CF_PARM)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("CFParam mismatch!\n"));
			}

			if (AtimWin != ie_list->AtimWin)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("AtimWin mismatch!Old=%d, New=%d\n", AtimWin, ie_list->AtimWin));
			}


			if (CapabilityInfo != ie_list->CapabilityInfo)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("CapabilityInfo mismatch!Old=%d, New=%d\n", CapabilityInfo, ie_list->CapabilityInfo));
			}

			if (Erp != ie_list->Erp)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("Erp mismatch!Old=%d, New=%d\n", Erp, ie_list->Erp));
			}

			if (DtimCount != ie_list->DtimCount)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("DtimCount mismatch!Old=%d, New=%d\n", DtimCount, ie_list->DtimCount));
			}

			if (DtimPeriod != ie_list->DtimPeriod)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("DtimPeriod mismatch!Old=%d, New=%d\n", DtimPeriod, ie_list->DtimPeriod));
			}

			if (BcastFlag != ie_list->BcastFlag)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("BcastFlag mismatch!Old=%d, New=%d\n", BcastFlag, ie_list->BcastFlag));
			}

			if (MessageToMe != ie_list->MessageToMe)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("MessageToMe mismatch!Old=%d, New=%d\n", MessageToMe, ie_list->MessageToMe));
			}

			if (SupRateLen != ie_list->SupRateLen)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("SupRateLen mismatch!Old=%d, New=%d\n", SupRateLen, ie_list->SupRateLen));
			}

			if (memcmp(&ie_list->SupRate[0], &SupRate, ie_list->SupRateLen) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("SupRate mismatch!\n"));
			}


			if (ExtRateLen != ie_list->ExtRateLen)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("ExtRateLen mismatch!Old=%d, New=%d\n", ExtRateLen, ie_list->ExtRateLen));
			}
			if (memcmp(&ie_list->ExtRate[0], &ExtRate, ie_list->ExtRateLen) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("ExtRate mismatch!\n"));
			}


			if (CkipFlag != ie_list->CkipFlag)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("CkipFlag mismatch!Old=%d, New=%d\n", CkipFlag, ie_list->CkipFlag));
			}

			if (AironetCellPowerLimit != ie_list->AironetCellPowerLimit)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("AironetCellPowerLimit mismatch!Old=%d, New=%d\n", AironetCellPowerLimit, ie_list->AironetCellPowerLimit));
			}
			if (memcmp(&ie_list->EdcaParm, &EdcaParm, sizeof(EDCA_PARM)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("EdcaParm mismatch!\n"));
			}

			if (memcmp(&ie_list->QbssLoad, &QbssLoad, sizeof(QBSS_LOAD_PARM)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("QbssLoad mismatch!\n"));
			}

			if (memcmp(&ie_list->QosCapability, &QosCapability, sizeof(QOS_CAPABILITY_PARM)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("QosCapability mismatch!\n"));
			}

			if (RalinkIe != ie_list->RalinkIe)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("RalinkIe mismatch!Old=%lx, New=%lx\n", RalinkIe, ie_list->RalinkIe));
			}

			if (HtCapabilityLen != ie_list->HtCapabilityLen)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("HtCapabilityLen mismatch!Old=%d, New=%d\n", HtCapabilityLen, ie_list->HtCapabilityLen));
			}

#ifdef CONFIG_STA_SUPPORT
			if (PreNHtCapabilityLen != ie_list->PreNHtCapabilityLen)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("PreNHtCapabilityLen mismatch!Old=%d, New=%d\n", PreNHtCapabilityLen, ie_list->PreNHtCapabilityLen));
			}
#endif /* CONFIG_STA_SUPPORT */
			if (memcmp(&ie_list->HtCapability, pHtCapability, sizeof(HT_CAPABILITY_IE)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("pHtCapability mismatch!\n"));
			}

			if (memcmp(&ie_list->ExtCapInfo, &ExtCapInfo, sizeof(EXT_CAP_INFO_ELEMENT)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("ExtCapInfo mismatch!\n"));
			}

			if (AddHtInfoLen != ie_list->AddHtInfoLen)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("AddHtInfoLen mismatch!Old=%d, New=%d\n", AddHtInfoLen, ie_list->AddHtInfoLen));
			}

			if (memcmp(&ie_list->AddHtInfo, pAddHtInfo, sizeof(ADD_HT_INFO_IE)) != 0)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("AddHtInfo mismatch!\n"));
			}

			if (NewExtChannelOffset != ie_list->NewExtChannelOffset)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("AddHtInfoLen mismatch!Old=%d, New=%d\n", NewExtChannelOffset, ie_list->NewExtChannelOffset));
			}
		}
		goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (Ssid != NULL)
		kfree(Ssid);
	if (VarIE != NULL)
		kfree(VarIE);
	if (pHtCapability != NULL)
		kfree(pHtCapability);
	if (pAddHtInfo != NULL)
		kfree(pAddHtInfo);

	if (ie_list != NULL)
		kfree(ie_list);

}
//---Add by shiang to check correctness of new sanity function


/*
	==========================================================================
	Description:
		peer sends beacon back when scanning
	==========================================================================
 */
void PeerBeaconAtScanAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	PFRAME_802_11 pFrame;
	USHORT LenVIE;
	u8 *VarIE = NULL;
	NDIS_802_11_VARIABLE_IEs *pVIE = NULL;



	BCN_IE_LIST *ie_list = NULL;



	ie_list = kmalloc(sizeof(BCN_IE_LIST), GFP_ATOMIC);
	if (!ie_list) {
		DBGPRINT(RT_DEBUG_ERROR, ("%s():Alloc ie_list failed!\n", __FUNCTION__));
		return;
	}
	memset((u8 *)ie_list, 0, sizeof(BCN_IE_LIST));

	/* Init Variable IE structure */
	VarIE = kmalloc(MAX_VIE_LEN, GFP_ATOMIC);
	if (VarIE == NULL)
		goto LabelErr;
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;


	pFrame = (PFRAME_802_11) Elem->Msg;


	if (PeerBeaconAndProbeRspSanity(pAd,
						&Elem->Msg[0], Elem->MsgLen,
						Elem->Channel,
						ie_list, &LenVIE, pVIE))
	{
		ULONG Idx = 0;
		CHAR Rssi = 0;

		Idx = BssTableSearch(&pAd->ScanTab, &ie_list->Bssid[0], ie_list->Channel);
		if (Idx != BSS_NOT_FOUND)
			Rssi = pAd->ScanTab.BssEntry[Idx].Rssi;

		Rssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0),
							ConvertToRssi(pAd, Elem->Rssi1, RSSI_1),
							ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));


#ifdef DOT11_N_SUPPORT
		if ((ie_list->HtCapabilityLen > 0) || (ie_list->PreNHtCapabilityLen > 0))
			ie_list->HtCapabilityLen = SIZE_HT_CAP_IE;
#endif /* DOT11_N_SUPPORT */

		Idx = BssTableSetEntry(pAd, &pAd->ScanTab, ie_list, Rssi, LenVIE, pVIE);
#ifdef DOT11_N_SUPPORT
		/* TODO: Check for things need to do when enable "DOT11V_WNM_SUPPORT" */
#ifdef DOT11N_DRAFT3
		/* Check if this scan channel is the effeced channel */
		if (INFRA_ON(pAd)
			&& (pAd->CommonCfg.bBssCoexEnable == true)
			&& ((ie_list->Channel > 0) && (ie_list->Channel <= 14)))
		{
			int chListIdx;

			/* find the channel list idx by the channel number */
			for (chListIdx = 0; chListIdx < pAd->ChannelListNum; chListIdx++)
			{
				if (ie_list->Channel == pAd->ChannelList[chListIdx].Channel)
					break;
			}

			if (chListIdx < pAd->ChannelListNum)
			{
				/*
					If this channel is effected channel for the 20/40 coex operation. Check the related IEs.
				*/
				if (pAd->ChannelList[chListIdx].bEffectedChannel == true)
				{
					u8 RegClass;
					OVERLAP_BSS_SCAN_IE	BssScan;

					/* Read Beacon's Reg Class IE if any. */
					PeerBeaconAndProbeRspSanity2(pAd, Elem->Msg, Elem->MsgLen, &BssScan, &RegClass);
					TriEventTableSetEntry(pAd, &pAd->CommonCfg.TriggerEventTab, &ie_list->Bssid[0],
										&ie_list->HtCapability, ie_list->HtCapabilityLen,
										RegClass, ie_list->Channel);
				}
			}
		}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */
		if (Idx != BSS_NOT_FOUND)
		{
			PBSS_ENTRY	pBssEntry = &pAd->ScanTab.BssEntry[Idx];
			memmove(pBssEntry->PTSF, &Elem->Msg[24], 4);
			memmove(&pBssEntry->TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
			memmove(&pBssEntry->TTSF[4], &Elem->TimeStamp.u.LowPart, 4);

			pBssEntry->MinSNR = Elem->Signal % 10;
			if (pBssEntry->MinSNR == 0)
				pBssEntry->MinSNR = -5;

			memmove(pBssEntry->MacAddr, &ie_list->Addr2[0], ETH_ALEN);

			if ((pFrame->Hdr.FC.SubType == SUBTYPE_PROBE_RSP) && (LenVIE != 0))
			{
				pBssEntry->VarIeFromProbeRspLen = 0;
				if (pBssEntry->pVarIeFromProbRsp)
				{
					pBssEntry->VarIeFromProbeRspLen = LenVIE;
					memset(pBssEntry->pVarIeFromProbRsp, 0, MAX_VIE_LEN);
					memmove(pBssEntry->pVarIeFromProbRsp, pVIE, LenVIE);
				}
			}
		}

#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
		RT_CFG80211_SCANNING_INFORM(pAd, Idx, Elem->Channel, (u8 *)pFrame,
									Elem->MsgLen, Rssi);
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */
	}
	/* sanity check fail, ignored */
	goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (VarIE != NULL)
		kfree(VarIE);
	if (ie_list)
		kfree(ie_list);
	return;
}


/*
	==========================================================================
	Description:
		When waiting joining the (I)BSS, beacon received from external
	==========================================================================
 */
void PeerBeaconAtJoinAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	bool TimerCancelled;
	USHORT LenVIE;
	USHORT Status;
	u8 *VarIE = NULL;
	NDIS_802_11_VARIABLE_IEs *pVIE = NULL;
	ULONG Idx = 0;
	CHAR Rssi = 0;

#ifdef DOT11_N_SUPPORT
	u8 CentralChannel;
	bool bAllowNrate = false;
#endif /* DOT11_N_SUPPORT */
	BCN_IE_LIST *ie_list = NULL;


	/* allocate memory */
	ie_list = kmalloc(sizeof(BCN_IE_LIST), GFP_ATOMIC);
	if (ie_list == NULL)
		goto LabelErr;
	memset(ie_list, 0, sizeof(BCN_IE_LIST));

	VarIE = kmalloc(MAX_VIE_LEN, GFP_ATOMIC);
	if (VarIE == NULL)
		goto LabelErr;
	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;


	if (PeerBeaconAndProbeRspSanity(pAd,
								Elem->Msg,
								Elem->MsgLen,
								Elem->Channel,
								ie_list,
								&LenVIE,
								pVIE))
	{
		/* Disqualify 11b only adhoc when we are in 11g only adhoc mode */
		if ((ie_list->BssType == BSS_ADHOC) &&
			WMODE_EQUAL(pAd->CommonCfg.PhyMode, WMODE_G) &&
			((ie_list->SupRateLen+ie_list->ExtRateLen)< 12))
			goto LabelOK;


		/*
		    BEACON from desired BSS/IBSS found. We should be able to decide most
		    BSS parameters here.
		    Q. But what happen if this JOIN doesn't conclude a successful ASSOCIATEION?
		        Do we need to receover back all parameters belonging to previous BSS?
		    A. Should be not. There's no back-door recover to previous AP. It still need
		        a new JOIN-AUTH-ASSOC sequence.
		*/
		if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, &ie_list->Bssid[0]))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("%s():receive desired BEACON,Channel=%d\n",
								__FUNCTION__, ie_list->Channel));
			RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &TimerCancelled);

			/* Update RSSI to prevent No signal display when cards first initialized */
			pAd->StaCfg.RssiSample.LastRssi0 = ConvertToRssi(pAd, Elem->Rssi0, RSSI_0);
			pAd->StaCfg.RssiSample.LastRssi1 = ConvertToRssi(pAd, Elem->Rssi1, RSSI_1);
			pAd->StaCfg.RssiSample.LastRssi2 = ConvertToRssi(pAd, Elem->Rssi2, RSSI_2);
			pAd->StaCfg.RssiSample.AvgRssi0 = pAd->StaCfg.RssiSample.LastRssi0;
			pAd->StaCfg.RssiSample.AvgRssi0X8	= pAd->StaCfg.RssiSample.AvgRssi0 << 3;
			pAd->StaCfg.RssiSample.AvgRssi1 = pAd->StaCfg.RssiSample.LastRssi1;
			pAd->StaCfg.RssiSample.AvgRssi1X8	= pAd->StaCfg.RssiSample.AvgRssi1 << 3;
			pAd->StaCfg.RssiSample.AvgRssi2 = pAd->StaCfg.RssiSample.LastRssi2;
			pAd->StaCfg.RssiSample.AvgRssi2X8	= pAd->StaCfg.RssiSample.AvgRssi2 << 3;

			/*
			  We need to check if SSID only set to any, then we can record the current SSID.
			  Otherwise will cause hidden SSID association failed.
			*/
			if (pAd->MlmeAux.SsidLen == 0)
			{
				memmove(pAd->MlmeAux.Ssid, ie_list->Ssid, ie_list->SsidLen);
				pAd->MlmeAux.SsidLen = ie_list->SsidLen;
			}
			else
			{
				Idx = BssSsidTableSearch(&pAd->ScanTab, ie_list->Bssid,
										pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen,
										ie_list->Channel);

				if (Idx == BSS_NOT_FOUND)
				{
					Rssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0),
											ConvertToRssi(pAd, Elem->Rssi1, RSSI_1),
											ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));
					Idx = BssTableSetEntry(pAd, &pAd->ScanTab, ie_list, Rssi, LenVIE, pVIE);
					if (Idx != BSS_NOT_FOUND)
					{
						memmove(pAd->ScanTab.BssEntry[Idx].PTSF, &Elem->Msg[24], 4);
						memmove(&pAd->ScanTab.BssEntry[Idx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
						memmove(&pAd->ScanTab.BssEntry[Idx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
						ie_list->CapabilityInfo = pAd->ScanTab.BssEntry[Idx].CapabilityInfo;

						pAd->ScanTab.BssEntry[Idx].MinSNR = Elem->Signal % 10;
						if (pAd->ScanTab.BssEntry[Idx].MinSNR == 0)
							pAd->ScanTab.BssEntry[Idx].MinSNR = -5;

						memmove(pAd->ScanTab.BssEntry[Idx].MacAddr, ie_list->Addr2, ETH_ALEN);
					}
				}
				else
				{
#ifdef WPA_SUPPLICANT_SUPPORT
					if (pAd->StaCfg.WpaSupplicantUP & WPA_SUPPLICANT_ENABLE_WPS)
						;
					else
#endif /* WPA_SUPPLICANT_SUPPORT */
					{

						/*
						    Check if AP privacy is different Staion, if yes,
						    start a new scan and ignore the frame
						    (often happen during AP change privacy at short time)
						*/
						if ((((pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled) << 4) ^ ie_list->CapabilityInfo) & 0x0010)
						{
							MLME_SCAN_REQ_STRUCT ScanReq;
							DBGPRINT(RT_DEBUG_TRACE, ("%s:AP privacy %d is differenct from STA privacy%d\n",
										__FUNCTION__, (ie_list->CapabilityInfo & 0x0010) >> 4 ,
										pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled));
							ScanParmFill(pAd, &ScanReq, (char *) pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen, BSS_ANY, SCAN_ACTIVE);
							MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq, 0);
							pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
							NdisGetSystemUpTime(&pAd->StaCfg.LastScanTime);
							goto LabelOK;
						}
					}

					/* Multiple SSID case, used correct CapabilityInfo */
					ie_list->CapabilityInfo = pAd->ScanTab.BssEntry[Idx].CapabilityInfo;
				}
			}
			pAd->MlmeAux.CapabilityInfo = ie_list->CapabilityInfo & SUPPORTED_CAPABILITY_INFO;
			pAd->MlmeAux.BssType = ie_list->BssType;
			pAd->MlmeAux.BeaconPeriod = ie_list->BeaconPeriod;

			/*
				Some AP may carrys wrong beacon interval (ex. 0) in Beacon IE.
				We need to check here for preventing divided by 0 error.
			*/
			if (pAd->MlmeAux.BeaconPeriod == 0)
				pAd->MlmeAux.BeaconPeriod = 100;

			pAd->MlmeAux.Channel = ie_list->Channel;
			pAd->MlmeAux.AtimWin = ie_list->AtimWin;
			pAd->MlmeAux.CfpPeriod = ie_list->CfParm.CfpPeriod;
			pAd->MlmeAux.CfpMaxDuration = ie_list->CfParm.CfpMaxDuration;
			pAd->MlmeAux.APRalinkIe = ie_list->RalinkIe;

			/*
			    Copy AP's supported rate to MlmeAux for creating assoication request
			    Also filter out not supported rate
			*/
			pAd->MlmeAux.SupRateLen = ie_list->SupRateLen;
			memmove(pAd->MlmeAux.SupRate, ie_list->SupRate, ie_list->SupRateLen);
			RTMPCheckRates(pAd, pAd->MlmeAux.SupRate, &pAd->MlmeAux.SupRateLen);
			pAd->MlmeAux.ExtRateLen = ie_list->ExtRateLen;
			memmove(pAd->MlmeAux.ExtRate, ie_list->ExtRate, ie_list->ExtRateLen);
			RTMPCheckRates(pAd, pAd->MlmeAux.ExtRate, &pAd->MlmeAux.ExtRateLen);

			memset(pAd->StaActive.SupportedPhyInfo.MCSSet, 0, 16);


			/*  Get the ext capability info element */
			memmove(&pAd->MlmeAux.ExtCapInfo, &ie_list->ExtCapInfo,sizeof(ie_list->ExtCapInfo));

			pAd->StaActive.SupportedPhyInfo.bVhtEnable = false;
			pAd->StaActive.SupportedPhyInfo.vht_bw = VHT_BW_2040;

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
			DBGPRINT(RT_DEBUG_TRACE, ("MlmeAux.ExtCapInfo=%d\n", pAd->MlmeAux.ExtCapInfo.BssCoexistMgmtSupport));
			if (pAd->CommonCfg.bBssCoexEnable == true)
				pAd->CommonCfg.ExtCapIE.BssCoexistMgmtSupport = 1;
#endif /* DOT11N_DRAFT3 */

			if (((pAd->StaCfg.WepStatus != Ndis802_11WEPEnabled) && (pAd->StaCfg.WepStatus != Ndis802_11Encryption2Enabled))
				|| (pAd->CommonCfg.HT_DisallowTKIP == false))
			{
				if ((pAd->StaCfg.BssType == BSS_INFRA) ||
					((pAd->StaCfg.BssType == BSS_ADHOC) && (pAd->StaCfg.bAdhocN == true)))
				bAllowNrate = true;
			}

			pAd->MlmeAux.NewExtChannelOffset = ie_list->NewExtChannelOffset;
			pAd->MlmeAux.HtCapabilityLen = ie_list->HtCapabilityLen;

			CentralChannel = ie_list->Channel;

			memset(&pAd->MlmeAux.HtCapability, 0, SIZE_HT_CAP_IE);
			/* filter out un-supported ht rates */
			if (((ie_list->HtCapabilityLen > 0) || (ie_list->PreNHtCapabilityLen > 0)) &&
				(pAd->StaCfg.DesiredHtPhyInfo.bHtEnable) &&
				(WMODE_CAP_N(pAd->CommonCfg.PhyMode) && bAllowNrate))
			{
				memmove(&pAd->MlmeAux.AddHtInfo, &ie_list->AddHtInfo, SIZE_ADD_HT_INFO_IE);

                		/* StaActive.SupportedHtPhy.MCSSet stores Peer AP's 11n Rx capability */
				memmove(pAd->StaActive.SupportedPhyInfo.MCSSet, ie_list->HtCapability.MCSSet, 16);
				pAd->MlmeAux.NewExtChannelOffset = ie_list->NewExtChannelOffset;
				pAd->MlmeAux.HtCapabilityLen = SIZE_HT_CAP_IE;
				pAd->StaActive.SupportedPhyInfo.bHtEnable = true;
				if (ie_list->PreNHtCapabilityLen > 0)
					pAd->StaActive.SupportedPhyInfo.bPreNHt = true;
				RTMPCheckHt(pAd, BSSID_WCID, &ie_list->HtCapability, &ie_list->AddHtInfo);
				/* Copy AP Parameter to StaActive.  This is also in LinkUp. */
				DBGPRINT(RT_DEBUG_TRACE, ("%s():(MpduDensity=%d, MaxRAmpduFactor=%d, BW=%d)\n",
							__FUNCTION__, pAd->StaActive.SupportedHtPhy.MpduDensity,
							pAd->StaActive.SupportedHtPhy.MaxRAmpduFactor,
							ie_list->HtCapability.HtCapInfo.ChannelWidth));

				if (ie_list->AddHtInfoLen > 0)
				{
		 			/* Check again the Bandwidth capability of this AP. */
					CentralChannel = get_cent_ch_by_htinfo(pAd,
													&ie_list->AddHtInfo,
													&ie_list->HtCapability);

		 			DBGPRINT(RT_DEBUG_OFF, ("%s(): HT-CtrlChannel=%d, CentralChannel=>%d\n",
		 						__FUNCTION__, ie_list->AddHtInfo.ControlChan, CentralChannel));
				}

#ifdef DOT11_VHT_AC
				if (WMODE_CAP_AC(pAd->CommonCfg.PhyMode) &&
					(pAd->MlmeAux.Channel > 14) &&
					(ie_list->vht_cap_len))
				{
					VHT_OP_INFO *vht_op = &ie_list->vht_op_ie.vht_op_info;

					memmove(&pAd->MlmeAux.vht_cap, &ie_list->vht_cap_ie, ie_list->vht_cap_len);
					pAd->MlmeAux.vht_cap_len = ie_list->vht_cap_len;
					pAd->StaActive.SupportedPhyInfo.bVhtEnable = true;
					if (vht_op->ch_width == 0) {
						pAd->StaActive.SupportedPhyInfo.vht_bw = VHT_BW_2040;
					} else if (vht_op->ch_width == 1) {
						CentralChannel = vht_op->center_freq_1;
						pAd->StaActive.SupportedPhyInfo.vht_bw = VHT_BW_80;
					}

					DBGPRINT(RT_DEBUG_OFF, ("%s(): VHT->center_freq_1=%d, CentralChannel=>%d, vht_cent_ch=%d\n",
		 						__FUNCTION__, vht_op->center_freq_1, CentralChannel, pAd->CommonCfg.vht_cent_ch));
				}
#endif /* DOT11_VHT_AC */
			}
			else
#endif /* DOT11_N_SUPPORT */
			{
   				/* To prevent error, let legacy AP must have same CentralChannel and Channel. */
				if ((ie_list->HtCapabilityLen == 0) && (ie_list->PreNHtCapabilityLen == 0))
					pAd->MlmeAux.CentralChannel = pAd->MlmeAux.Channel;

				pAd->StaActive.SupportedPhyInfo.bHtEnable = false;
#ifdef DOT11_VHT_AC
				pAd->StaActive.SupportedPhyInfo.bVhtEnable = false;
				pAd->StaActive.SupportedPhyInfo.vht_bw = VHT_BW_2040;
#endif /* DOT11_VHT_AC */
				pAd->MlmeAux.NewExtChannelOffset = 0xff;
				memset(&pAd->MlmeAux.HtCapability, 0, SIZE_HT_CAP_IE);
				pAd->MlmeAux.HtCapabilityLen = 0;
				memset(&pAd->MlmeAux.AddHtInfo, 0, SIZE_ADD_HT_INFO_IE);
			}

			pAd->hw_cfg.cent_ch = CentralChannel;
			pAd->MlmeAux.CentralChannel = CentralChannel;
			DBGPRINT(RT_DEBUG_OFF, ("%s(): Set CentralChannel=%d\n", __FUNCTION__, pAd->MlmeAux.CentralChannel));

			RTMPUpdateMlmeRate(pAd);

			/* copy QOS related information */
			if ((pAd->CommonCfg.bWmmCapable)
#ifdef DOT11_N_SUPPORT
				 || WMODE_CAP_N(pAd->CommonCfg.PhyMode)
#endif /* DOT11_N_SUPPORT */
				)
			{
				memmove(&pAd->MlmeAux.APEdcaParm, &ie_list->EdcaParm, sizeof(EDCA_PARM));
				memmove(&pAd->MlmeAux.APQbssLoad, &ie_list->QbssLoad, sizeof(QBSS_LOAD_PARM));
				memmove(&pAd->MlmeAux.APQosCapability, &ie_list->QosCapability, sizeof(QOS_CAPABILITY_PARM));
			}
			else
			{
				memset(&pAd->MlmeAux.APEdcaParm, 0, sizeof(EDCA_PARM));
				memset(&pAd->MlmeAux.APQbssLoad, 0, sizeof(QBSS_LOAD_PARM));
				memset(&pAd->MlmeAux.APQosCapability, 0, sizeof(QOS_CAPABILITY_PARM));
			}

			DBGPRINT(RT_DEBUG_TRACE, ("%s(): - after JOIN, SupRateLen=%d, ExtRateLen=%d\n",
								__FUNCTION__, pAd->MlmeAux.SupRateLen,
								pAd->MlmeAux.ExtRateLen));

			if (ie_list->AironetCellPowerLimit != 0xFF)
			{
				/* We need to change our TxPower for CCX 2.0 AP Control of Client Transmit Power */
				ChangeToCellPowerLimit(pAd, ie_list->AironetCellPowerLimit);
			}
			else  /* Used the default TX Power Percentage. */
				pAd->CommonCfg.TxPowerPercentage = pAd->CommonCfg.TxPowerDefault;

			if (pAd->StaCfg.BssType == BSS_INFRA)
			{
				bool InfraAP_BW;
				u8 BwFallBack = 0;

				if (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40)
					InfraAP_BW = true;
				else
					InfraAP_BW = false;

				AdjustChannelRelatedValue(pAd,
											&BwFallBack,
											BSS0,
											InfraAP_BW,
											pAd->MlmeAux.Channel,
											pAd->MlmeAux.CentralChannel);
			}

			pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
			Status = MLME_SUCCESS;
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status, 0);

#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
			RT_CFG80211_SCANNING_INFORM(pAd, Idx, Elem->Channel, Elem->Msg,
										Elem->MsgLen, Rssi);
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */
		}
		/* not to me BEACON, ignored */
	}
	/* sanity check fail, ignore this frame */

	goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (ie_list != NULL)
		kfree(ie_list);
	if (VarIE != NULL)
		kfree(VarIE);

	return;
}

/*
	==========================================================================
	Description:
		receive BEACON from peer

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void PeerBeacon(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	u8 index=0;
	USHORT TbttNumToNextWakeUp;
	USHORT LenVIE;
	u8 *VarIE = NULL;		/* Total VIE length = MAX_VIE_LEN - -5 */
	NDIS_802_11_VARIABLE_IEs *pVIE = NULL;


	BCN_IE_LIST *ie_list = NULL;

	if (!(INFRA_ON(pAd) || ADHOC_ON(pAd)
		))
		return;

	/* allocate memory */
	ie_list = kmalloc(sizeof(BCN_IE_LIST), GFP_ATOMIC);
	if (ie_list == NULL)
		goto LabelErr;
	memset(ie_list, 0, sizeof(BCN_IE_LIST));

	VarIE = kmalloc(MAX_VIE_LEN, GFP_ATOMIC);
	if (VarIE == NULL)
		goto LabelErr;
	/* Init Variable IE structure */
	pVIE = (PNDIS_802_11_VARIABLE_IEs) VarIE;
	pVIE->Length = 0;


	if (PeerBeaconAndProbeRspSanity(pAd,
								Elem->Msg,
								Elem->MsgLen,
								Elem->Channel,
								ie_list,
								&LenVIE,
								pVIE))
	{
		bool is_my_bssid, is_my_ssid;
		ULONG Bssidx, Now;
		BSS_ENTRY *pBss;
		CHAR RealRssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0),
									ConvertToRssi(pAd, Elem->Rssi1, RSSI_1),
									ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));

		is_my_bssid = MAC_ADDR_EQUAL(ie_list->Bssid, pAd->CommonCfg.Bssid)? true : false;
		is_my_ssid = SSID_EQUAL(ie_list->Ssid, ie_list->SsidLen, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen)? true:false;


		/* ignore BEACON not for my SSID */
		if ((!is_my_ssid) && (!is_my_bssid))
			goto LabelOK;

		/* It means STA waits disassoc completely from this AP, ignores this beacon. */
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_WAIT_DISASSOC)
			goto LabelOK;

#ifdef DOT11_N_SUPPORT
		/* Copy Control channel for this BSSID. */
		if (ie_list->AddHtInfoLen != 0)
			ie_list->Channel = ie_list->AddHtInfo.ControlChan;

		if ((ie_list->HtCapabilityLen > 0) || (ie_list->PreNHtCapabilityLen > 0))
			ie_list->HtCapabilityLen = SIZE_HT_CAP_IE;
#endif /* DOT11_N_SUPPORT */

		/*
		   Housekeeping "SsidBssTab" table for later-on ROAMing usage.
		*/
		Bssidx = BssTableSearchWithSSID(&pAd->MlmeAux.SsidBssTab, ie_list->Bssid, ie_list->Ssid, ie_list->SsidLen, ie_list->Channel);
		if (Bssidx == BSS_NOT_FOUND)
		{
			/* discover new AP of this network, create BSS entry */
			Bssidx = BssTableSetEntry(pAd, &pAd->MlmeAux.SsidBssTab, ie_list, RealRssi, LenVIE, pVIE);
			if (Bssidx == BSS_NOT_FOUND)
				;
			else
			{
				PBSS_ENTRY	pBssEntry = &pAd->MlmeAux.SsidBssTab.BssEntry[Bssidx];
				memmove(&pBssEntry->PTSF[0], &Elem->Msg[24], 4);
				memmove(&pBssEntry->TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
				memmove(&pBssEntry->TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
				pBssEntry->Rssi = RealRssi;

				memmove(pBssEntry->MacAddr, ie_list->Addr2, ETH_ALEN);


			}
		}

		/*
			Update ScanTab
		*/
		Bssidx = BssTableSearch(&pAd->ScanTab, ie_list->Bssid, ie_list->Channel);
		if (Bssidx == BSS_NOT_FOUND)
		{
			/* discover new AP of this network, create BSS entry */
			Bssidx = BssTableSetEntry(pAd, &pAd->ScanTab, ie_list, RealRssi, LenVIE, pVIE);
			if (Bssidx == BSS_NOT_FOUND) /* return if BSS table full */
				goto LabelOK;

			memmove(pAd->ScanTab.BssEntry[Bssidx].PTSF, &Elem->Msg[24], 4);
			memmove(&pAd->ScanTab.BssEntry[Bssidx].TTSF[0], &Elem->TimeStamp.u.LowPart, 4);
			memmove(&pAd->ScanTab.BssEntry[Bssidx].TTSF[4], &Elem->TimeStamp.u.LowPart, 4);
			pAd->ScanTab.BssEntry[Bssidx].MinSNR = Elem->Signal % 10;
			if (pAd->ScanTab.BssEntry[Bssidx].MinSNR == 0)
				pAd->ScanTab.BssEntry[Bssidx].MinSNR = -5;

			memmove(pAd->ScanTab.BssEntry[Bssidx].MacAddr, ie_list->Addr2, ETH_ALEN);



		}

		/*
		    if the ssid matched & bssid unmatched, we should select the bssid with large value.
		    This might happened when two STA start at the same time
		*/
		if ((! is_my_bssid) && ADHOC_ON(pAd))
		{
			INT	i;
			/* Add the safeguard against the mismatch of adhoc wep status */
			if ((pAd->StaCfg.WepStatus != pAd->ScanTab.BssEntry[Bssidx].WepStatus) ||
				(pAd->StaCfg.AuthMode != pAd->ScanTab.BssEntry[Bssidx].AuthMode))
			{
				goto LabelOK;
			}
			/* collapse into the ADHOC network which has bigger BSSID value. */
			for (i = 0; i < 6; i++)
			{
				if (ie_list->Bssid[i] > pAd->CommonCfg.Bssid[i])
				{
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - merge to the IBSS with bigger BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n",
						PRINT_MAC(ie_list->Bssid)));
					AsicDisableSync(pAd);
					memcpy(pAd->CommonCfg.Bssid, ie_list->Bssid, ETH_ALEN);
					AsicSetBssid(pAd, pAd->CommonCfg.Bssid);
					MakeIbssBeacon(pAd);        /* re-build BEACON frame */
					AsicEnableIbssSync(pAd);    /* copy BEACON frame to on-chip memory */
					is_my_bssid = true;
					break;
				}
				else if (ie_list->Bssid[i] < pAd->CommonCfg.Bssid[i])
					break;
			}
		}


		NdisGetSystemUpTime(&Now);
		pBss = &pAd->ScanTab.BssEntry[Bssidx];
		pBss->Rssi = RealRssi;       /* lastest RSSI */
		pBss->LastBeaconRxTime = Now;   /* last RX timestamp */

		/*
		   BEACON from my BSSID - either IBSS or INFRA network
		*/
		if (is_my_bssid)
		{
			struct rxwi_nmac RxWI;
			u8 RXWISize = sizeof(struct rxwi_nmac);

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
			OVERLAP_BSS_SCAN_IE	BssScan;
			u8 				RegClass;
			bool 				brc;

			/* Read Beacon's Reg Class IE if any. */
			brc = PeerBeaconAndProbeRspSanity2(pAd, Elem->Msg, Elem->MsgLen, &BssScan, &RegClass);
			if (brc == true)
			{
				UpdateBssScanParm(pAd, BssScan);
				pAd->StaCfg.RegClass = RegClass;
			}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

			pAd->StaCfg.DtimCount = ie_list->DtimCount;
			pAd->StaCfg.DtimPeriod = ie_list->DtimPeriod;
			pAd->StaCfg.LastBeaconRxTime = Now;


			memset(&RxWI, 0, RXWISize);
			RxWI.RxWIRSSI0 = Elem->Rssi0;
			RxWI.RxWIRSSI1 = Elem->Rssi1;
			RxWI.RxWIRSSI2 = Elem->Rssi2;
			RxWI.RxWIPhyMode = 0; /* Prevent SNR calculate error. */

			if (INFRA_ON(pAd)) {
				MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[BSSID_WCID];
				if (pEntry)
					Update_Rssi_Sample(pAd,
							   &pEntry->RssiSample,
							   &RxWI);
			}

			Update_Rssi_Sample(pAd, &pAd->StaCfg.RssiSample, &RxWI);

			if ((pAd->CommonCfg.bIEEE80211H == 1) &&
				(ie_list->NewChannel != 0) &&
				(ie_list->Channel != ie_list->NewChannel))
			{
				/* Switching to channel 1 can prevent from rescanning the current channel immediately (by auto reconnection). */
				/* In addition, clear the MLME queue and the scan table to discard the RX packets and previous scanning results. */
				AsicSwitchChannel(pAd, 1, false);
				AsicLockChannel(pAd, 1);
			    LinkDown(pAd, false);
				MlmeQueueInit(pAd, &pAd->Mlme.Queue);
				BssTableInit(&pAd->ScanTab);
			    RTMPusecDelay(1000000);		/* use delay to prevent STA do reassoc */

				/* channel sanity check */
				for (index = 0 ; index < pAd->ChannelListNum; index++)
				{
					if (pAd->ChannelList[index].Channel == ie_list->NewChannel)
					{
						pAd->ScanTab.BssEntry[Bssidx].Channel = ie_list->NewChannel;
						pAd->CommonCfg.Channel = ie_list->NewChannel;
						AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, false);
						AsicLockChannel(pAd, pAd->CommonCfg.Channel);
						DBGPRINT(RT_DEBUG_TRACE, ("PeerBeacon - STA receive channel switch announcement IE (New Channel =%d)\n", ie_list->NewChannel));
						break;
					}
				}

				if (index >= pAd->ChannelListNum)
				{
					DBGPRINT_ERR(("PeerBeacon(can not find New Channel=%d in ChannelList[%d]\n", pAd->CommonCfg.Channel, pAd->ChannelListNum));
				}
			}

#ifdef WPA_SUPPLICANT_SUPPORT
			if (pAd->StaCfg.WpaSupplicantUP & WPA_SUPPLICANT_ENABLE_WPS) ;
			else
#endif /* WPA_SUPPLICANT_SUPPORT */
			{
				if ((((pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled) << 4) ^ ie_list->CapabilityInfo) & 0x0010)
				{
					/*
						To prevent STA connect to OPEN/WEP AP when STA is OPEN/NONE or
						STA connect to OPEN/NONE AP when STA is OPEN/WEP AP.
					*/
					DBGPRINT(RT_DEBUG_TRACE, ("%s:AP privacy:%x is differenct from STA privacy:%x\n",
								__FUNCTION__, (ie_list->CapabilityInfo & 0x0010) >> 4 ,
								pAd->StaCfg.WepStatus != Ndis802_11WEPDisabled));
					if (INFRA_ON(pAd))
					{
						LinkDown(pAd,false);
						BssTableInit(&pAd->ScanTab);
					}
					goto LabelOK;
				}
			}

#ifdef LINUX
#ifdef RT_CFG80211_SUPPORT
/*			CFG80211_BeaconCountryRegionParse(pAd, pVIE, LenVIE); */
#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX */

			if (ie_list->AironetCellPowerLimit != 0xFF)
			{
				/*
				   We get the Cisco (ccx) "TxPower Limit" required
				   Changed to appropriate TxPower Limit for Ciso Compatible Extensions
				*/
				ChangeToCellPowerLimit(pAd, ie_list->AironetCellPowerLimit);
			}
			else
			{
				/*
				   AironetCellPowerLimit equal to 0xFF means the Cisco (ccx) "TxPower Limit" not exist.
				   Used the default TX Power Percentage, that set from UI.
				*/
				pAd->CommonCfg.TxPowerPercentage = pAd->CommonCfg.TxPowerDefault;
			}

			if (ADHOC_ON(pAd) && (CAP_IS_IBSS_ON(ie_list->CapabilityInfo)))
			{
				u8 		MaxSupportedRateIn500Kbps = 0;
				u8 		idx;
				MAC_TABLE_ENTRY *pEntry;

				MaxSupportedRateIn500Kbps = dot11_max_sup_rate(ie_list->SupRateLen, &ie_list->SupRate[0],
																ie_list->ExtRateLen, &ie_list->ExtRate[0]);

				/* look up the existing table */
				pEntry = MacTableLookup(pAd, ie_list->Addr2);

				/*
				   Ad-hoc mode is using MAC address as BA session. So we need to continuously find newly joined adhoc station by receiving beacon.
				   To prevent always check this, we use wcid == RESERVED_WCID to recognize it as newly joined adhoc station.
				*/
				if ((ADHOC_ON(pAd) && ((!pEntry) || (pEntry && IS_ENTRY_NONE(pEntry)))) ||
					(pEntry && RTMP_TIME_AFTER(Now, pEntry->LastBeaconRxTime + ADHOC_ENTRY_BEACON_LOST_TIME)))
				{
					if (pEntry == NULL)
						/* Another adhoc joining, add to our MAC table. */
						pEntry = MacTableInsertEntry(pAd, ie_list->Addr2, BSS0, OPMODE_STA, false);

					if (pEntry == NULL)
						goto LabelOK;


#ifdef DOT11_VHT_AC
{
					bool result;
					IE_LISTS *ielist;

					ielist = kmalloc(sizeof(IE_LISTS), GFP_ATOMIC);
					if (!ielist)
						goto LabelOK;
					memset((u8 *)ielist, 0, sizeof(IE_LISTS));

					if (ie_list->vht_cap_len && ie_list->vht_op_len)
					{
						memmove(&ielist->vht_cap, &ie_list->vht_cap_ie, sizeof(VHT_CAP_IE));
						memmove(&ielist->vht_op, &ie_list->vht_op_ie, sizeof(VHT_OP_IE));
						ielist->vht_cap_len = ie_list->vht_cap_len;
						ielist->vht_op_len = ie_list->vht_op_len;
					}
					result = StaAddMacTableEntry(pAd,
											pEntry,
											MaxSupportedRateIn500Kbps,
											&ie_list->HtCapability,
											ie_list->HtCapabilityLen,
											&ie_list->AddHtInfo,
											ie_list->AddHtInfoLen,
											ielist,
											ie_list->CapabilityInfo);

					kfree(ielist);
					if ( result== false)
					{
						DBGPRINT(RT_DEBUG_TRACE, ("ADHOC - Add Entry failed.\n"));
						goto LabelOK;
					}
}
#else
					if (StaAddMacTableEntry(pAd,
											pEntry,
											MaxSupportedRateIn500Kbps,
											&ie_list->HtCapability,
											ie_list->HtCapabilityLen,
											&ie_list->AddHtInfo,
											ie_list->AddHtInfoLen,
											ie_list,
											ie_list->CapabilityInfo) == false)
					{
						DBGPRINT(RT_DEBUG_TRACE, ("ADHOC - Add Entry failed.\n"));
						goto LabelOK;
					}
#endif /* DOT11_VHT_AC */

					if (ADHOC_ON(pAd) && pEntry)
					{
						RTMPSetSupportMCS(pAd,
										OPMODE_STA,
										pEntry,
										ie_list->SupRate,
										ie_list->SupRateLen,
										ie_list->ExtRate,
										ie_list->ExtRateLen,
#ifdef DOT11_VHT_AC
										ie_list->vht_cap_len,
										&ie_list->vht_cap_ie,
#endif /* DOT11_VHT_AC */
										&ie_list->HtCapability,
										ie_list->HtCapabilityLen);
					}

					pEntry->LastBeaconRxTime = 0;


					if (pEntry && (Elem->Wcid == RESERVED_WCID))
					{
						idx = pAd->StaCfg.DefaultKeyId;
							RTMP_SET_WCID_SEC_INFO(pAd, BSS0, idx,
												   pAd->SharedKey[BSS0][idx].CipherAlg,
												   pEntry->Aid,
												   SHAREDKEYTABLE);
					}
				}

				if (pEntry && IS_ENTRY_CLIENT(pEntry))
				{
					pEntry->LastBeaconRxTime = Now;
				}

				/* At least another peer in this IBSS, declare MediaState as CONNECTED */
				if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
				{
					OPSTATUS_SET_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
					RTMP_IndicateMediaState(pAd, NdisMediaStateConnected);
	                pAd->ExtraInfo = GENERAL_LINK_UP;
					DBGPRINT(RT_DEBUG_TRACE, ("ADHOC  fOP_STATUS_MEDIA_STATE_CONNECTED.\n"));
				}
			}

			if (INFRA_ON(pAd))
			{
				bool bUseShortSlot, bUseBGProtection;

				/*
				   decide to use/change to -
				      1. long slot (20 us) or short slot (9 us) time
				      2. turn on/off RTS/CTS and/or CTS-to-self protection
				      3. short preamble
				*/


				/* bUseShortSlot = pAd->CommonCfg.bUseShortSlotTime && CAP_IS_SHORT_SLOT(CapabilityInfo); */
				bUseShortSlot = CAP_IS_SHORT_SLOT(ie_list->CapabilityInfo);
				if (bUseShortSlot != OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED))
					AsicSetSlotTime(pAd, bUseShortSlot);

				bUseBGProtection = (pAd->CommonCfg.UseBGProtection == 1) ||    /* always use */
								   ((pAd->CommonCfg.UseBGProtection == 0) && ERP_IS_USE_PROTECTION(ie_list->Erp));

				if (pAd->CommonCfg.Channel > 14)  /* always no BG protection in A-band. falsely happened when switching A/G band to a dual-band AP */
					bUseBGProtection = false;

				if (bUseBGProtection != OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					if (bUseBGProtection)
					{
						OPSTATUS_SET_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED);
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode,
											(OFDMSETPROTECT|CCKSETPROTECT|ALLN_SETPROTECT),
											false,(pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1));
					}
					else
					{
						OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED);
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode,
											(OFDMSETPROTECT|CCKSETPROTECT|ALLN_SETPROTECT),true,
											(pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1));
					}

					DBGPRINT(RT_DEBUG_WARN, ("SYNC - AP changed B/G protection to %d\n", bUseBGProtection));
				}

#ifdef DOT11_N_SUPPORT
				/* check Ht protection mode. and adhere to the Non-GF device indication by AP. */
				if ((ie_list->AddHtInfoLen != 0) &&
					((ie_list->AddHtInfo.AddHtInfo2.OperaionMode != pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode) ||
					(ie_list->AddHtInfo.AddHtInfo2.NonGfPresent != pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent)))
				{
					pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent = ie_list->AddHtInfo.AddHtInfo2.NonGfPresent;
					pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode = ie_list->AddHtInfo.AddHtInfo2.OperaionMode;
					if (pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent == 1)
				{
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, false, true);
					}
					else
						AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, false, false);

					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP changed N OperaionMode to %d\n", pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode));
				}
#endif /* DOT11_N_SUPPORT */

				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED) &&
					ERP_IS_USE_BARKER_PREAMBLE(ie_list->Erp))
				{
					MlmeSetTxPreamble(pAd, Rt802_11PreambleLong);
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP forced to use LONG preamble\n"));
				}

				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)    &&
					(ie_list->EdcaParm.bValid == true)                          &&
					(ie_list->EdcaParm.EdcaUpdateCount != pAd->CommonCfg.APEdcaParm.EdcaUpdateCount))
				{
					DBGPRINT(RT_DEBUG_TRACE, ("SYNC - AP change EDCA parameters(from %d to %d)\n",
						pAd->CommonCfg.APEdcaParm.EdcaUpdateCount,
						ie_list->EdcaParm.EdcaUpdateCount));
					AsicSetEdcaParm(pAd, &ie_list->EdcaParm);
				}

				/* copy QOS related information */
				memmove(&pAd->CommonCfg.APQbssLoad, &ie_list->QbssLoad, sizeof(QBSS_LOAD_PARM));
				memmove(&pAd->CommonCfg.APQosCapability, &ie_list->QosCapability, sizeof(QOS_CAPABILITY_PARM));
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
				/*
				   2009: PF#1: 20/40 Coexistence in 2.4 GHz Band
				   When AP changes "STA Channel Width" and "Secondary Channel Offset" fields of HT Operation Element in the Beacon to 0
				*/
				if ((ie_list->AddHtInfoLen != 0) && INFRA_ON(pAd) && pAd->CommonCfg.Channel <= 14)
				{
					bool bChangeBW = false;

					/*
					     1) HT Information
					     2) Secondary Channel Offset Element

					     40 -> 20 case
					*/
					if (pAd->CommonCfg.BBPCurrentBW == BW_40)
					{
						if (((ie_list->AddHtInfo.AddHtInfo.ExtChanOffset == EXTCHA_NONE) &&
							(ie_list->AddHtInfo.AddHtInfo.RecomWidth == 0))
							||(ie_list->NewExtChannelOffset==0x0)
						)
						{
							pAd->StaActive.SupportedHtPhy.ChannelWidth = BW_20;
							pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.BW = 0;

							{
								bChangeBW = true;
								pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
								DBGPRINT(RT_DEBUG_TRACE, ("FallBack from 40MHz to 20MHz(CtrlCh=%d, CentralCh=%d)\n",
															pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel));
								CntlChannelWidth(pAd, pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel, BW_20, 0);
							}
						}
					}

					/*
					    20 -> 40 case
					    1.) Supported Channel Width Set Field of the HT Capabilities element of both STAs is set to a non-zero
					    2.) Secondary Channel Offset field is SCA or SCB
					    3.) 40MHzRegulatoryClass is true (not implement it)
					*/
					else if (((pAd->CommonCfg.BBPCurrentBW == BW_20) ||(ie_list->NewExtChannelOffset!=0x0)) &&
							(pAd->CommonCfg.DesiredHtPhy.ChannelWidth != BW_20)
						)
					{
						if ((ie_list->AddHtInfo.AddHtInfo.ExtChanOffset != EXTCHA_NONE) &&
							(ie_list->AddHtInfo.AddHtInfo.RecomWidth == 1) &&
							(ie_list->HtCapabilityLen>0) && (ie_list->HtCapability.HtCapInfo.ChannelWidth == 1)
						)
						{
							{
								pAd->CommonCfg.CentralChannel = get_cent_ch_by_htinfo(pAd,
																		&ie_list->AddHtInfo,
																		&ie_list->HtCapability);
								if (pAd->CommonCfg.CentralChannel != ie_list->AddHtInfo.ControlChan)
									bChangeBW = true;

								if (bChangeBW)
								{
									pAd->CommonCfg.Channel = ie_list->AddHtInfo.ControlChan;
										pAd->StaActive.SupportedHtPhy.ChannelWidth = BW_40;
									DBGPRINT(RT_DEBUG_TRACE, ("FallBack from 20MHz to 40MHz(CtrlCh=%d, CentralCh=%d)\n",
																pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel));
									CntlChannelWidth(pAd, pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel, BW_40, ie_list->AddHtInfo.AddHtInfo.ExtChanOffset);
									pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.BW = 1;
								}
							}
						}
					}

					if (bChangeBW)
					{
						pAd->CommonCfg.BSSCoexist2040.word = 0;
						TriEventInit(pAd);
						BuildEffectedChannelList(pAd);
					}
				}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */
			}

			/* only INFRASTRUCTURE mode support power-saving feature */
			if ((INFRA_ON(pAd) && (RtmpPktPmBitCheck(pAd) == true)) || (pAd->CommonCfg.bAPSDForcePowerSave))
			{
				u8 FreeNumber;
				/*
				     1. AP has backlogged unicast-to-me frame, stay AWAKE, send PSPOLL
				     2. AP has backlogged broadcast/multicast frame and we want those frames, stay AWAKE
				     3. we have outgoing frames in TxRing or MgmtRing, better stay AWAKE
				     4. Psm change to PWR_SAVE, but AP not been informed yet, we better stay AWAKE
				     5. otherwise, put PHY back to sleep to save battery.
				*/
				if (ie_list->MessageToMe)
				{
#ifdef UAPSD_SUPPORT
					if (pAd->StaCfg.UapsdInfo.bAPSDCapable &&
						pAd->CommonCfg.APEdcaParm.bAPSDCapable &&
						pAd->CommonCfg.bAPSDAC_BE &&
						pAd->CommonCfg.bAPSDAC_BK &&
						pAd->CommonCfg.bAPSDAC_VI &&
						pAd->CommonCfg.bAPSDAC_VO)
					{
						pAd->CommonCfg.bNeedSendTriggerFrame = true;
					}
					else
#endif /* UAPSD_SUPPORT */
					{
						if (pAd->StaCfg.WindowsBatteryPowerMode == Ndis802_11PowerModeFast_PSP)
						{
							/* wake up and send a NULL frame with PM = 0 to the AP */
							RTMP_SET_PSM_BIT(pAd, PWR_ACTIVE);
							RTMPSendNullFrame(pAd,
											  pAd->CommonCfg.TxRate,
											  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? true:false),
											  PWR_ACTIVE);
						}
						else
						{
							/* use PS-Poll to get any buffered packet */
							RTMP_PS_POLL_ENQUEUE(pAd);
						}
					}
				}
				else if (ie_list->BcastFlag && (ie_list->DtimCount == 0) && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM))
				{
				}
				else if ((pAd->TxSwQueue[QID_AC_BK].Number != 0) ||
						(pAd->TxSwQueue[QID_AC_BE].Number != 0) ||
						(pAd->TxSwQueue[QID_AC_VI].Number != 0) ||
						(pAd->TxSwQueue[QID_AC_VO].Number != 0) ||
						(RTMPFreeTXDRequest(pAd, QID_AC_BK, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS) ||
						(RTMPFreeTXDRequest(pAd, QID_AC_BE, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS) ||
						(RTMPFreeTXDRequest(pAd, QID_AC_VI, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS) ||
						(RTMPFreeTXDRequest(pAd, QID_AC_VO, TX_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS) ||
						(RTMPFreeTXDRequest(pAd, QID_MGMT, MGMT_RING_SIZE - 1, &FreeNumber) != NDIS_STATUS_SUCCESS))
				{
					/* TODO: consider scheduled HCCA. might not be proper to use traditional DTIM-based power-saving scheme */
					/* can we cheat here (i.e. just check MGMT & AC_BE) for better performance? */
				}
				else
				{
					if ((pAd->CommonCfg.bACMAPSDTr[QID_AC_VO]) ||
						(pAd->CommonCfg.bACMAPSDTr[QID_AC_VI]) ||
						(pAd->CommonCfg.bACMAPSDTr[QID_AC_BK]) ||
						(pAd->CommonCfg.bACMAPSDTr[QID_AC_BE])
						)
					{
					}
					else
					{
						USHORT NextDtim = ie_list->DtimCount;

						if (NextDtim == 0)
							NextDtim = ie_list->DtimPeriod;

						TbttNumToNextWakeUp = pAd->StaCfg.DefaultListenCount;
						if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM) && (TbttNumToNextWakeUp > NextDtim))
							TbttNumToNextWakeUp = NextDtim;

						if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
						{
							/* Set a flag to go to sleep . Then after parse this RxDoneInterrupt, will go to sleep mode. */
							pAd->ThisTbttNumToNextWakeUp = TbttNumToNextWakeUp;
		                                        AsicSleepThenAutoWakeup(pAd, pAd->ThisTbttNumToNextWakeUp);

						}
					}
				}
			}
		}
		/* not my BSSID, ignore it */
	}
	/* sanity check fail, ignore this frame */
	goto LabelOK;

LabelErr:
	DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));

LabelOK:
	if (VarIE != NULL)
		kfree(VarIE);
	if (ie_list != NULL)
		kfree(ie_list);

	return;
}

/*
	==========================================================================
	Description:
		Receive PROBE REQ from remote peer when operating in IBSS mode
	==========================================================================
 */
void PeerProbeReqAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	u8         Addr2[ETH_ALEN];
	CHAR          Ssid[MAX_LEN_OF_SSID];
	u8         SsidLen;
#ifdef DOT11_N_SUPPORT
	u8 	  HtLen, AddHtLen, NewExtLen;
#endif /* DOT11_N_SUPPORT */
	HEADER_802_11 ProbeRspHdr;
	u8 *       pOutBuffer = NULL;
	ULONG         FrameLen = 0;
	LARGE_INTEGER FakeTimestamp;
	u8         DsLen = 1, IbssLen = 2;
	u8         LocalErpIe[3] = {IE_ERP, 1, 0};
	bool       Privacy;
	USHORT        CapabilityInfo;


	if (! ADHOC_ON(pAd))
		return;

	if (PeerProbeReqSanity(pAd, Elem->Msg, Elem->MsgLen, Addr2, Ssid, &SsidLen, NULL))
	{
		if ((SsidLen == 0) || SSID_EQUAL(Ssid, SsidLen, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen))
		{
			/* allocate and send out ProbeRsp frame */
			pOutBuffer = kmalloc(MGMT_DMA_BUFFER_SIZE, GFP_ATOMIC);  /* Get an unused nonpaged memory */
			if (!pOutBuffer)
				return;

			MgtMacHeaderInit(pAd, &ProbeRspHdr, SUBTYPE_PROBE_RSP, 0, Addr2,
								pAd->CommonCfg.Bssid);

			Privacy = (pAd->StaCfg.WepStatus == Ndis802_11Encryption1Enabled) ||
					  (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) ||
					  (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled);
			CapabilityInfo = CAP_GENERATE(0, 1, Privacy, (pAd->CommonCfg.TxPreamble == Rt802_11PreambleShort), 0, 0);

			MakeOutgoingFrame(pOutBuffer,                   &FrameLen,
							  sizeof(HEADER_802_11),        &ProbeRspHdr,
							  TIMESTAMP_LEN,                &FakeTimestamp,
							  2,                            &pAd->CommonCfg.BeaconPeriod,
							  2,                            &CapabilityInfo,
							  1,                            &SsidIe,
							  1,                            &pAd->CommonCfg.SsidLen,
							  pAd->CommonCfg.SsidLen,       pAd->CommonCfg.Ssid,
							  1,                            &SupRateIe,
							  1,                            &pAd->StaActive.SupRateLen,
							  pAd->StaActive.SupRateLen,    pAd->StaActive.SupRate,
							  1,                            &DsIe,
							  1,                            &DsLen,
							  1,                            &pAd->CommonCfg.Channel,
							  1,                            &IbssIe,
							  1,                            &IbssLen,
							  2,                            &pAd->StaActive.AtimWin,
							  END_OF_ARGS);

			if (pAd->StaActive.ExtRateLen)
			{
				ULONG tmp;
				MakeOutgoingFrame(pOutBuffer + FrameLen,        &tmp,
								  3,                            LocalErpIe,
								  1,                            &ExtRateIe,
								  1,                            &pAd->StaActive.ExtRateLen,
								  pAd->StaActive.ExtRateLen,    &pAd->StaActive.ExtRate,
								  END_OF_ARGS);
				FrameLen += tmp;
			}

        	/* Modify by Eddy, support WPA2PSK in Adhoc mode */
        	if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
                )
        	{
        		ULONG   tmp;
       	        u8   RSNIe = IE_WPA;


        		MakeOutgoingFrame(pOutBuffer + FrameLen,        	&tmp,
        						  1,                              	&RSNIe,
        						  1,                            	&pAd->StaCfg.RSNIE_Len,
        						  pAd->StaCfg.RSNIE_Len,      		pAd->StaCfg.RSN_IE,
        						  END_OF_ARGS);
        		FrameLen += tmp;
        	}

#ifdef DOT11_N_SUPPORT
			if (WMODE_CAP_N(pAd->CommonCfg.PhyMode))
			{
				ULONG TmpLen;
				USHORT  epigram_ie_len;
				u8 BROADCOM[4] = {0x0, 0x90, 0x4c, 0x33};
				HtLen = sizeof(pAd->CommonCfg.HtCapability);
				AddHtLen = sizeof(pAd->CommonCfg.AddHTInfo);
				NewExtLen = 1;
				/* New extension channel offset IE is included in Beacon, Probe Rsp or channel Switch Announcement Frame */
				if (pAd->bBroadComHT == true)
				{
					epigram_ie_len = pAd->MlmeAux.HtCapabilityLen + 4;
					MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
								  1,                                &WpaIe,
								  1,          						&epigram_ie_len,
								  4,                                &BROADCOM[0],
								 pAd->MlmeAux.HtCapabilityLen,          &pAd->MlmeAux.HtCapability,
								  END_OF_ARGS);
				}
				else
				{
				MakeOutgoingFrame(pOutBuffer + FrameLen,            &TmpLen,
								  1,                                &HtCapIe,
								  1,                                &HtLen,
								 sizeof(HT_CAPABILITY_IE),          &pAd->CommonCfg.HtCapability,
								  1,                                &AddHtInfoIe,
								  1,                                &AddHtLen,
								 sizeof(ADD_HT_INFO_IE),          &pAd->CommonCfg.AddHTInfo,
								  END_OF_ARGS);
				}
				FrameLen += TmpLen;
			}
#endif /* DOT11_N_SUPPORT */



			MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
			kfree(pOutBuffer);
		}
	}
}

void BeaconTimeoutAtJoinAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("SYNC - BeaconTimeoutAtJoinAction\n"));
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_REJ_TIMEOUT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status, 0);
}

/*
	==========================================================================
	Description:
		Scan timeout procedure. basically add channel index by 1 and rescan
	==========================================================================
 */
void ScanTimeoutAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{

#ifdef RTMP_MAC_USB
	/*
	    To prevent data lost.
	    Send an NULL data with turned PSM bit on to current associated AP when SCAN in the channel where
	    associated AP located.
	*/
	if ((pAd->CommonCfg.Channel == pAd->MlmeAux.Channel) &&
		(pAd->MlmeAux.ScanType == SCAN_ACTIVE) &&
		(INFRA_ON(pAd)) &&
		OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
	)
	{
		RTMPSendNullFrame(pAd,
						  pAd->CommonCfg.TxRate,
						  (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? true:false),
						  PWR_SAVE);
	}
#endif /* RTMP_MAC_USB */

	if (pAd->StaCfg.bFastConnect && !pAd->StaCfg.bNotFirstScan)
	{
		pAd->MlmeAux.Channel = 0;
		pAd->StaCfg.bNotFirstScan = true;
	}
	else
	{
		pAd->MlmeAux.Channel = NextChannel(pAd, pAd->MlmeAux.Channel);
	}

	/* Only one channel scanned for CISCO beacon request */
	if ((pAd->MlmeAux.ScanType == SCAN_CISCO_ACTIVE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_PASSIVE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_NOISE) ||
		(pAd->MlmeAux.ScanType == SCAN_CISCO_CHANNEL_LOAD))
		pAd->MlmeAux.Channel = 0;

	/* this routine will stop if pAd->MlmeAux.Channel == 0 */
	ScanNextChannel(pAd, OPMODE_STA);
}

/*
	==========================================================================
	Description:
	==========================================================================
 */
void InvalidStateWhenScan(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;

	if (Elem->MsgType != MT2_MLME_SCAN_REQ)
		DBGPRINT(RT_DEBUG_TRACE, ("AYNC - InvalidStateWhenScan(state=%ld). Reset SYNC machine\n", pAd->Mlme.SyncMachine.CurrState));
	else
		DBGPRINT(RT_DEBUG_TRACE, ("AYNC - Already in scanning, do nothing here.(state=%ld). \n", pAd->Mlme.SyncMachine.CurrState));

	if (Elem->MsgType != MT2_MLME_SCAN_REQ)
	{
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		Status = MLME_STATE_MACHINE_REJECT;
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_SCAN_CONF, 2, &Status, 0);
	}
}

/*
	==========================================================================
	Description:
	==========================================================================
 */
void InvalidStateWhenJoin(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("InvalidStateWhenJoin(state=%ld, msg=%ld). Reset SYNC machine\n",
								pAd->Mlme.SyncMachine.CurrState,
								Elem->MsgType));
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
	{
		RTMPResumeMsduTransmission(pAd);
	}
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_JOIN_CONF, 2, &Status, 0);
}

/*
	==========================================================================
	Description:
	==========================================================================
 */
void InvalidStateWhenStart(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	USHORT Status;
	DBGPRINT(RT_DEBUG_TRACE, ("InvalidStateWhenStart(state=%ld). Reset SYNC machine\n", pAd->Mlme.SyncMachine.CurrState));
	pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
	Status = MLME_STATE_MACHINE_REJECT;
	MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_START_CONF, 2, &Status, 0);
}

/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void EnqueuePsPoll(
	IN struct rtmp_adapter *pAd)
{
	if (pAd->StaCfg.WindowsPowerMode == Ndis802_11PowerModeLegacy_PSP)
    	pAd->PsPollFrame.FC.PwrMgmt = PWR_SAVE;
	MiniportMMRequest(pAd, 0, (u8 *)&pAd->PsPollFrame, sizeof(PSPOLL_FRAME));
#ifdef RTMP_MAC_USB
	/* Keep Waking up */
	if (pAd->CountDowntoPsm == 0)
		pAd->CountDowntoPsm = 2;	/* 100 ms; stay awake 200ms at most, average will be 1xx ms */
#endif /* RTMP_MAC_USB */

}


/*
	==========================================================================
	Description:
	==========================================================================
 */
void EnqueueProbeRequest(
	IN struct rtmp_adapter *pAd)
{
	u8 *         pOutBuffer;
	ULONG           FrameLen = 0;
	HEADER_802_11   Hdr80211;

	DBGPRINT(RT_DEBUG_TRACE, ("force out a ProbeRequest ...\n"));

	pOutBuffer = kmalloc(MGMT_DMA_BUFFER_SIZE, GFP_ATOMIC);  /* Get an unused nonpaged memory */
	if (pOutBuffer)
	{
		MgtMacHeaderInit(pAd, &Hdr80211, SUBTYPE_PROBE_REQ, 0, BROADCAST_ADDR,
							BROADCAST_ADDR);

		/* this ProbeRequest explicitly specify SSID to reduce unwanted ProbeResponse */
		MakeOutgoingFrame(pOutBuffer,                     &FrameLen,
						  sizeof(HEADER_802_11),          &Hdr80211,
						  1,                              &SsidIe,
						  1,                              &pAd->CommonCfg.SsidLen,
						  pAd->CommonCfg.SsidLen,		  pAd->CommonCfg.Ssid,
						  1,                              &SupRateIe,
						  1,                              &pAd->StaActive.SupRateLen,
						  pAd->StaActive.SupRateLen,      pAd->StaActive.SupRate,
						  END_OF_ARGS);
		MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
		kfree(pOutBuffer);
	}

}


#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
void BuildEffectedChannelList(
	IN struct rtmp_adapter *pAd)
{
	u8 	EChannel[11];
	u8 	i, j, k;
	u8 	UpperChannel = 0, LowerChannel = 0;

	memset(EChannel, 0, 11);
	DBGPRINT(RT_DEBUG_TRACE, ("BuildEffectedChannelList:CtrlCh=%d,CentCh=%d,AuxCtrlCh=%d,AuxExtCh=%d\n",
								pAd->CommonCfg.Channel, pAd->CommonCfg.CentralChannel,
								pAd->MlmeAux.AddHtInfo.ControlChan,
								pAd->MlmeAux.AddHtInfo.AddHtInfo.ExtChanOffset));

	/* 802.11n D4 11.14.3.3: If no secondary channel has been selected, all channels in the frequency band shall be scanned. */
	{
		for (k = 0;k < pAd->ChannelListNum;k++)
		{
			if (pAd->ChannelList[k].Channel <=14 )
			pAd->ChannelList[k].bEffectedChannel = true;
		}
		return;
	}

	i = 0;
	/* Find upper and lower channel according to 40MHz current operation. */
	if (pAd->CommonCfg.CentralChannel < pAd->CommonCfg.Channel)
	{
		UpperChannel = pAd->CommonCfg.Channel;
		LowerChannel = pAd->CommonCfg.CentralChannel-2;
	}
	else if (pAd->CommonCfg.CentralChannel > pAd->CommonCfg.Channel)
	{
		UpperChannel = pAd->CommonCfg.CentralChannel+2;
		LowerChannel = pAd->CommonCfg.Channel;
	}
	else
	{
		DBGPRINT(RT_DEBUG_TRACE, ("LinkUP 20MHz . No Effected Channel \n"));
		/* Now operating in 20MHz, doesn't find 40MHz effected channels */
		return;
	}

	DeleteEffectedChannelList(pAd);

	DBGPRINT(RT_DEBUG_TRACE, ("BuildEffectedChannelList!LowerChannel ~ UpperChannel; %d ~ %d \n", LowerChannel, UpperChannel));

	/* Find all channels that are below lower channel.. */
	if (LowerChannel > 1)
	{
		EChannel[0] = LowerChannel - 1;
		i = 1;
		if (LowerChannel > 2)
		{
			EChannel[1] = LowerChannel - 2;
			i = 2;
			if (LowerChannel > 3)
			{
				EChannel[2] = LowerChannel - 3;
				i = 3;
			}
		}
	}
	/* Find all channels that are between  lower channel and upper channel. */
	for (k = LowerChannel;k <= UpperChannel;k++)
	{
		EChannel[i] = k;
		i++;
	}
	/* Find all channels that are above upper channel.. */
	if (UpperChannel < 14)
	{
		EChannel[i] = UpperChannel + 1;
		i++;
		if (UpperChannel < 13)
		{
			EChannel[i] = UpperChannel + 2;
			i++;
			if (UpperChannel < 12)
			{
				EChannel[i] = UpperChannel + 3;
				i++;
			}
		}
	}
	/*
	    Total i channels are effected channels.
	    Now find corresponding channel in ChannelList array.  Then set its bEffectedChannel= true
	*/
	for (j = 0;j < i;j++)
	{
		for (k = 0;k < pAd->ChannelListNum;k++)
		{
			if (pAd->ChannelList[k].Channel == EChannel[j])
			{
				pAd->ChannelList[k].bEffectedChannel = true;
				DBGPRINT(RT_DEBUG_TRACE,(" EffectedChannel[%d]( =%d)\n", k, EChannel[j]));
				break;
			}
		}
	}
}


void DeleteEffectedChannelList(
	IN struct rtmp_adapter *pAd)
{
	u8 	i;
	/*Clear all bEffectedChannel in ChannelList array. */
 	for (i = 0; i < pAd->ChannelListNum; i++)
	{
		pAd->ChannelList[i].bEffectedChannel = false;
	}
}


/*
	========================================================================

	Routine Description:
		Control Primary&Central Channel, ChannelWidth and Second Channel Offset

	Arguments:
		pAd						Pointer to our adapter
		PrimaryChannel			Primary Channel
		CentralChannel			Central Channel
		ChannelWidth				BW_20 or BW_40
		SecondaryChannelOffset	EXTCHA_NONE, EXTCHA_ABOVE and EXTCHA_BELOW

	Return Value:
		None

	Note:

	========================================================================
*/
void CntlChannelWidth(
	IN struct rtmp_adapter *pAd,
	IN u8 prim_ch,
	IN u8 cent_ch,
	IN u8 ch_bw,
	IN u8 sec_ch_offset)
{
	u8 rf_channel = 0, rf_bw;
	INT32 ext_ch;


	DBGPRINT(RT_DEBUG_TRACE, ("%s: PrimaryChannel[%d] \n",__FUNCTION__,prim_ch));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: CentralChannel[%d] \n",__FUNCTION__,cent_ch));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: ChannelWidth[%d] \n",__FUNCTION__,ch_bw));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: SecondaryChannelOffset[%d] \n",__FUNCTION__,sec_ch_offset));

#ifdef DOT11_N_SUPPORT
	/*Change to AP channel */
	if (ch_bw == BW_40)
	{
		if(sec_ch_offset == EXTCHA_ABOVE)
		{
			rf_bw = BW_40;
			ext_ch = EXTCHA_ABOVE;
			rf_channel = cent_ch;
		}
		else if (sec_ch_offset == EXTCHA_BELOW)
		{
			rf_bw = BW_40;
			ext_ch = EXTCHA_BELOW;
			rf_channel = cent_ch;
		}
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		rf_bw = BW_20;
		ext_ch = EXTCHA_NONE;
		rf_channel = prim_ch;
	}

	if (rf_channel != 0) {
		rtmp_bbp_set_bw(pAd, rf_bw);

		/* Tx/ RX : control channel setting */
		rtmp_bbp_set_ctrlch(pAd, ext_ch);
		rtmp_mac_set_ctrlch(pAd, ext_ch);

		AsicSwitchChannel(pAd, rf_channel, false);
		AsicLockChannel(pAd, rf_channel);

		DBGPRINT(RT_DEBUG_TRACE, ("!!!40MHz Lower !!! Control Channel at Below. Central = %d \n", pAd->CommonCfg.CentralChannel ));

		rtmp_bbp_get_agc(pAd, &pAd->BbpTuning.R66CurrentValue, RX_CHAIN_0);
	}
}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

/*
    ==========================================================================
    Description:
        MLME Cancel the SCAN req state machine procedure
    ==========================================================================
 */
void ScanCnclAction(
	IN struct rtmp_adapter *pAd,
	IN MLME_QUEUE_ELEM *Elem)
{
	bool Cancelled;

	RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &Cancelled);
	pAd->MlmeAux.Channel = 0;
	ScanNextChannel(pAd, OPMODE_STA);

	return;
}

