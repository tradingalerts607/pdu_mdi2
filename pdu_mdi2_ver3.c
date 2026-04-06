	/*
	* pdu_mdi2.c
	* ISO 22900-2 D-PDU API shim — GM MDI 2 / SM2 Pro / Tech2Win
	*
	* Data flow:
	*   Tech2Win
	*     +- PDUStartComPrimitive()  --? PassThruWriteMsgs()  --? SM2 hardware --? VPW bus
	*     +- PDUGetEventItem()       ?-- event queue          ?-- RX thread
	*                                                              +- PassThruReadMsgs() ?-- SM2
	*
	* Build (MinGW):
	*   gcc -shared -o pdu_mdi2.dll pdu_mdi2.c -lole32 -Wl,--out-implib,pdu_mdi2.lib
	* Build (MSVC):
	*   cl /LD pdu_mdi2.c /Fe:pdu_mdi2.dll ole32.lib
	*
	* Deployment:
	*   1. Run install_registry.reg (or pdu_mdi2.dll --register) to register the DLL
	*   2. Place pdu_mdi2.dll + MDI2.mdi in same directory
	*   3. smj2534.dll must be on PATH or same directory
	*/
#include <stdio.h>
#include <stdio.h>
#include <stdarg.h>

static void logmsg(const char *fmt, ...)
{
    FILE *f = fopen("C:\\GM\\mdi2_shim.log", "a");
    if (!f) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    va_end(args);

    fclose(f);
}

	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <stdint.h>
	#include <stdio.h>
	#include <string.h>
	#include <stdlib.h>
	#include <process.h>    /* _beginthreadex */

	#define PDU_API     __declspec(dllexport)
	#define UNUSED(x)   (void)(x)

	/* =========================================================================
	* PDU Return codes (ISO 22900-2 Table A.2)
	* ====================================================================== */
	#define PDU_STATUS_NOERROR              0x00000000UL
	#define PDU_ERR_FCT_FAILED              0x00000001UL
	#define PDU_ERR_COMM_PC_TO_VCI_FAILED   0x00000002UL
	#define PDU_ERR_PDUAPI_NOT_CONSTRUCTED  0x00000003UL
	#define PDU_ERR_MODULE_NOT_CONNECTED    0x00000010UL
	#define PDU_ERR_RESOURCE_NOT_LOCKED     0x00000020UL
	#define PDU_ERR_COMPARAM_NOT_SUPPORTED  0x00000030UL
	#define PDU_ERR_INVALID_PARAMETERS      0x00000050UL
	#define PDU_ERR_ITEM_NOT_FOUND          0x00000060UL

	#define PDU_HANDLE_UNDEF                0xFFFFFFFEUL
	#define PDU_ID_UNDEF                    0xFFFFFFFEUL

	/* PDU object types */
	#define PDU_OBJT_MODULEITEM             0x00000000UL
	#define PDU_OBJT_COMPARAM               0x00000001UL
	#define PDU_OBJT_BUSTYPE                0x00000002UL
	#define PDU_OBJT_IO_CTRL                0x00000003UL
	#define PDU_OBJT_PROTOCOL               0x00000004UL
	#define PDU_OBJT_RESOURCE               0x00000005UL

	/* PDU event types */
	#define PDU_EVT_DATA_RECEIVED           0x00000001UL
	#define PDU_EVT_ERROR                   0x00000100UL

	/* =========================================================================
	* MDI constants (sourced from MDI2.mdi XML)
	* ====================================================================== */
	#define MDI2_MODULE_TYPE_ID             28UL

	#define RSC_ID_J2190_ON_J1850VPW_2      395UL
	#define RSC_ID_OBD2_ON_J1850VPW_2       1005UL
	#define RSC_ID_OBD2DSC_ON_J1850VPW_2    1027UL

	#define PROTO_ID_ISO15031_J1850VPW      13UL
	#define PROTO_ID_ISOOB_J1850            14UL
	#define PROTO_ID_J2190_J1850VPW         37UL

	#define BUSTYPE_J1850_VPW               11UL
	#define BUSTYPE_J1850_VPW_BAUD          10400UL

	/* IoCtl IDs (from MDI2.mdi) */
	#define PDU_IOCTL_RESET                 1UL
	#define PDU_IOCTL_CLEAR_TX_QUEUE        2UL
	#define PDU_IOCTL_SUSPEND_TX_QUEUE      3UL
	#define PDU_IOCTL_RESUME_TX_QUEUE       4UL
	#define PDU_IOCTL_CLEAR_RX_QUEUE        5UL
	#define PDU_IOCTL_READ_VBATT            6UL
	#define PDU_IOCTL_SET_PROG_VOLTAGE      7UL
	#define PDU_IOCTL_READ_PROG_VOLTAGE     8UL
	#define PDU_IOCTL_GENERIC               9UL
	#define PDU_IOCTL_SET_BUFFER_SIZE       10UL
	#define PDU_IOCTL_GET_CABLE_ID          11UL
	#define PDU_IOCTL_START_MSG_FILTER      12UL
	#define PDU_IOCTL_STOP_MSG_FILTER       13UL
	#define PDU_IOCTL_CLEAR_MSG_FILTER      14UL
	#define PDU_IOCTL_SET_EVENT_QUEUE_PROPS 15UL
	#define PDU_IOCTL_SEND_BREAK            16UL
	#define PDU_IOCTL_READ_IGNITION_SENSE   17UL
	#define PDU_IOCTL_CLEAR_TX_PENDING      18UL

	/* =========================================================================
	* J2534 types (minimal — enough for smj2534.dll)
	* ====================================================================== */
	#define J2534_J1850VPW                  0x01UL
	#define J2534_PASS_FILTER               0x01UL
	#define J2534_STATUS_NOERROR            0x00L
	#define J2534_ERR_BUFFER_EMPTY          0x10L
	#define J2534_MAX_DATA_SIZE             4128

	/* J2534 IoCtl IDs */
	#define IOCTL_CLEAR_TX_BUFFER           7UL
	#define IOCTL_CLEAR_RX_BUFFER           8UL
	#define IOCTL_CLEAR_MSG_FILTERS         9UL
	#define IOCTL_READ_VBATT                12UL
	#define IOCTL_READ_PROG_VOLTAGE         13UL
	#define IOCTL_SET_CONFIG                5UL
	#define IOCTL_GET_CONFIG                6UL

	typedef struct {
		unsigned long ProtocolID;
		unsigned long RxStatus;
		unsigned long TxFlags;
		unsigned long Timestamp;
		unsigned long DataSize;
		unsigned long ExtraDataIndex;
		unsigned char Data[J2534_MAX_DATA_SIZE];
	} PASSTHRU_MSG;

	typedef long(*FN_PassThruOpen)          (void *pName, unsigned long *pDeviceID);
	typedef long(*FN_PassThruClose)         (unsigned long DeviceID);
	typedef long(*FN_PassThruConnect)       (unsigned long DeviceID, unsigned long ProtocolID,
		unsigned long Flags, unsigned long BaudRate,
		unsigned long *pChannelID);
	typedef long(*FN_PassThruDisconnect)    (unsigned long ChannelID);
	typedef long(*FN_PassThruReadMsgs)      (unsigned long ChannelID, PASSTHRU_MSG *pMsg,
		unsigned long *pNumMsgs, unsigned long Timeout);
	typedef long(*FN_PassThruWriteMsgs)     (unsigned long ChannelID, PASSTHRU_MSG *pMsg,
		unsigned long *pNumMsgs, unsigned long Timeout);
	typedef long(*FN_PassThruStartMsgFilter)(unsigned long ChannelID, unsigned long FilterType,
		PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
		PASSTHRU_MSG *pFlowCtrl, unsigned long *pMsgID);
	typedef long(*FN_PassThruStopMsgFilter) (unsigned long ChannelID, unsigned long MsgID);
	typedef long(*FN_PassThruIoctl)         (unsigned long ChannelID, unsigned long IoctlID,
		void *pInput, void *pOutput);
	typedef long(*FN_PassThruGetLastError)  (char *pErrorDescription);

	/* =========================================================================
	* PDU public types
	* ====================================================================== */
	typedef uint32_t T_PDU_UINT32;
	typedef uint8_t  T_PDU_UINT8;

	typedef struct {
		T_PDU_UINT32 hMod;
		T_PDU_UINT32 ModuleTypeId;
		T_PDU_UINT32 Status;
		char         VendorModuleName[64];
		char         VendorAdditionalInfo[128];
	} PDU_MODULE_ITEM;

	typedef struct {
		T_PDU_UINT32 BusTypeId;
		T_PDU_UINT32 ProtocolId;
		T_PDU_UINT32 NumPinData;
		void        *pPinData;
	} PDU_RSC_DATA;

	typedef struct {
		T_PDU_UINT32 ResourceId;
		T_PDU_UINT32 ResourceStatus;
	} PDU_RSC_STATUS_DATA;

	typedef struct {
		T_PDU_UINT32 ParamId;
		T_PDU_UINT32 ParamType;
		union {
			T_PDU_UINT32 ParamValueUint32;
			struct { T_PDU_UINT32 NumByteValues; T_PDU_UINT8 *pByteValues; } Bytes;
		} ParamValue;
	} PDU_COM_PARAM;

	typedef struct {
		char MVCI_Part2StandardVersion[64];
		char Vendor[64];
		char HWVersion[64];
		char FWVersion[64];
		char SWVersion[64];
	} PDU_VERSION_DATA;

	typedef struct {
		T_PDU_UINT32 NumFlagBytes;
		T_PDU_UINT8 *pFlagData;
	} PDU_FLAG_DATA;

	typedef struct {
		T_PDU_UINT32 NumEntries;
		void        *pEntries;
	} PDU_UNIQUE_RESP_ID_TABLE;

	typedef struct {
		T_PDU_UINT32 NumItems;
		void        *pItems;
	} PDU_IOCTL_DATA;

	/* Result data payload inside an event item */
	typedef struct {
		T_PDU_UINT32 RxStatus;
		T_PDU_UINT32 TxMsgFlags;
		T_PDU_UINT32 Timestamp;
		T_PDU_UINT32 DataSize;
		T_PDU_UINT32 ExtraDataIndex;
		T_PDU_UINT8  Data[J2534_MAX_DATA_SIZE];
	} PDU_RESULT_DATA;

	typedef struct PDU_EVENT_ITEM {
		T_PDU_UINT32          EventType;
		T_PDU_UINT32          hConn;
		T_PDU_UINT32          hCoPrimitive;
		T_PDU_UINT32          Timestamp;
		void                 *pData;            /* PDU_RESULT_DATA* */
		struct PDU_EVENT_ITEM *pNext;
	} PDU_EVENT_ITEM;

	typedef struct PDU_ITEM {
		T_PDU_UINT32  ItemType;
		void         *pData;
		struct PDU_ITEM *pNext;
	} PDU_ITEM;

	typedef void(*T_PDU_CALLBACK)(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn,
		PDU_EVENT_ITEM *pEventItem, void *pUserData);

	/* =========================================================================
	* Event queue — lock-free ring buffer of PDU_EVENT_ITEM*
	* ====================================================================== */
	#define EVT_QUEUE_SIZE  128     /* must be power of 2 */
	#define EVT_QUEUE_MASK  (EVT_QUEUE_SIZE - 1)

	typedef struct {
		PDU_EVENT_ITEM * volatile slots[EVT_QUEUE_SIZE];
		volatile LONG head;         /* consumer reads here */
		volatile LONG tail;         /* producer writes here */
		CRITICAL_SECTION lock;      /* simple mutex — good enough at this rate */
	} EventQueue;

	static void evq_init(EventQueue *q) {
		memset(q->slots, 0, sizeof(q->slots));
		q->head = q->tail = 0;
		InitializeCriticalSection(&q->lock);
	}

	static void evq_destroy(EventQueue *q) {
		EnterCriticalSection(&q->lock);
		while (q->head != q->tail) {
			PDU_EVENT_ITEM *ev = q->slots[q->head & EVT_QUEUE_MASK];
			if (ev) { free(ev->pData); free(ev); }
			q->head++;
		}
		LeaveCriticalSection(&q->lock);
		DeleteCriticalSection(&q->lock);
	}

	/* Returns 1 on success, 0 if full (frame dropped) */
	static int evq_push(EventQueue *q, PDU_EVENT_ITEM *ev) {
		int ok = 0;
		EnterCriticalSection(&q->lock);
		if ((q->tail - q->head) < EVT_QUEUE_SIZE) {
			q->slots[q->tail & EVT_QUEUE_MASK] = ev;
			q->tail++;
			ok = 1;
		}
		LeaveCriticalSection(&q->lock);
		return ok;
	}

	/* Returns NULL if empty */
	static PDU_EVENT_ITEM *evq_pop(EventQueue *q) {
		PDU_EVENT_ITEM *ev = NULL;
		EnterCriticalSection(&q->lock);
		if (q->head != q->tail) {
			ev = q->slots[q->head & EVT_QUEUE_MASK];
			q->slots[q->head & EVT_QUEUE_MASK] = NULL;
			q->head++;
		}
		LeaveCriticalSection(&q->lock);
		return ev;
	}

	/* Drain all — returns linked list */
	static PDU_EVENT_ITEM *evq_drain(EventQueue *q) {
		PDU_EVENT_ITEM *head = NULL, *tail = NULL;
		PDU_EVENT_ITEM *ev;
		while ((ev = evq_pop(q)) != NULL) {
			if (!head) head = ev; else tail->pNext = ev;
			tail = ev;
		}
		return head;
	}

	/* =========================================================================
	* Per-connection state
	* ====================================================================== */
	#define MAX_CONNECTIONS  8

	typedef struct {
		int           InUse;
		T_PDU_UINT32  ResourceId;
		T_PDU_UINT32  ModHandle;
		unsigned long J2534ChannelID;
		unsigned long J2534FilterID;
		int           ChannelOpen;      /* CreateComLogicalLink done */
		int           Connected;        /* PDUConnect (filter installed) done */
		EventQueue    EvtQ;

		/* RX background thread */
		HANDLE        hRxThread;
		volatile int  RxThreadRun;      /* set to 0 to stop thread */
	} PDU_CONN_STATE;

	/* =========================================================================
	* Global DLL state
	* ====================================================================== */
	static struct {
		int              Constructed;
		HMODULE          hJ2534Dll;
		unsigned long    J2534DeviceID;
		int              DeviceOpen;

		/* smj2534.dll function pointers */
		FN_PassThruOpen             pfOpen;
		FN_PassThruClose            pfClose;
		FN_PassThruConnect          pfConnect;
		FN_PassThruDisconnect       pfDisconnect;
		FN_PassThruReadMsgs         pfReadMsgs;
		FN_PassThruWriteMsgs        pfWriteMsgs;
		FN_PassThruStartMsgFilter   pfStartFilter;
		FN_PassThruStopMsgFilter    pfStopFilter;
		FN_PassThruIoctl            pfIoctl;
		FN_PassThruGetLastError     pfGetLastError;

		PDU_CONN_STATE  Connections[MAX_CONNECTIONS];
		T_PDU_CALLBACK  EventCallback;
		void           *pCallbackUserData;

		char            MdiPath[MAX_PATH];
		char            DllPath[MAX_PATH];   /* our own DLL path, for registry */

		CRITICAL_SECTION GlobalLock;
	} g;

	/* =========================================================================
	* Resource table — verbatim from MDI2.mdi
	* ====================================================================== */
	static const struct {
		T_PDU_UINT32 ResourceId;
		T_PDU_UINT32 ProtocolId;
		T_PDU_UINT32 BusTypeId;
		const char  *ShortName;
	} g_Resources[] = {
		{ RSC_ID_J2190_ON_J1850VPW_2,   PROTO_ID_J2190_J1850VPW,    BUSTYPE_J1850_VPW, "RSC_ID_J2190_ON_J1850VPW_2" },
		{ RSC_ID_OBD2_ON_J1850VPW_2,    PROTO_ID_ISO15031_J1850VPW, BUSTYPE_J1850_VPW, "RSC_ID_OBD2_ON_J1850VPW_2" },
		{ RSC_ID_OBD2DSC_ON_J1850VPW_2, PROTO_ID_ISOOB_J1850,        BUSTYPE_J1850_VPW, "RSC_ID_OBD2DSC_ON_J1850VPW_2" },
	};
	#define NUM_RESOURCES  3

	/* =========================================================================
	* Background RX thread
	* Runs per connection after PDUConnect; reads frames from SM2 via
	* PassThruReadMsgs and pushes PDU_EVENT_ITEMs into the connection's queue.
	* ====================================================================== */
	static unsigned __stdcall RxThreadProc(void *pArg)
	{
		PDU_CONN_STATE *c = (PDU_CONN_STATE *)pArg;

		while (c->RxThreadRun) {

			PASSTHRU_MSG msg = { 0 };
			unsigned long numMsgs = 1;

			long ret = g.pfReadMsgs(c->J2534ChannelID, &msg, &numMsgs, 10 /* 10ms block */);

			if (ret == J2534_STATUS_NOERROR && numMsgs > 0) {

				/* ----------------------------------------------------------
				* Convert J2534 frame ? PDU_EVENT_ITEM
				* -------------------------------------------------------- */
				PDU_RESULT_DATA *res = (PDU_RESULT_DATA *)calloc(1, sizeof(PDU_RESULT_DATA));
				PDU_EVENT_ITEM  *ev = (PDU_EVENT_ITEM  *)calloc(1, sizeof(PDU_EVENT_ITEM));

				if (!res || !ev) { free(res); free(ev); continue; }

				/* Copy J2534 fields into PDU result */
				res->RxStatus = msg.RxStatus;
				res->TxMsgFlags = msg.TxFlags;
				res->Timestamp = msg.Timestamp;
				res->DataSize = (msg.DataSize <= J2534_MAX_DATA_SIZE) ? msg.DataSize : J2534_MAX_DATA_SIZE;
				res->ExtraDataIndex = msg.ExtraDataIndex;
				if (res->DataSize > 0)
					memcpy(res->Data, msg.Data, res->DataSize);

				/* Build event item */
				ev->EventType = PDU_EVT_DATA_RECEIVED;
				ev->hConn = (T_PDU_UINT32)(c - g.Connections);   /* index = handle */
				ev->hCoPrimitive = PDU_HANDLE_UNDEF;
				ev->Timestamp = msg.Timestamp ? msg.Timestamp : GetTickCount();
				ev->pData = res;
				ev->pNext = NULL;

				/* Push onto the connection's event queue */
				if (!evq_push(&c->EvtQ, ev)) {
					/* Queue full — drop frame */
					free(res);
					free(ev);
				}
				else if (g.EventCallback) {
					/* Fire callback immediately if registered */
					g.EventCallback(0, ev->hConn, ev, g.pCallbackUserData);
				}
			}
			/* J2534_ERR_BUFFER_EMPTY is normal — just loop */
		}

		return 0;
	}

	static void StartRxThread(PDU_CONN_STATE *c)
	{
		c->RxThreadRun = 1;
		c->hRxThread = (HANDLE)_beginthreadex(NULL, 0, RxThreadProc, c, 0, NULL);
	}

	static void StopRxThread(PDU_CONN_STATE *c)
	{
		if (c->hRxThread) {
			c->RxThreadRun = 0;
			WaitForSingleObject(c->hRxThread, 2000);
			CloseHandle(c->hRxThread);
			c->hRxThread = NULL;
		}
	}

	/* =========================================================================
	* smj2534.dll loader
	* ====================================================================== */
	static int LoadJ2534(void)
	{
		g.hJ2534Dll = LoadLibraryA("smj2534.dll");
		if (!g.hJ2534Dll) return 0;

	#define GP(name, type, field) \
		g.field = (type)GetProcAddress(g.hJ2534Dll, name); \
		if (!g.field) { FreeLibrary(g.hJ2534Dll); g.hJ2534Dll = NULL; return 0; }

		GP("PassThruOpen", FN_PassThruOpen, pfOpen)
			GP("PassThruClose", FN_PassThruClose, pfClose)
			GP("PassThruConnect", FN_PassThruConnect, pfConnect)
			GP("PassThruDisconnect", FN_PassThruDisconnect, pfDisconnect)
			GP("PassThruReadMsgs", FN_PassThruReadMsgs, pfReadMsgs)
			GP("PassThruWriteMsgs", FN_PassThruWriteMsgs, pfWriteMsgs)
			GP("PassThruStartMsgFilter", FN_PassThruStartMsgFilter, pfStartFilter)
			GP("PassThruStopMsgFilter", FN_PassThruStopMsgFilter, pfStopFilter)
			GP("PassThruIoctl", FN_PassThruIoctl, pfIoctl)
			GP("PassThruGetLastError", FN_PassThruGetLastError, pfGetLastError)
	#undef GP
			return 1;
	}

	/* =========================================================================
	* Connection helpers
	* ====================================================================== */
	static int AllocConn(T_PDU_UINT32 *phConn)
	{
		for (int i = 0; i < MAX_CONNECTIONS; i++) {
			if (!g.Connections[i].InUse) {
				memset(&g.Connections[i], 0, sizeof(PDU_CONN_STATE));
				evq_init(&g.Connections[i].EvtQ);
				g.Connections[i].InUse = 1;
				g.Connections[i].J2534ChannelID = (unsigned long)-1;
				g.Connections[i].J2534FilterID = (unsigned long)-1;
				*phConn = (T_PDU_UINT32)i;
				return 1;
			}
		}
		return 0;
	}

	static PDU_CONN_STATE *GetConn(T_PDU_UINT32 hConn)
	{
		if (hConn >= MAX_CONNECTIONS)          return NULL;
		if (!g.Connections[hConn].InUse)       return NULL;
		return &g.Connections[hConn];
	}

	static void FreeConn(T_PDU_UINT32 hConn)
	{
		if (hConn >= MAX_CONNECTIONS) return;
		PDU_CONN_STATE *c = &g.Connections[hConn];
		StopRxThread(c);
		evq_destroy(&c->EvtQ);
		memset(c, 0, sizeof(PDU_CONN_STATE));
	}

	/* =========================================================================
	* Registry helpers — Tech2Win discovers DPDUA DLLs via:
	*   HKLM\\SOFTWARE\\D-PDU API\\<VendorName>\\<ModuleName>
	*     VCI_MODULE_DESCRIPTION_FILE  = <path to MDI2.mdi>
	*     MVCI_LIBRARY_FILE            = <path to pdu_mdi2.dll>
	*     SUPPLIER_NAME                = Bosch
	*     SHORT_NAME                   = MDI2
	* ====================================================================== */
	#define REG_DPDUA_ROOT   "SOFTWARE\\\\D-PDU API"
	#define REG_VENDOR       "Bosch"
	#define REG_MODULE       "MDI2"

	static void WriteRegString(HKEY hKey, const char *name, const char *value)
	{
		RegSetValueExA(hKey, name, 0, REG_SZ, (const BYTE*)value, (DWORD)(strlen(value) + 1));
	}

	static int RegisterDll(void)
	{
		char keyPath[256];
		snprintf(keyPath, sizeof(keyPath), "%s\\\\%s\\\\%s", REG_DPDUA_ROOT, REG_VENDOR, REG_MODULE);

		HKEY hKey;
		LONG res = RegCreateKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
		if (res != ERROR_SUCCESS) {
			/* Try HKCU as fallback (no admin required) */
			res = RegCreateKeyExA(HKEY_CURRENT_USER, keyPath, 0, NULL,
				REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
			if (res != ERROR_SUCCESS) return 0;
		}

		/* Build MDI path: same dir as DLL, filename MDI2.mdi */
		char mdiPath[MAX_PATH];
		strncpy(mdiPath, g.DllPath, MAX_PATH - 1);
		char *slash = strrchr(mdiPath, '\\');
		if (slash) strcpy(slash + 1, "MDI2.mdi");

		WriteRegString(hKey, "VCI_MODULE_DESCRIPTION_FILE", mdiPath);
		WriteRegString(hKey, "MVCI_LIBRARY_FILE", g.DllPath);
		WriteRegString(hKey, "SUPPLIER_NAME", "Bosch");
		WriteRegString(hKey, "SHORT_NAME", "MODULE_TYPE_ID_MDI_2");

		RegCloseKey(hKey);
		return 1;
	}

	static int UnregisterDll(void)
	{
		char keyPath[256];
		snprintf(keyPath, sizeof(keyPath), "%s\\\\%s\\\\%s", REG_DPDUA_ROOT, REG_VENDOR, REG_MODULE);
		RegDeleteKeyA(HKEY_LOCAL_MACHINE, keyPath);
		RegDeleteKeyA(HKEY_CURRENT_USER, keyPath);
		return 1;
	}

	/* =========================================================================
	* DllMain
	* ====================================================================== */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
	{
		switch (reason) {

		case DLL_PROCESS_ATTACH:
			// ? ADD THIS
			logmsg("DllMain: PROCESS_ATTACH");

			// ? KEEP YOUR EXISTING CODE
			InitializeCriticalSection(&g.GlobalLock);
			GetModuleFileNameA((HMODULE)hInst, g.DllPath, MAX_PATH);
			break;

		case DLL_PROCESS_DETACH:
			// ? ADD THIS
			logmsg("DllMain: PROCESS_DETACH");

			// ? KEEP YOUR EXISTING CODE
			DeleteCriticalSection(&g.GlobalLock);
			break;
		}

		return TRUE;
	}12

	/* =========================================================================
	* Registration entry points — call once from an installer / elevated cmd:
	*   rundll32 pdu_mdi2.dll,DllRegisterServer
	*   rundll32 pdu_mdi2.dll,DllUnregisterServer
	* ====================================================================== */
	PDU_API HRESULT __stdcall DllRegisterServer(void)
	{
		return RegisterDll() ? S_OK : E_FAIL;
	}

	PDU_API HRESULT __stdcall DllUnregisterServer(void)
	{
		return UnregisterDll() ? S_OK : E_FAIL;
	}

	/* =========================================================================
	* PDUConstruct — initialize, load MDI, load smj2534.dll
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUConstruct(void)
	{
		logmsg("PDUConstruct: ENTER");

		EnterCriticalSection(&g.GlobalLock);

		if (g.Constructed) {
			logmsg("PDUConstruct: already constructed, returning OK");
			LeaveCriticalSection(&g.GlobalLock);
			return PDU_STATUS_NOERROR;
		}

		logmsg("PDUConstruct: initializing globals");
		memset(&g.Connections, 0, sizeof(g.Connections));
		g.EventCallback = NULL;
		g.pCallbackUserData = NULL;
		g.DeviceOpen = 0;
		g.hJ2534Dll = NULL;

		// Build MDI path based on DLL location
		logmsg("PDUConstruct: g.DllPath = %s", g.DllPath);
		strncpy_s(g.MdiPath, MAX_PATH, g.DllPath, _TRUNCATE);
		logmsg("PDUConstruct: after strncpy_s, g.MdiPath = %s", g.MdiPath);

		char *slash = strrchr(g.MdiPath, '\\');
		logmsg("PDUConstruct: slash pointer = %p", slash);

		if (slash) {
			logmsg("PDUConstruct: replacing filename with MDI2.mdi");
			strcpy_s(slash + 1, MAX_PATH - (slash - g.MdiPath) - 1, "MDI2.mdi");
		}
		else {
			logmsg("PDUConstruct: no slash found, using default MDI2.mdi");
			strcpy_s(g.MdiPath, MAX_PATH, "MDI2.mdi");
		}

		logmsg("PDUConstruct: final MDI path = %s", g.MdiPath);

		// Load J2534 DLL safely
		logmsg("PDUConstruct: calling LoadJ2534()");
		if (!LoadJ2534()) {
			logmsg("PDUConstruct: LoadJ2534 FAILED");
			LeaveCriticalSection(&g.GlobalLock);
			return PDU_ERR_FCT_FAILED;
		}

		logmsg("PDUConstruct: LoadJ2534 succeeded");

		g.Constructed = 1;
		logmsg("PDUConstruct: construction complete");

		LeaveCriticalSection(&g.GlobalLock);
		logmsg("PDUConstruct: EXIT (OK)");

		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUDestruct
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUDestruct(void)
	{
		EnterCriticalSection(&g.GlobalLock);

		for (int i = 0; i < MAX_CONNECTIONS; i++) {
			if (g.Connections[i].InUse) {
				PDU_CONN_STATE *c = &g.Connections[i];
				StopRxThread(c);
				if (c->J2534FilterID != (unsigned long)-1) g.pfStopFilter(c->J2534ChannelID, c->J2534FilterID);
				if (c->J2534ChannelID != (unsigned long)-1) g.pfDisconnect(c->J2534ChannelID);
				evq_destroy(&c->EvtQ);
			}
		}

		if (g.DeviceOpen && g.pfClose) g.pfClose(g.J2534DeviceID);
		if (g.hJ2534Dll)               FreeLibrary(g.hJ2534Dll);

		char savedDllPath[MAX_PATH];
		strncpy(savedDllPath, g.DllPath, MAX_PATH - 1);
		memset(&g, 0, sizeof(g));
		strncpy(g.DllPath, savedDllPath, MAX_PATH - 1);
		InitializeCriticalSection(&g.GlobalLock);  /* re-init after memset */

		LeaveCriticalSection(&g.GlobalLock);
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetVersion
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetVersion(T_PDU_UINT32 hMod, PDU_VERSION_DATA *pVer)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!pVer)          return PDU_ERR_INVALID_PARAMETERS;
		strncpy(pVer->MVCI_Part2StandardVersion, "2.2.0", 63);
		strncpy(pVer->Vendor, "Bosch", 63);
		strncpy(pVer->HWVersion, "MDI2", 63);
		strncpy(pVer->FWVersion, "1.0.0", 63);
		strncpy(pVer->SWVersion, "pdu_mdi2/1.0", 63);
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetModuleIds — module type 28
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetModuleIds(PDU_MODULE_ITEM **ppModuleTable)
	{
		if (!g.Constructed)  return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!ppModuleTable)  return PDU_ERR_INVALID_PARAMETERS;

		PDU_MODULE_ITEM *tbl = (PDU_MODULE_ITEM*)calloc(2, sizeof(PDU_MODULE_ITEM));
		if (!tbl) return PDU_ERR_FCT_FAILED;

		tbl[0].hMod = 0;
		tbl[0].ModuleTypeId = MDI2_MODULE_TYPE_ID;   /* 28 */
		tbl[0].Status = g.DeviceOpen ? 1 : 0;
		strncpy(tbl[0].VendorModuleName, "MDI2", 63);
		strncpy(tbl[0].VendorAdditionalInfo, "GM MDI 2 by Bosch", 127);

		tbl[1].hMod = PDU_HANDLE_UNDEF;       /* sentinel */
		tbl[1].ModuleTypeId = PDU_ID_UNDEF;

		*ppModuleTable = tbl;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUModuleConnect — PassThruOpen to SM2
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUModuleConnect(T_PDU_UINT32 hMod)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (g.DeviceOpen)   return PDU_STATUS_NOERROR;

		long ret = g.pfOpen(NULL, &g.J2534DeviceID);
		if (ret != J2534_STATUS_NOERROR) return PDU_ERR_MODULE_NOT_CONNECTED;

		g.DeviceOpen = 1;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUModuleDisconnect
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUModuleDisconnect(T_PDU_UINT32 hMod)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!g.DeviceOpen)  return PDU_STATUS_NOERROR;
		g.pfClose(g.J2534DeviceID);
		g.DeviceOpen = 0;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetResourceIds — returns 395, 1005, 1027
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetResourceIds(T_PDU_UINT32   hMod,
		PDU_RSC_DATA  *pRscData,
		T_PDU_UINT32 **ppList)
	{
		UNUSED(hMod); UNUSED(pRscData);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!ppList)         return PDU_ERR_INVALID_PARAMETERS;

		/* Format: [count, id0, id1, id2, PDU_ID_UNDEF] */
		T_PDU_UINT32 *list = (T_PDU_UINT32*)malloc((NUM_RESOURCES + 2) * sizeof(T_PDU_UINT32));
		if (!list) return PDU_ERR_FCT_FAILED;

		list[0] = NUM_RESOURCES;
		for (int i = 0; i < NUM_RESOURCES; i++) list[i + 1] = g_Resources[i].ResourceId;
		list[NUM_RESOURCES + 1] = PDU_ID_UNDEF;

		*ppList = list;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetObjectId — short name ? numeric ID
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetObjectId(T_PDU_UINT32  type,
		const char   *pShortName,
		T_PDU_UINT32 *pId)
	{
		if (!g.Constructed)       return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!pShortName || !pId)  return PDU_ERR_INVALID_PARAMETERS;

		if (type == PDU_OBJT_MODULEITEM && strcmp(pShortName, "MODULE_TYPE_ID_MDI_2") == 0)
		{
			*pId = MDI2_MODULE_TYPE_ID; return PDU_STATUS_NOERROR;
		}

		if (type == PDU_OBJT_BUSTYPE && strcmp(pShortName, "SAE_J1850_VPW") == 0)
		{
			*pId = BUSTYPE_J1850_VPW; return PDU_STATUS_NOERROR;
		}

		if (type == PDU_OBJT_RESOURCE) {
			for (int i = 0; i < NUM_RESOURCES; i++)
				if (strcmp(pShortName, g_Resources[i].ShortName) == 0)
				{
					*pId = g_Resources[i].ResourceId; return PDU_STATUS_NOERROR;
				}
		}

		if (type == PDU_OBJT_PROTOCOL) {
			if (strstr(pShortName, "J2190")) { *pId = PROTO_ID_J2190_J1850VPW;    return PDU_STATUS_NOERROR; }
			if (strstr(pShortName, "15031")) { *pId = PROTO_ID_ISO15031_J1850VPW; return PDU_STATUS_NOERROR; }
			if (strstr(pShortName, "OBD")) { *pId = PROTO_ID_ISOOB_J1850;       return PDU_STATUS_NOERROR; }
		}

		*pId = PDU_ID_UNDEF;
		return PDU_ERR_ITEM_NOT_FOUND;
	}

	/* =========================================================================
	* PDUCreateComLogicalLink — PassThruConnect J1850VPW @ 10400 baud
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUCreateComLogicalLink(T_PDU_UINT32   hMod,
		PDU_RSC_DATA  *pRscData,
		T_PDU_UINT32   resourceId,
		T_PDU_UINT32   hCll,
		PDU_FLAG_DATA *pFlag,
		T_PDU_UINT32  *pHConn)
	{
		UNUSED(hMod); UNUSED(hCll); UNUSED(pFlag);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!g.DeviceOpen)  return PDU_ERR_MODULE_NOT_CONNECTED;
		if (!pHConn)        return PDU_ERR_INVALID_PARAMETERS;

		/* Accept any of our 3 VPW resources, or a RSC_DATA with J1850 bustype */
		int valid = 0;
		for (int i = 0; i < NUM_RESOURCES; i++)
			if (g_Resources[i].ResourceId == resourceId) { valid = 1; break; }
		if (!valid && pRscData && pRscData->BusTypeId == BUSTYPE_J1850_VPW) valid = 1;
		if (!valid) return PDU_ERR_INVALID_PARAMETERS;

		T_PDU_UINT32 hConn;
		if (!AllocConn(&hConn)) return PDU_ERR_FCT_FAILED;

		PDU_CONN_STATE *c = &g.Connections[hConn];
		c->ResourceId = resourceId;
		c->ModHandle = hMod;

		unsigned long chanID = 0;
		long ret = g.pfConnect(g.J2534DeviceID, J2534_J1850VPW, 0, BUSTYPE_J1850_VPW_BAUD, &chanID);
		if (ret != J2534_STATUS_NOERROR) {
			FreeConn(hConn);
			return PDU_ERR_FCT_FAILED;
		}

		c->J2534ChannelID = chanID;
		c->ChannelOpen = 1;
		*pHConn = hConn;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUDestroyComLogicalLink
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUDestroyComLogicalLink(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		PDU_CONN_STATE *c = GetConn(hConn);
		if (!c) return PDU_ERR_INVALID_PARAMETERS;

		StopRxThread(c);

		if (c->J2534FilterID != (unsigned long)-1) {
			g.pfStopFilter(c->J2534ChannelID, c->J2534FilterID);
			c->J2534FilterID = (unsigned long)-1;
		}
		if (c->J2534ChannelID != (unsigned long)-1) {
			g.pfDisconnect(c->J2534ChannelID);
			c->J2534ChannelID = (unsigned long)-1;
		}

		FreeConn(hConn);
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUConnect — install pass-all filter, start RX thread
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUConnect(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		PDU_CONN_STATE *c = GetConn(hConn);
		if (!c || !c->ChannelOpen) return PDU_ERR_MODULE_NOT_CONNECTED;
		if (c->Connected)          return PDU_STATUS_NOERROR;

		/* Pass-all filter: zero-length mask and pattern */
		PASSTHRU_MSG mask = { 0 };
		PASSTHRU_MSG pattern = { 0 };
		mask.ProtocolID = pattern.ProtocolID = J2534_J1850VPW;

		unsigned long filterID = 0;
		long ret = g.pfStartFilter(c->J2534ChannelID, J2534_PASS_FILTER,
			&mask, &pattern, NULL, &filterID);
		if (ret != J2534_STATUS_NOERROR) return PDU_ERR_FCT_FAILED;

		c->J2534FilterID = filterID;
		c->Connected = 1;

		/* Kick off background RX thread — it will feed PDUGetEventItem */
		StartRxThread(c);

		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUDisconnect — stop RX thread, remove filter
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUDisconnect(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		PDU_CONN_STATE *c = GetConn(hConn);
		if (!c) return PDU_ERR_INVALID_PARAMETERS;

		StopRxThread(c);

		if (c->J2534FilterID != (unsigned long)-1) {
			g.pfStopFilter(c->J2534ChannelID, c->J2534FilterID);
			c->J2534FilterID = (unsigned long)-1;
		}
		c->Connected = 0;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUStartComPrimitive
	* Tech2Win ? PDUStartComPrimitive ? your DLL ? PassThruWriteMsgs ? SM2
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUStartComPrimitive(T_PDU_UINT32  hMod,
		T_PDU_UINT32  hConn,
		T_PDU_UINT32  CoPrimitiveType,
		T_PDU_UINT32  CoPrimitiveDataSize,
		T_PDU_UINT8  *pCoPrimitiveData,
		PDU_ITEM     *pCopTag,
		T_PDU_UINT32 *pHCoPrimitive)
	{
		UNUSED(hMod); UNUSED(CoPrimitiveType); UNUSED(pCopTag);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

		PDU_CONN_STATE *c = GetConn(hConn);
		if (!c || !c->Connected) return PDU_ERR_MODULE_NOT_CONNECTED;

		if (pHCoPrimitive) *pHCoPrimitive = PDU_HANDLE_UNDEF;

		/* Receive-only primitive — nothing to transmit */
		if (!pCoPrimitiveData || CoPrimitiveDataSize == 0)
			return PDU_STATUS_NOERROR;

		if (CoPrimitiveDataSize > J2534_MAX_DATA_SIZE)
			return PDU_ERR_INVALID_PARAMETERS;

		/* ------------------------------------------------------------------
		* Convert PDU frame ? J2534 frame, forward to SM2 via PassThruWriteMsgs
		* ---------------------------------------------------------------- */
		PASSTHRU_MSG msg = { 0 };
		msg.ProtocolID = J2534_J1850VPW;
		msg.TxFlags = 0;
		msg.DataSize = CoPrimitiveDataSize;
		memcpy(msg.Data, pCoPrimitiveData, CoPrimitiveDataSize);

		unsigned long numMsgs = 1;
		long ret = g.pfWriteMsgs(c->J2534ChannelID, &msg, &numMsgs, 500);
		if (ret != J2534_STATUS_NOERROR) return PDU_ERR_FCT_FAILED;

		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUCancelComPrimitive
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUCancelComPrimitive(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, T_PDU_UINT32 hCoPrimitive)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(hCoPrimitive);
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetEventItem
	* Tech2Win calls this ? DLL pops one item from queue populated by RX thread
	*
	* SM2 ? PassThruReadMsgs ? RxThread ? evq_push ? PDUGetEventItem ? Tech2Win
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetEventItem(T_PDU_UINT32    hMod,
		T_PDU_UINT32    hConn,
		PDU_EVENT_ITEM **ppEventItem)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!ppEventItem)   return PDU_ERR_INVALID_PARAMETERS;

		PDU_CONN_STATE *c = GetConn(hConn);
		if (!c || !c->Connected) return PDU_ERR_MODULE_NOT_CONNECTED;

		*ppEventItem = evq_pop(&c->EvtQ);   /* NULL = no data yet, not an error */
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetEventItems — drain entire queue into linked list
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetEventItems(T_PDU_UINT32    hMod,
		T_PDU_UINT32    hConn,
		PDU_EVENT_ITEM **ppEventItem)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
		if (!ppEventItem)   return PDU_ERR_INVALID_PARAMETERS;

		PDU_CONN_STATE *c = GetConn(hConn);
		if (!c || !c->Connected) return PDU_ERR_MODULE_NOT_CONNECTED;

		*ppEventItem = evq_drain(&c->EvtQ);
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDURegisterEventCallback
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDURegisterEventCallback(T_PDU_UINT32   hMod,
		T_PDU_UINT32   hConn,
		T_PDU_CALLBACK cb,
		void          *pUserData)
	{
		UNUSED(hMod); UNUSED(hConn);
		g.EventCallback = cb;
		g.pCallbackUserData = pUserData;
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUIoCtl — maps PDU IoCtl IDs 1-18 to J2534 ioctl calls
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUIoCtl(T_PDU_UINT32    hMod,
		T_PDU_UINT32    hConn,
		T_PDU_UINT32    IoCtlCommandId,
		PDU_IOCTL_DATA *pInputData,
		PDU_IOCTL_DATA **ppOutputData)
	{
		UNUSED(hMod);
		if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

		PDU_CONN_STATE *c = GetConn(hConn);
		unsigned long chanID = (c && c->J2534ChannelID != (unsigned long)-1) ? c->J2534ChannelID : 0;
		if (ppOutputData) *ppOutputData = NULL;

		struct { unsigned long Parameter; long Value; } sLong = { 0 };

		switch (IoCtlCommandId) {
		case PDU_IOCTL_RESET:
			if (!chanID) return PDU_ERR_MODULE_NOT_CONNECTED;
			g.pfIoctl(chanID, IOCTL_CLEAR_TX_BUFFER, NULL, NULL);
			g.pfIoctl(chanID, IOCTL_CLEAR_RX_BUFFER, NULL, NULL);
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_CLEAR_TX_QUEUE:
		case PDU_IOCTL_CLEAR_TX_PENDING:
			if (!chanID) return PDU_ERR_MODULE_NOT_CONNECTED;
			g.pfIoctl(chanID, IOCTL_CLEAR_TX_BUFFER, NULL, NULL);
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_CLEAR_RX_QUEUE:
			if (!chanID) return PDU_ERR_MODULE_NOT_CONNECTED;
			g.pfIoctl(chanID, IOCTL_CLEAR_RX_BUFFER, NULL, NULL);
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_CLEAR_MSG_FILTER:
			if (!chanID) return PDU_ERR_MODULE_NOT_CONNECTED;
			g.pfIoctl(chanID, IOCTL_CLEAR_MSG_FILTERS, NULL, NULL);
			/* Re-install pass-all */
			if (c && c->Connected) {
				PASSTHRU_MSG mask = { 0 }, pat = { 0 };
				mask.ProtocolID = pat.ProtocolID = J2534_J1850VPW;
				unsigned long fid = 0;
				g.pfStartFilter(chanID, J2534_PASS_FILTER, &mask, &pat, NULL, &fid);
				c->J2534FilterID = fid;
			}
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_SUSPEND_TX_QUEUE:
		case PDU_IOCTL_RESUME_TX_QUEUE:
			return PDU_STATUS_NOERROR;   /* no-op on J1850 VPW */

		case PDU_IOCTL_READ_VBATT:
			g.pfIoctl(g.J2534DeviceID, IOCTL_READ_VBATT, NULL, &sLong);
			if (ppOutputData) {
				PDU_IOCTL_DATA *out = (PDU_IOCTL_DATA*)calloc(1, sizeof(PDU_IOCTL_DATA));
				T_PDU_UINT32   *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));
				if (out && v) { *v = (T_PDU_UINT32)sLong.Value; out->pItems = v; out->NumItems = 1; *ppOutputData = out; }
				else { free(out); free(v); }
			}
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_READ_PROG_VOLTAGE:
			g.pfIoctl(g.J2534DeviceID, IOCTL_READ_PROG_VOLTAGE, NULL, &sLong);
			if (ppOutputData) {
				PDU_IOCTL_DATA *out = (PDU_IOCTL_DATA*)calloc(1, sizeof(PDU_IOCTL_DATA));
				T_PDU_UINT32   *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));
				if (out && v) { *v = (T_PDU_UINT32)sLong.Value; out->pItems = v; out->NumItems = 1; *ppOutputData = out; }
				else { free(out); free(v); }
			}
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_SET_PROG_VOLTAGE:
			if (pInputData && pInputData->pItems) {
				sLong.Value = *(long*)pInputData->pItems;
				g.pfIoctl(g.J2534DeviceID, IOCTL_SET_CONFIG, &sLong, NULL);
			}
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_READ_IGNITION_SENSE:
			if (ppOutputData) {
				PDU_IOCTL_DATA *out = (PDU_IOCTL_DATA*)calloc(1, sizeof(PDU_IOCTL_DATA));
				T_PDU_UINT32   *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));
				if (out && v) { *v = 1; out->pItems = v; out->NumItems = 1; *ppOutputData = out; }
				else { free(out); free(v); }
			}
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_GET_CABLE_ID:
			if (ppOutputData) {
				PDU_IOCTL_DATA *out = (PDU_IOCTL_DATA*)calloc(1, sizeof(PDU_IOCTL_DATA));
				T_PDU_UINT32   *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));
				if (out && v) { *v = (T_PDU_UINT32)g.J2534DeviceID; out->pItems = v; out->NumItems = 1; *ppOutputData = out; }
				else { free(out); free(v); }
			}
			return PDU_STATUS_NOERROR;

		case PDU_IOCTL_START_MSG_FILTER:
		case PDU_IOCTL_STOP_MSG_FILTER:
		case PDU_IOCTL_SET_BUFFER_SIZE:
		case PDU_IOCTL_SET_EVENT_QUEUE_PROPS:
		case PDU_IOCTL_SEND_BREAK:
		case PDU_IOCTL_GENERIC:
			return PDU_STATUS_NOERROR;

		default:
			return PDU_ERR_ITEM_NOT_FOUND;
		}
	}

	/* =========================================================================
	* PDUGetLastError
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetLastError(T_PDU_UINT32  hMod, T_PDU_UINT32  hConn,
		T_PDU_UINT32 *pErrCode, T_PDU_UINT32 *pErrEvtCount,
		T_PDU_UINT32 *pErrDestHandle)
	{
		UNUSED(hMod); UNUSED(hConn);
		if (pErrCode)       *pErrCode = PDU_STATUS_NOERROR;
		if (pErrEvtCount)   *pErrEvtCount = 0;
		if (pErrDestHandle) *pErrDestHandle = PDU_HANDLE_UNDEF;
		if (g.Constructed && g.pfGetLastError) {
			char desc[512] = { 0 };
			g.pfGetLastError(desc);
			/* desc could be logged here */
		}
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetTimestamp
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetTimestamp(T_PDU_UINT32 *pTimestamp)
	{
		if (pTimestamp) *pTimestamp = GetTickCount();
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUGetStatus
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetStatus(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn,
		T_PDU_UINT32 hCoPrimitive, T_PDU_UINT32 *pStatus)
	{
		UNUSED(hMod); UNUSED(hCoPrimitive);
		if (pStatus) {
			PDU_CONN_STATE *c = GetConn(hConn);
			*pStatus = (c && c->Connected) ? 1 : 0;
		}
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* PDUDestroyItem / PDUDestroyItems — free memory returned to Tech2Win
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUDestroyItem(PDU_ITEM *pItem)
	{
		if (!pItem) return PDU_STATUS_NOERROR;
		free(pItem->pData);
		free(pItem);
		return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUDestroyItems(PDU_ITEM *pItem)
	{
		while (pItem) {
			PDU_ITEM *next = pItem->pNext;
			free(pItem->pData);
			free(pItem);
			pItem = next;
		}
		return PDU_STATUS_NOERROR;
	}

	/* =========================================================================
	* Remaining stubs
	* ====================================================================== */
	PDU_API T_PDU_UINT32 PDUGetObjectIds(T_PDU_UINT32 type, T_PDU_UINT32 hMod,
		T_PDU_UINT32 hConn, T_PDU_UINT32 **ppList)
	{
		UNUSED(type); UNUSED(hMod); UNUSED(hConn); UNUSED(ppList); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUGetResourceStatus(T_PDU_UINT32 hMod, T_PDU_UINT32 resourceId,
		PDU_RSC_STATUS_DATA *pStatus)
	{
		UNUSED(hMod);
		if (pStatus) { pStatus->ResourceId = resourceId; pStatus->ResourceStatus = g.DeviceOpen ? 1 : 0; }
		return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUGetConflictingResources(T_PDU_UINT32 hMod, T_PDU_UINT32 resourceId, void **pp)
	{
		UNUSED(hMod); UNUSED(resourceId); UNUSED(pp); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDULockResource(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, T_PDU_UINT32 mask)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(mask); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUUnlockResource(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, T_PDU_UINT32 mask)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(mask); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUGetComParam(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn,
		T_PDU_UINT32 ParamId, PDU_COM_PARAM *p)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(ParamId); UNUSED(p); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUSetComParam(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, PDU_COM_PARAM *p)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(p); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUGetUniqueRespIdTable(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, PDU_UNIQUE_RESP_ID_TABLE **pp)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(pp); return PDU_STATUS_NOERROR;
	}

	PDU_API T_PDU_UINT32 PDUSetUniqueRespIdTable(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, PDU_UNIQUE_RESP_ID_TABLE *p)
	{
		UNUSED(hMod); UNUSED(hConn); UNUSED(p); return PDU_STATUS_NOERROR;
	}