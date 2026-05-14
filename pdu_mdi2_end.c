/* =========================================================================
* PDUGetStatus
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetStatus(T_PDU_UINT32 hMod,
	T_PDU_UINT32 hConn,
	T_PDU_UINT32 hCoPrimitive,
	T_PDU_UINT32 *pStatus,
	T_PDU_UINT32 param5,
	T_PDU_UINT32 param6)
{
	logmsg("PDUGetStatus: CALLED hMod=%lu hConn=%lu hCoPrimitive=%lu",
		hMod, hConn, hCoPrimitive);

	UNUSED(hMod); UNUSED(param5); UNUSED(param6);

	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!pStatus) return PDU_ERR_INVALID_PARAMETERS;

	if (hCoPrimitive == PDU_HANDLE_UNDEF || hCoPrimitive == 0xFFFFFFFF || hCoPrimitive == 0) {
		PDU_CONN_STATE *c = GetConn(hConn);
		if (c) {
			*pStatus = PDU_CLLST_COMM_STARTED;
		}
		else {
			*pStatus = PDU_CLLST_OFFLINE;
		}
	}
	else {
		PDU_CONN_STATE *c = GetConn(hConn);
		if (c && c->LastCoPrimHandle == hCoPrimitive) {
			if (c->PrimitiveActive) {
				if (GetTickCount() - c->LastCoPrimTime > 250) {
					logmsg("PDUGetStatus: primitive %lu timeout, forcing FINISHED", hCoPrimitive);
					c->PrimitiveActive = 0;
					*pStatus = PDU_COPST_FINISHED;
				}
				else {
					*pStatus = PDU_COPST_EXECUTING;
				}
			}
			else {
				*pStatus = PDU_COPST_FINISHED;
			}
		}
		else {
			*pStatus = PDU_COPST_FINISHED;
		}
	}

	logmsg("PDUGetStatus: EXIT status=0x%04X", *pStatus);
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUDestroyItem / PDUDestroyItems - free memory returned to Tech2Win
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUDestroyItem(PDU_ITEM *p)
{
	logmsg("PDUDestroyItem: ENTER p=%p ItemType=%lu", p, p ? p->ItemType : 0);
	if (!p) return PDU_STATUS_NOERROR;
	if (p == (PDU_ITEM*)&g_EmptyEvent) return PDU_STATUS_NOERROR;

	if (p->ItemType == PDU_IT_RESULT) {
		PDU_EVENT_ITEM *ev = (PDU_EVENT_ITEM*)p;
		PDU_RESULT_DATA *r = (PDU_RESULT_DATA*)ev->pData;
		if (r) {
			if (r->pDataBytes) free(r->pDataBytes);
			if (r->TimestampFlags.pFlagData) free(r->TimestampFlags.pFlagData);
			free(r);
		}
		free(p);
	}
	else if (p->ItemType == PDU_IT_STATUS) {
		PDU_EVENT_ITEM *ev = (PDU_EVENT_ITEM*)p;
		if (ev->pData) free(ev->pData);
		free(p);
	}
	else if (p->ItemType == PDU_IT_IO_UNUM32) {
		PDU_DATA_ITEM *di = (PDU_DATA_ITEM*)p;
		if (di->pData) free(di->pData);
		free(p);
	}
	else if (p->ItemType == PDU_IT_MODULE_ID) {
		PDU_MODULE_ITEM *mi = (PDU_MODULE_ITEM*)p;
		if (mi->pModuleData) free(mi->pModuleData);
		free(p);
	}
	else if (p->ItemType == PDU_IT_RSC_ID) {
		PDU_RSC_ID_ITEM *ri = (PDU_RSC_ID_ITEM*)p;
		if (ri->pResourceIdData) free(ri->pResourceIdData);
		free(p);
	}
	else if (p->ItemType == PDU_IT_UNIQUE_RESP_ID_TABLE) {
		PDU_UNIQUE_RESP_ID_TABLE_ITEM *tbl = (PDU_UNIQUE_RESP_ID_TABLE_ITEM*)p;
		if (tbl->pUniqueData) {
			for (UNUM32 i = 0; i < tbl->NumEntries; i++) {
				if (tbl->pUniqueData[i].pParams) {
					for (UNUM32 p = 0; p < tbl->pUniqueData[i].NumParamItems; p++) {
						free(tbl->pUniqueData[i].pParams[p].pComParamData);
					}
					free(tbl->pUniqueData[i].pParams);
				}
			}
			free(tbl->pUniqueData);
		}
		free(p);
	}

	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* Remaining stubs
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetObjectIds(
	T_PDU_UINT32    ObjectType,
	T_PDU_UINT32    NumItems,
	const char    **ppShortNames,
	T_PDU_UINT32  **ppIds)
{
	logmsg("PDUGetObjectIds: CALLED ObjectType=0x%04X NumItems=%u", ObjectType, NumItems);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	if (!ppShortNames || !ppIds)
		return PDU_ERR_INVALID_PARAMETERS;

	for (T_PDU_UINT32 i = 0; i < NumItems; i++) {
		const char *name = ppShortNames[i];
		T_PDU_UINT32 *pId = ppIds[i];

		if (!name || !pId) continue;

		*pId = PDU_ID_UNDEF;

		switch (ObjectType) {
		case PDU_OBJT_PROTOCOL:
			if (strstr(name, "J2190")) { *pId = PROTO_ID_J2190_J1850VPW;    break; }
			if (strstr(name, "15031")) { *pId = PROTO_ID_ISO15031_J1850VPW; break; }
			if (strstr(name, "OBD")) { *pId = PROTO_ID_ISOOB_J1850;       break; }
			break;
		case PDU_OBJT_BUSTYPE:
			if (strcmp(name, "SAE_J1850_VPW") == 0) { *pId = BUSTYPE_J1850_VPW; }
			break;
		case PDU_OBJT_RESOURCE:
			for (int r = 0; r < NUM_RESOURCES; r++)
				if (strcmp(name, g_Resources[r].ShortName) == 0)
				{
					*pId = g_Resources[r].ResourceId; break;
				}
			break;
		case PDU_OBJT_IO_CTRL:
		case PDU_OBJT_COMPARAM:
		case PDU_OBJT_PINTYPE:
			break;
		}

		logmsg("PDUGetObjectIds:  [%u] %s -> %u", i, name, *pId);
	}

	return PDU_STATUS_NOERROR;
}

PDU_API T_PDU_UINT32 PDU_CALL PDUGetResourceStatus(void)
{
	return PDU_STATUS_NOERROR;
}


PDU_API T_PDU_UINT32 PDU_CALL PDUGetConflictingResources(void)
{
	return PDU_STATUS_NOERROR;
}


PDU_API T_PDU_UINT32 PDU_CALL PDULockResource(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, T_PDU_UINT32 mask)
{
	logmsg("PDULockResource: CALLED");
	UNUSED(hMod); UNUSED(hConn); UNUSED(mask); return PDU_STATUS_NOERROR;
}

PDU_API T_PDU_UINT32 PDU_CALL PDUUnlockResource(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, T_PDU_UINT32 mask)
{
	logmsg("PDUUnlockResource: CALLED");
	UNUSED(hMod); UNUSED(hConn); UNUSED(mask); return PDU_STATUS_NOERROR;
}

PDU_API T_PDU_ERROR PDU_CALL PDUGetUniqueRespIdTable(
	UNUM32 hMod, UNUM32 hCLL,
	PDU_UNIQUE_RESP_ID_TABLE_ITEM **pUniqueRespIdTable)
{
	UNUSED(hMod);
	logmsg("PDUGetUniqueRespIdTable: CALLED hCLL=%u", hCLL);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!pUniqueRespIdTable)
		return PDU_ERR_INVALID_PARAMETERS;

	PDU_CONN_STATE *c = GetConn(hCLL);
	if (!c)
		return PDU_ERR_INVALID_HANDLE;

	/* Allocate table container */
	PDU_UNIQUE_RESP_ID_TABLE_ITEM *tbl =
		(PDU_UNIQUE_RESP_ID_TABLE_ITEM*)calloc(1, sizeof(*tbl));
	if (!tbl) return PDU_ERR_FCT_FAILED;

	/* Allocate 1 entry (template) */
	PDU_ECU_UNIQUE_RESP_DATA *entry =
		(PDU_ECU_UNIQUE_RESP_DATA*)calloc(1, sizeof(*entry));
	if (!entry) { free(tbl); return PDU_ERR_FCT_FAILED; }

	/* Allocate 3 params for the template: SourceAddr, Format, TargetAddr */
	PDU_PARAM_ITEM *params = (PDU_PARAM_ITEM*)calloc(3, sizeof(PDU_PARAM_ITEM));
	if (!params) { free(entry); free(tbl); return PDU_ERR_FCT_FAILED; }

	tbl->ItemType = PDU_IT_UNIQUE_RESP_ID_TABLE;
	tbl->NumEntries = 1;
	tbl->pUniqueData = entry;

	entry->UniqueRespIdentifier = PDU_ID_UNDEF;
	entry->NumParamItems = 3;
	entry->pParams = params;

	// Param 0: CP_EcuRespSourceAddress
	params[0].ItemType = PDU_IT_PARAM;
	params[0].ComParamId = CP_EcuRespSourceAddress;
	params[0].ComParamDataType = PDU_PT_UNUM32;
	params[0].ComParamClass = PDU_PC_PROTOCOL;
	params[0].pComParamData = calloc(1, 4);

	// Param 1: CP_FuncRespFormatPriorityType
	params[1].ItemType = PDU_IT_PARAM;
	params[1].ComParamId = CP_FuncRespFormatPriorityType;
	params[1].ComParamDataType = PDU_PT_UNUM32;
	params[1].ComParamClass = PDU_PC_PROTOCOL;
	params[1].pComParamData = calloc(1, 4);

	// Param 2: CP_FuncRespTargetAddr
	params[2].ItemType = PDU_IT_PARAM;
	params[2].ComParamId = CP_FuncRespTargetAddr;
	params[2].ComParamDataType = PDU_PT_UNUM32;
	params[2].ComParamClass = PDU_PC_PROTOCOL;
	params[2].pComParamData = calloc(1, 4);

	*pUniqueRespIdTable = tbl;

	logmsg("PDUGetUniqueRespIdTable: returned standard MDI template (3 params)");
	return PDU_STATUS_NOERROR;
}


PDU_API T_PDU_UINT32 PDU_CALL PDUDestroyItems(void)
{

	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUGetDeviceIds
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetDeviceIds(
	T_PDU_UINT32   hMod,
	PDU_DEVICE_ITEM **pDeviceIdList)
{
	logmsg("PDUGetDeviceIds: ENTER hMod=%u", hMod);

	if (!pDeviceIdList) return PDU_ERR_INVALID_PARAMETERS;

	PDU_DEVICE_ITEM *item = (PDU_DEVICE_ITEM*)calloc(1, sizeof(PDU_DEVICE_ITEM));
	if (!item) return PDU_ERR_FAILED;

	PDU_DEVICE_DATA *devs = (PDU_DEVICE_DATA*)calloc(1, sizeof(PDU_DEVICE_DATA));
	if (!devs) { free(item); return PDU_ERR_FAILED; }

	item->ItemType = (T_PDU_IT)0x1800;
	item->NumEntries = 1;
	item->pDeviceData = devs;

	devs[0].hMod = 1;
	devs[0].hDev = 0; // Match MDI log (ID=0)
	devs[0].DeviceStatus = PDU_DEVST_AVAIL;

	*pDeviceIdList = item;
	logmsg("PDUGetDeviceIds: EXIT OK (1 device, id=0)");
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUOpenDevice
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUOpenDevice(T_PDU_UINT32 hMod, T_PDU_UINT32 hDev, T_PDU_UINT32 *phDev)
{
	logmsg("PDUOpenDevice: CALLED hMod=%u hDev=%u", hMod, hDev);
	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!phDev) return PDU_ERR_INVALID_PARAMETERS;

	if (hDev != 0) return PDU_ERR_INVALID_HANDLE;

	*phDev = 0;
	return PDU_STATUS_NOERROR;
}


// --- VCI STUBS ---------------------------------------------------------

PDU_API T_PDU_UINT32 PDU_CALL VCIAPIDestruct(void)
{
	logmsg("VCIAPIDestruct: STUB");
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIConnect(char *name, T_PDU_UINT32 *hVci)
{
	logmsg("VCIConnect: STUB name=%s", name ? name : "(null)");
	if (hVci) *hVci = 1;
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIConnectIP(char *addr, T_PDU_UINT32 *hVci)
{
	logmsg("VCIConnectIP: STUB addr=%s", addr ? addr : "(null)");
	if (hVci) *hVci = 1;
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIDisconnect(T_PDU_UINT32 hVci)
{
	logmsg("VCIDisconnect: STUB hVci=%u", hVci);
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIRegisterEventCallback(void *cb, void *ctx)
{
	logmsg("VCIRegisterEventCallback: STUB cb=%p ctx=%p", cb, ctx);
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCISetLogServer(char *addr, T_PDU_UINT32 port)
{
	logmsg("VCISetLogServer: STUB addr=%s port=%u", addr ? addr : "(null)", port);
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIStopLogging(void)
{
	logmsg("VCIStopLogging: STUB");
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIGetLED(T_PDU_UINT32 hVci,
	T_PDU_UINT32 led,
	T_PDU_UINT32 *state)
{
	logmsg("VCIGetLED: STUB hVci=%u led=%u", hVci, led);
	if (state) *state = 0;
	return 0;
}

PDU_API T_PDU_UINT32 PDU_CALL VCIGetA2D(T_PDU_UINT32 hVci,
	T_PDU_UINT32 ch,
	T_PDU_UINT32 *value)
{
	logmsg("VCIGetA2D: STUB hVci=%u ch=%u", hVci, ch);
	if (value) *value = 0;
	return 0;
}