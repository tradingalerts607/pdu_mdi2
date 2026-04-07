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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <process.h>
#include <stdio.h> 
#include <stdarg.h>	
#include <errno.h>

static void logmsg(const char *fmt, ...)
{
	FILE *f = fopen("C:\\GM\\mdi2_shim.log", "a");
	if (!f) return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	DWORD tid = GetCurrentThreadId();

	fprintf(f, "[%02d:%02d:%02d.%03d] [TID %lu] ",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		(unsigned long)tid);

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fprintf(f, "\n");
	fclose(f);
}

#define LOG0(msg) \
    logmsg("%s:%d " msg, __FUNCTION__, __LINE__)

#define LOG1(fmt, ...) \
    logmsg("%s:%d " fmt, __FUNCTION__, __LINE__, __VA_ARGS__)

#define TRACE() \
    logmsg("%s:%d", __FUNCTION__, __LINE__)


/* _beginthreadex */

#define PDU_CALL __stdcall
#define PDU_API
#define UNUSED(x)   (void)(x)
#define PDU_IT_MODULE 1


/* =========================================================================
* PDU Return codes (ISO 22900-2 Table A.2)
* ====================================================================== */
#define PDU_STATUS_NOERROR              0x00000000UL //ALLCONFIRMED
#define PDU_ERR_FCT_FAILED              0x00000001UL
#define PDU_ERR_COMM_PC_TO_VCI_FAILED   0x00000011UL
#define PDU_ERR_PDUAPI_NOT_CONSTRUCTED  0x00000020UL
#define PDU_ERR_INVALID_HANDLE          0x00000060UL
#define PDU_ERR_MODULE_NOT_CONNECTED    0x000000A3UL
#define PDU_ERR_COMPARAM_NOT_SUPPORTED  0x00000063UL
#define PDU_ERR_INVALID_PARAMETERS      0x00000050UL
#define PDU_ERR_EVENT_QUEUE_EMPTY		0x00000071UL

#define PDU_HANDLE_UNDEF                0xFFFFFFFF //CONFIRMED
#define PDU_ID_UNDEF                    0xFFFFFFFE

/* PDU object types */
#define PDU_OBJT_PROTOCOL               0x8021UL
#define PDU_OBJT_BUSTYPE                0x8022UL
#define PDU_OBJT_IO_CTRL                0x8023UL
#define PDU_OBJT_COMPARAM               0x8024UL
#define PDU_OBJT_PINTYPE	            0x8025UL
#define PDU_OBJT_RESOURCE               0x8026UL

/* PDU event types */
#define PDU_EVT_DATA_AVAILABLE          0x00000801UL
#define PDU_EVT_DATA_LOST				0x0802UL
#define PDU_EVT_ERROR                   0x00000100UL
#define PDU_ERR_FAILED					0x00020000UL
#define PDU_EVT_MODULE_CONNECT			0x00000200UL
#define PDU_EVT_MODULE_DISCONNECT		0x00000400UL

/* PDU COPT TYPES - ISO 22900-2 correct values */
#define PDU_COPT_STARTCOMM				0x8001UL
#define PDU_COPT_STOPCOMM				0x8002UL
#define PDU_COPT_UPDATEPARAM			0x8003UL
#define PDU_COPT_SENDRECV				0x8004UL
#define PDU_COPT_DELAY					0x8005UL
#define PDU_COPT_RESTORE_PARAM			0x8006UL

/* PDU STATUS VALUES - ISO 22900-2 */
#define PDU_COPST_IDLE					0x8010UL
#define PDU_COPST_EXECUTING				0x8011UL
#define PDU_COPST_FINISHED				0X8012UL
#define PDU_COPST_CANCELLED				0x8013UL
#define PDU_COPST_WAITING				0x8014UL
#define PDU_CLLST_OFFLINE				0x8050UL
#define PDU_CLLST_ONLINE				0x8051UL
#define PDU_CLLST_COMM_STARTED			0x8052UL

#define CP_EcuRespSourceAddress         143UL
#define CP_FuncRespFormatPriorityType   150UL
#define CP_FuncRespTargetAddr           151UL

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

// Real BTVX status values
#define PDU_MODST_AVAIL					0x8063UL
#define PDU_MODST_READY					0x8060UL
#define PDU_MODST_NOT_READY				0x8061UL
#define PDU_MODST_NOT_AVAIL				0x8062UL

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

/* J2534 RxStatus flags */
#define TX_MSG_TYPE     0x00000001UL  /* 1 = TX echo, 0 = received from bus */
#define START_OF_MSG    0x00000002UL
#define RX_BREAK        0x00000004UL
#define TX_INDICATION   0x00000008UL

typedef struct {
	unsigned long ProtocolID;
	unsigned long RxStatus;
	unsigned long TxFlags;
	unsigned long Timestamp;
	unsigned long DataSize;
	unsigned long ExtraDataIndex;
	unsigned char Data[J2534_MAX_DATA_SIZE];
} PASSTHRU_MSG;

typedef long (PDU_CALL *FN_PassThruOpen)          (void *pName, unsigned long *pDeviceID);
typedef long (PDU_CALL *FN_PassThruClose)         (unsigned long DeviceID);
typedef long (PDU_CALL *FN_PassThruConnect)       (unsigned long DeviceID, unsigned long ProtocolID,
	unsigned long Flags, unsigned long BaudRate,
	unsigned long *pChannelID);
typedef long (PDU_CALL *FN_PassThruDisconnect)    (unsigned long ChannelID);
typedef long (PDU_CALL *FN_PassThruReadMsgs)      (unsigned long ChannelID, PASSTHRU_MSG *pMsg,
	unsigned long *pNumMsgs, unsigned long Timeout);
typedef long (PDU_CALL *FN_PassThruWriteMsgs)     (unsigned long ChannelID, PASSTHRU_MSG *pMsg,
	unsigned long *pNumMsgs, unsigned long Timeout);
typedef long (PDU_CALL *FN_PassThruStartMsgFilter)(unsigned long ChannelID, unsigned long FilterType,
	PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
	PASSTHRU_MSG *pFlowCtrl, unsigned long *pMsgID);
typedef long (PDU_CALL *FN_PassThruStopMsgFilter) (unsigned long ChannelID, unsigned long MsgID);
typedef long (PDU_CALL *FN_PassThruIoctl)         (unsigned long ChannelID, unsigned long IoctlID,
	void *pInput, void *pOutput);
typedef long (PDU_CALL *FN_PassThruGetLastError)  (char *pErrorDescription);


/* =========================================================================
* PDU public types
* ====================================================================== */
/* Base numeric types */
typedef uint32_t  UNUM32;
typedef uint8_t   UNUM8;
typedef char      CHAR8;
typedef uint32_t  T_PDU_UINT32;
typedef uint8_t   T_PDU_UINT8;
typedef int32_t   T_PDU_INT32;

/* Item type enum */
typedef enum E_PDU_IT {
	PDU_IT_IO_UNUM32 = 0x1000,
	PDU_IT_IO_PROG_VOLTAGE = 0x1001,
	PDU_IT_IO_BYTEARRAY = 0x1002,
	PDU_IT_IO_FILTER = 0x1003,
	PDU_IT_IO_EVENT_QUEUE_PROPERTY = 0x1004,
	PDU_IT_RSC_STATUS = 0x1100,
	PDU_IT_PARAM = 0x1200,
	PDU_IT_RESULT = 0x1300,
	PDU_IT_STATUS = 0x1301,
	PDU_IT_ERROR = 0x1302,
	PDU_IT_INFO = 0x1303,
	PDU_IT_RSC_ID = 0x1400,
	PDU_IT_RSC_CONFLICT = 0x1500,
	PDU_IT_MODULE_ID = 0x1600,
	PDU_IT_UNIQUE_RESP_ID_TABLE = 0x1700
} T_PDU_IT;

typedef enum E_PDU_ERROR {
	PDU_ERR_RESERVED_1 = 0x00000010, /* Reserved by ISO 22900-2 */
	PDU_ERR_SHARING_VIOLATION = 0x00000021, /* A PDUDestruct was not called before another PDUConstruct */
	PDU_ERR_RESOURCE_BUSY = 0x00000030, /* The requested resource is already in use */
	PDU_ERR_RESOURCE_TABLE_CHANGED = 0x00000031, /* Not used by the D-PDU API */
	PDU_ERR_RESOURCE_ERROR = 0x00000032, /* Not used by the D-PDU API */
	PDU_ERR_CLL_NOT_CONNECTED = 0x00000040, /* The ComLogicalLink cannot be in the PDU_CLLST_OFFLINE state */
	PDU_ERR_CLL_NOT_STARTED = 0x00000041, /* The ComLogicalLink must be in PDU_CLLST_COMM_STARTED state */
	PDU_ERR_VALUE_NOT_SUPPORTED = 0x00000061, /* One of the option values in PDUConstruct is invalid */
	PDU_ERR_ID_NOT_SUPPORTED = 0x00000062, /* IOCTL command id not supported */
	PDU_ERR_COMPARAM_LOCKED = 0x00000064, /* Physical ComParam locked by another ComLogicalLink */
	PDU_ERR_TX_QUEUE_FULL = 0x00000070, /* ComLogicalLink transmit queue is full */
	PDU_ERR_VOLTAGE_NOT_SUPPORTED = 0x00000080, /* Voltage value not supported by MVCI protocol module */
	PDU_ERR_MUX_RSC_NOT_SUPPORTED = 0x00000081, /* Pin/resource not supported by MVCI protocol module */
	PDU_ERR_CABLE_UNKNOWN = 0x00000082, /* Cable attached is of unknown type */
	PDU_ERR_NO_CABLE_DETECTED = 0x00000083, /* No cable detected by MVCI protocol module */
	PDU_ERR_CLL_CONNECTED = 0x00000084, /* ComLogicalLink already in PDU_CLLST_ONLINE state */
	PDU_ERR_TEMPPARAM_NOT_ALLOWED = 0x00000090, /* Physical ComParams cannot be changed as temporary ComParam */
	PDU_ERR_RSC_LOCKED = 0x000000A0, /* The resource is already locked */
	PDU_ERR_RSC_LOCKED_BY_OTHER_CLL = 0x000000A1, /* Resource locked by another ComLogicalLink */
	PDU_ERR_RSC_NOT_LOCKED = 0x000000A2, /* Resource already in unlocked state */
	PDU_ERR_API_SW_OUT_OF_DATE = 0x000000A4, /* API software older than MVCI protocol module software */
	PDU_ERR_MODULE_FW_OUT_OF_DATE = 0x000000A5, /* MVCI protocol module software older than API software */
	PDU_ERR_PIN_NOT_CONNECTED = 0x000000A6  /* Requested pin not routed by supported cable */
} T_PDU_ERROR;

/* Base item - used for PDUDestroyItem casting */
typedef struct {
	T_PDU_IT ItemType;
} PDU_ITEM;

typedef struct {
	T_PDU_IT ItemType;
	void    *pData;
} PDU_DATA_ITEM;

typedef struct {
	UNUM32 DataSize; 
	UNUM8 *pData;
	/* number of bytes in the data array */ /* pointer to the data array */
} PDU_IO_BYTEARRAY_DATA;

typedef struct {
	UNUM32 ProgVoltage_mv; 
	UNUM32 PinOnDLC;
	/* programming voltage in mV */ /* pin number on Data Link Connector */
} PDU_IO_PROG_VOLTAGE_DATA;

// 1. Define the filter type enum FIRST
typedef enum {
	PDU_FLT_PASS = 0x00000001,
	PDU_FLT_BLOCK = 0x00000002,
	PDU_FLT_PASS_UUDT = 0x00000011,
	PDU_FLT_BLOCK_UUDT = 0x00000012
} T_PDU_FILTER;

// 2. Filter entry struct
typedef struct {
	T_PDU_FILTER FilterType;
	UNUM32       FilterNumber;
	UNUM32       FilterCompareSize;
	UNUM8        FilterMaskMessage[12];
	UNUM8        FilterPatternMessage[12];
} PDU_IO_FILTER_DATA;

#include <assert.h>
static_assert(sizeof(PDU_IO_FILTER_DATA) == 0x24,
	"PDU_IO_FILTER_DATA must be 36 bytes");


// 3. Filter list struct
typedef struct {
	UNUM32 NumFilterEntries;
	PDU_IO_FILTER_DATA *pFilterData;
} PDU_IO_FILTER_LIST;

/* Per-module data row - spec 11.1.4.6 */
typedef struct {
	UNUM32        ModuleTypeId;
	UNUM32        hMod;
	CHAR8        *pVendorModuleName;
	CHAR8        *pVendorAdditionalInfo;
	T_PDU_UINT32  ModuleStatus;
} PDU_MODULE_DATA;

typedef struct {
	T_PDU_UINT32 hMod;
	T_PDU_UINT32 hDev;
	T_PDU_UINT32 DeviceStatus;
} PDU_DEVICE_DATA;

#define PDU_DEVST_AVAIL  0x00000001

typedef struct {
	T_PDU_UINT32 ResourceId;
	T_PDU_UINT32 ProtocolId;
	T_PDU_UINT32 BusTypeId;
	const char  *ShortName;
} PDU_RSC_DATA;

typedef struct {
	int constructed;

	int numModules;
	PDU_MODULE_DATA modules[4];

	int numDevices;
	PDU_DEVICE_DATA devices[4];

	int numResources;
	PDU_RSC_DATA resources[8];

	int nextCLL;

	char optionStr[128];
	void* apiTag;

} PDU_CONTEXT;

static PDU_CONTEXT gPdu;

typedef struct {
	T_PDU_IT         ItemType;
	UNUM32           NumEntries;
	PDU_MODULE_DATA *pModuleData;
} PDU_MODULE_ITEM;

/* Flag data */
typedef struct {
	UNUM32  NumFlagBytes;
	UNUM8  *pFlagData;
} PDU_FLAG_DATA;

typedef struct {
	T_PDU_UINT32   ResponseType;
	T_PDU_UINT32   AcceptanceId;
	T_PDU_UINT32   NumMaskPatternBytes;
	T_PDU_UINT8   *pMaskData;
	T_PDU_UINT8   *pPatternData;
	T_PDU_UINT32   NumUniqueRespIds;
	T_PDU_UINT32  *pUniqueRespIds;
} PDU_EXP_RESP_DATA;

typedef struct {
	T_PDU_UINT32       Time;
	T_PDU_INT32        NumSendCycles;
	T_PDU_INT32        NumReceiveCycles;
	T_PDU_UINT32       TempParamUpdate;
	T_PDU_UINT32       TxFlagNumBytes;
	T_PDU_UINT8       *pTxFlagData;
	T_PDU_UINT32       NumPossibleExpectedResponses;
	PDU_EXP_RESP_DATA *pExpectedResponseArray;
} PDU_COP_CTRL_DATA;

typedef struct {
	T_PDU_UINT32 hRes;                 /* Resource handle */
	T_PDU_UINT32 ResourceTypeId;       /* e.g., VPW = 1 */
	char         ResourceName[64];     /* "VPW Channel" */
} PDU_RESOURCE_ITEM;

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
	T_PDU_UINT32 UniqueRespIdentifier;  // Tech2Win's opaque ID
	UNUM8        ExpectedSrcAddr;       // ECU addr from CP_EcuRespSourceAddress
	UNUM8        ExpectedHdrByte;       // from CP_FuncRespFormatPriorityType
	UNUM8        ExpectedTargetAddr;    // from CP_FuncRespTargetAddr
	int          IsCatchAll;
} VPW_URID_ENTRY;

typedef struct {
    UNUM32          NumEntries;
    VPW_URID_ENTRY *pEntries;
} VPW_URID_TABLE;

/* =========================================================================
* ComParam Data Types (ISO 22900-2)
* ========================================================================= */

/* ComParam data type enum (T_PDU_PT) */
typedef enum E_PDU_PT {
	PDU_PT_UNUM8 = 0x00000101,
	PDU_PT_SNUM8 = 0x00000102,
	PDU_PT_UNUM16 = 0x00000103,
	PDU_PT_SNUM16 = 0x00000104,
	PDU_PT_UNUM32 = 0x00000105,   /* <-- VPW uses this one */
	PDU_PT_SNUM32 = 0x00000106,
	PDU_PT_BYTEFIELD = 0x00000107,
	PDU_PT_STRUCTFIELD = 0x00000108,
	PDU_PT_LONGFIELD = 0x00000109
} T_PDU_PT;

/* ComParam class enum (T_PDU_PC) */
typedef enum E_PDU_PC {
	PDU_PC_PROTOCOL = 0x00000001,   /* <-- VPW uses this */
	PDU_PC_BUSTYPE = 0x00000002,   /* <-- CP_Baudrate uses this */
	PDU_PC_UNIQUE_ID = 0x00000003
} T_PDU_PC;

/* ComParam struct type enum (T_PDU_CPST) */
typedef enum E_PDU_CPST {
	PDU_CPST_SESSION_TIMING = 0x00000001,
	PDU_CPST_ACCESS_TIMING = 0x00000002
} T_PDU_CPST;

/* Optional ComParam data structures (not used for VPW but required by spec) */
typedef struct {
	UNUM32 ParamMaxLen;
	UNUM32 ParamActLen;
	UNUM8 *pDataArray;
} PDU_PARAM_BYTEFIELD_DATA;

typedef struct {
	T_PDU_CPST ComParamStructType;
	UNUM32     ParamMaxEntries;
	UNUM32     ParamActEntries;
	void      *pStructArray;
} PDU_PARAM_STRUCTFIELD_DATA;

typedef struct {
	UNUM32  ParamMaxLen;
	UNUM32  ParamActLen;
	UNUM32 *pDataArray;
} PDU_PARAM_LONGFIELD_DATA;

/* =========================================================================
* PDU_PARAM_ITEM (used by PDUSetComParam / PDUGetComParam)
* ========================================================================= */
typedef struct {
	T_PDU_IT  ItemType;         /* Always PDU_IT_PARAM */
	UNUM32    ComParamId;       /* CP_* ID */
	T_PDU_PT  ComParamDataType; /* Always PDU_PT_UNUM32 for VPW */
	T_PDU_PC  ComParamClass;    /* Usually PDU_PC_PROTOCOL */
	void     *pComParamData;    /* -> UNUM32 value */
} PDU_PARAM_ITEM;

typedef struct {
	UNUM32 UniqueRespIdentifier;
	UNUM32 NumParamItems;
	PDU_PARAM_ITEM *pParams;
} PDU_ECU_UNIQUE_RESP_DATA;

typedef struct {
	T_PDU_IT ItemType; /* value= PDU_IT_UNIQUE_RESP_ID_TABLE */
	UNUM32 NumEntries;  /* number of entries in the table */
	PDU_ECU_UNIQUE_RESP_DATA *pUniqueData; /* pointer to array of table entries for each ECU response */
} PDU_UNIQUE_RESP_ID_TABLE_ITEM;

typedef struct {
	T_PDU_UINT32 MVCI_Part1StandardVersion;
	T_PDU_UINT32 MVCI_Part2StandardVersion;
	T_PDU_UINT32 HwSerialNumber;
	char         HwName[64];
	T_PDU_UINT32 HwVersion;
	T_PDU_UINT32 HwDate;
	T_PDU_UINT32 HwInterface;
	char         FwName[64];
	T_PDU_UINT32 FwVersion;
	T_PDU_UINT32 FwDate;
	char         VendorName[64];
	char         PDUApiSwName[64];
	T_PDU_UINT32 PDUApiSwVersion;
	T_PDU_UINT32 PDUApiSwDate;
} PDU_VERSION_DATA;

/* Result data payload inside an event item */
typedef struct {
	PDU_FLAG_DATA  RxFlag;
	T_PDU_UINT32   UniqueRespIdentifier;
	T_PDU_UINT32   AcceptanceId;
	PDU_FLAG_DATA  TimestampFlags;
	T_PDU_UINT32   TxMsgDoneTimestamp;
	T_PDU_UINT32   StartMsgTimestamp;
	void          *pExtraInfo;
	T_PDU_UINT32   NumDataBytes;
	T_PDU_UINT8   *pDataBytes;
} PDU_RESULT_DATA;

typedef struct PDU_EVENT_ITEM {
	T_PDU_IT      ItemType;       // offset 0  — ItemType value goes here
	T_PDU_UINT32  hCoPrimitive;   // offset 4  — spec calls this hCop
	void         *pCoPTag;        // offset 8  — NULL for us
	T_PDU_UINT32  Timestamp;      // offset 12
	void         *pData;          // offset 16
} PDU_EVENT_ITEM;

/* keep your original PDU_HANDLE_UNDEF if you want, but only define it once */
#ifndef PDU_HANDLE_UNDEF
#define PDU_HANDLE_UNDEF 0xFFFFFFFEUL
#endif

/* Bosch/Tech2Win callback ABI: event item + user data */
typedef void (PDU_CALL *T_PDU_CALLBACK)(T_PDU_UINT32 ItemType,
	T_PDU_UINT32 hMod,
	T_PDU_UINT32 hCLL,
	void *pCllTag,
	void *pAPITag);

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

static PDU_EVENT_ITEM g_EmptyEvent = { 0 };
static EventQueue     g_ModuleEvtQ;
static int            g_ModuleEvtQInit = 0;

/* =========================================================================
* Per-connection state
* ====================================================================== */
#define MAX_CONNECTIONS  8

typedef struct {
	int           InUse;
	T_PDU_UINT32  ResourceId;
	T_PDU_UINT32  ModHandle;
	T_PDU_UINT32  LastCoPrimHandle;
	unsigned long J2534ChannelID;
	unsigned long J2534FilterID;
	int           ChannelOpen;
	int           Connected;
	EventQueue    EvtQ;
	DWORD         LastCoPrimTime;

	HANDLE        hRxThread;
	volatile int  RxThreadRun;

	T_PDU_UINT32  CllState;

	CRITICAL_SECTION  uridLock;          // ADD — protects URID table writes

	PDU_UNIQUE_RESP_ID_TABLE_ITEM *pWorkingURID;
	VPW_URID_TABLE ActiveURID;
	PDU_IO_FILTER_LIST *pActiveFilters;

	int           PrimitiveActive;
	uint8_t       ExpectedResponseId;

	int           ResultCount;
	int           ExpectedResults;
	int           FilterActive;
	int           FilterOverrideByIoCtl;

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
	HANDLE			hIdentMutex;
} g;

/* =========================================================================
* ComParam Store (per CLL)
* ========================================================================= */

#define MAX_COMPARAMS_PER_CLL 64

typedef struct {
	UNUM32 ParamId;
	UNUM32 Value;
	int    InUse;
} CP_ENTRY;

typedef struct {
	CP_ENTRY Entries[MAX_COMPARAMS_PER_CLL];
} CP_STORE;

static CP_STORE g_CpStore[MAX_CONNECTIONS];

static CP_STORE *GetCpStore(T_PDU_UINT32 hCLL)
{
	if (hCLL >= MAX_CONNECTIONS) return NULL;
	if (!g.Connections[hCLL].InUse) return NULL;
	return &g_CpStore[hCLL];
}

static void CpSet(CP_STORE *store, UNUM32 pid, UNUM32 val)
{
	for (int i = 0; i < MAX_COMPARAMS_PER_CLL; i++) {
		if (store->Entries[i].InUse && store->Entries[i].ParamId == pid) {
			store->Entries[i].Value = val;
			return;
		}
	}
	for (int i = 0; i < MAX_COMPARAMS_PER_CLL; i++) {
		if (!store->Entries[i].InUse) {
			store->Entries[i].InUse = 1;
			store->Entries[i].ParamId = pid;
			store->Entries[i].Value = val;
			return;
		}
	}
	LOG1("CpSet: store full, dropping ParamId=%u", pid);
}

static int CpGet(CP_STORE *store, UNUM32 pid, UNUM32 *pVal)
{
	for (int i = 0; i < MAX_COMPARAMS_PER_CLL; i++) {
		if (store->Entries[i].InUse && store->Entries[i].ParamId == pid) {
			*pVal = store->Entries[i].Value;
			return 1;
		}
	}
	return 0;
}

/* =========================================================================
* DPDU Runtime Identifiers (NOT from the .mdi file)
* ========================================================================= */
static const T_PDU_UINT32 RUNTIME_ModuleID = 1;
static const T_PDU_UINT32 RUNTIME_ObjectID = 1;
static const T_PDU_UINT32 RUNTIME_ResourceID = 1;
static const T_PDU_UINT32 RUNTIME_ComLogicalLinkID = 1;

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
// ============================================================
// PARAM + WORKING URID memory helpers
// Must appear above Cll_SyncURID and PDUSetUniqueRespIdTable
// ============================================================

static void FreeParam(PDU_PARAM_ITEM *p)
{
	if (!p || !p->pComParamData) return;

	switch (p->ComParamDataType) {
	case 0x101: case 0x102:
	case 0x103: case 0x104:
	case 0x105: case 0x106:
		free(p->pComParamData);
		break;

	case 0x107: case 0x109: {
		struct { DWORD max; DWORD cur; void *data; } *bf = p->pComParamData;
		if (bf->data) free(bf->data);
		free(bf);
		break;
	}

	case 0x108: {
		struct { DWORD type; DWORD count; DWORD size; void *entries; } *mf = p->pComParamData;
		if (mf->entries) free(mf->entries);
		free(mf);
		break;
	}

	default:
		free(p->pComParamData);
		break;
	}
	p->pComParamData = NULL;
}

static PDU_PARAM_ITEM* DeepCopyParam(PDU_PARAM_ITEM *dst, PDU_PARAM_ITEM *src)
{
	if (!dst || !src) return NULL;

	dst->ItemType = src->ItemType;
	dst->ComParamId = src->ComParamId;
	dst->ComParamDataType = src->ComParamDataType;
	dst->ComParamClass = src->ComParamClass;

	if (!src->pComParamData) {
		dst->pComParamData = NULL;
		return dst;
	}

	switch (src->ComParamDataType) {
	case 0x101: case 0x102:
		dst->pComParamData = malloc(1);
		*(BYTE*)dst->pComParamData = *(BYTE*)src->pComParamData;
		break;
	case 0x103: case 0x104:
		dst->pComParamData = malloc(2);
		*(USHORT*)dst->pComParamData = *(USHORT*)src->pComParamData;
		break;
	case 0x105: case 0x106:
		dst->pComParamData = malloc(4);
		*(DWORD*)dst->pComParamData = *(DWORD*)src->pComParamData;
		break;
	case 0x107: case 0x109: {
		struct { DWORD max; DWORD cur; void *data; } *s, *d;
		s = src->pComParamData;
		d = malloc(0xC);
		d->max = s->max;
		d->cur = s->cur;
		d->data = (s->max && s->data) ? malloc(s->max) : NULL;
		if (d->data) { memset(d->data, 0, s->max); memcpy(d->data, s->data, s->cur); }
		dst->pComParamData = d;
		break;
	}
	case 0x108: {
		struct { DWORD type; DWORD count; DWORD size; void *entries; } *s, *d;
		s = src->pComParamData;
		d = malloc(0x10);
		d->type = s->type; d->count = s->count; d->size = s->size;
		d->entries = (s->count && s->entries) ? malloc(s->count * 6) : NULL;
		if (d->entries) { memset(d->entries, 0, s->count * 6); memcpy(d->entries, s->entries, s->count * 6); }
		dst->pComParamData = d;
		break;
	}
	default:
		dst->pComParamData = NULL;
		break;
	}
	return dst;
}

static void FreeWorkingURID(PDU_UNIQUE_RESP_ID_TABLE_ITEM *tbl)
{
	UNUM32 i, p;
	if (!tbl) return;

	if (tbl->pUniqueData) {
		for (i = 0; i < tbl->NumEntries; i++) {
			PDU_ECU_UNIQUE_RESP_DATA *entry = &tbl->pUniqueData[i];
			if (entry->pParams) {
				for (p = 0; p < entry->NumParamItems; p++)
					FreeParam(&entry->pParams[p]);
				free(entry->pParams);
				entry->pParams = NULL;
			}
		}
		free(tbl->pUniqueData);
		tbl->pUniqueData = NULL;
	}
	free(tbl);
}

// ============================================================
// RX gating helpers (URID + filter logic)
// ============================================================
static void FreeVPWURIDTable(VPW_URID_TABLE *t)
{
	if (t->pEntries) {
		free(t->pEntries);
		t->pEntries = NULL;
	}
	t->NumEntries = 0;
}

static void Cll_SyncURID(PDU_CONN_STATE *c)
{
	logmsg("Cll_SyncURID CALLED");
	FreeVPWURIDTable(&c->ActiveURID);

	if (!c)
		return;

	PDU_UNIQUE_RESP_ID_TABLE_ITEM *tbl = c->pWorkingURID;
	if (!tbl || !tbl->NumEntries || !tbl->pUniqueData)
		return;

	c->ActiveURID.pEntries =
		(VPW_URID_ENTRY*)calloc(tbl->NumEntries, sizeof(VPW_URID_ENTRY));
	if (!c->ActiveURID.pEntries)
		return;

	c->ActiveURID.NumEntries = tbl->NumEntries;

	UNUM32 i;
	for (i = 0; i < tbl->NumEntries; i++) {
		PDU_ECU_UNIQUE_RESP_DATA *src = &tbl->pUniqueData[i];
		VPW_URID_ENTRY *dst = &c->ActiveURID.pEntries[i];

		dst->UniqueRespIdentifier = src->UniqueRespIdentifier;
		dst->IsCatchAll = (src->UniqueRespIdentifier == PDU_ID_UNDEF);
		dst->ExpectedHdrByte = 0x48;
		dst->ExpectedTargetAddr = 0x6B;
		dst->ExpectedSrcAddr = 0xFF;

		UNUM32 p;
		for (p = 0; p < src->NumParamItems; p++) {
			PDU_PARAM_ITEM *cp = &src->pParams[p];
			if (!cp || !cp->pComParamData)
				continue;

			UNUM32 val = *(UNUM32*)cp->pComParamData;

			switch (cp->ComParamId) {
			case CP_EcuRespSourceAddress:
				dst->ExpectedSrcAddr = (UNUM8)(val & 0xFF);
				break;
			case CP_FuncRespFormatPriorityType:
				dst->ExpectedHdrByte = (UNUM8)(val & 0xFF);
				break;
			case CP_FuncRespTargetAddr:
				dst->ExpectedTargetAddr = (UNUM8)(val & 0xFF);
				break;
			}
		}

		logmsg("Cll_SyncURID: [%u] id=0x%08X hdr=0x%02X tgt=0x%02X src=0x%02X catchall=%d",
			i, dst->UniqueRespIdentifier, dst->ExpectedHdrByte,
			dst->ExpectedTargetAddr, dst->ExpectedSrcAddr, dst->IsCatchAll);
	}
}

static void RebuildJ2534Filters(PDU_CONN_STATE *c)
{
	logmsg("RebuildJ2534Filters: CALLED");
	if (!c || c->J2534ChannelID == (unsigned long)-1)
		return;

	if (c->FilterOverrideByIoCtl) {
		logmsg("RebuildJ2534Filters: client filter override, skipping");
		return;
	}


	c->J2534FilterID = (unsigned long)-1;

	long ret = g.pfIoctl(c->J2534ChannelID, IOCTL_CLEAR_MSG_FILTERS, NULL, NULL);
	logmsg("RebuildJ2534Filters: CLEAR ret=%ld", ret);

	int installed = 0;
	UNUM32 i;

	for (i = 0; i < c->ActiveURID.NumEntries; i++) {
		VPW_URID_ENTRY *e = &c->ActiveURID.pEntries[i];

		if (e->IsCatchAll || e->ExpectedSrcAddr == 0xFF)
			continue;

		PASSTHRU_MSG mask = { 0 }, pat = { 0 };
		mask.ProtocolID = pat.ProtocolID = J2534_J1850VPW;
		mask.DataSize = pat.DataSize = 3;

		mask.Data[0] = 0xFF; pat.Data[0] = e->ExpectedHdrByte;
		mask.Data[1] = 0xFF; pat.Data[1] = e->ExpectedTargetAddr;
		mask.Data[2] = 0xFF; pat.Data[2] = e->ExpectedSrcAddr;

		unsigned long fid = 0;
		ret = g.pfStartFilter(c->J2534ChannelID, J2534_PASS_FILTER,
			&mask, &pat, NULL, &fid);

		logmsg("RebuildJ2534Filters: src=0x%02X ret=%ld fid=%lu",
			e->ExpectedSrcAddr, ret, fid);

		if (ret == J2534_STATUS_NOERROR) {
			c->J2534FilterID = fid;
			installed++;
		}
	}

	if (installed == 0) {
		PASSTHRU_MSG mask = { 0 }, pat = { 0 };
		mask.ProtocolID = pat.ProtocolID = J2534_J1850VPW;
		mask.DataSize = pat.DataSize = 1;
		mask.Data[0] = 0x00;
		pat.Data[0] = 0x00;

		unsigned long fid = 0;
		ret = g.pfStartFilter(c->J2534ChannelID, J2534_PASS_FILTER,
			&mask, &pat, NULL, &fid);

		logmsg("RebuildJ2534Filters: pass-all fallback ret=%ld fid=%lu", ret, fid);

		if (ret == J2534_STATUS_NOERROR)
			c->J2534FilterID = fid;
	}
}

static int Cll_ResultAllowed(PDU_CONN_STATE *c, PASSTHRU_MSG *msg)
{
	UNUM32 i;
	UNUM32 j;

	if (!c || !msg)
		return 1;

	if (!c->FilterActive)
		return 1;

	if (!c->pActiveFilters)
		return 1;

	if (!c->pActiveFilters->pFilterData)
		return 1;

	if (c->pActiveFilters->NumFilterEntries == 0)
		return 1;

	for (i = 0; i < c->pActiveFilters->NumFilterEntries; i++) {
		PDU_IO_FILTER_DATA *f = &c->pActiveFilters->pFilterData[i];
		UNUM32 compareSize;
		int match = 1;

		if (f->FilterType != PDU_FLT_PASS &&
			f->FilterType != PDU_FLT_BLOCK) {
			continue;
		}

		compareSize = f->FilterCompareSize;

		if (compareSize > 12)
			compareSize = 12;

		if (compareSize > msg->DataSize)
			match = 0;

		if (match) {
			for (j = 0; j < compareSize; j++) {
				if ((msg->Data[j] & f->FilterMaskMessage[j]) !=
					f->FilterPatternMessage[j]) {
					match = 0;
					break;
				}
			}
		}

		if (match) {
			if (f->FilterType == PDU_FLT_PASS) {
				logmsg("Cll_ResultAllowed: PASS filter[%u] matched", i);
				return 1;
			}
			else {
				logmsg("Cll_ResultAllowed: BLOCK filter[%u] matched", i);
				return 0;
			}
		}
	}

	logmsg("Cll_ResultAllowed: no filter matched, dropping");
	return 0;
}

/* ======================================================================
* Background RX thread
* Runs per connection after PDUConnect; reads frames from SM2 via
* PassThruReadMsgs and pushes PDU_EVENT_ITEMs into the connection's queue.
* ====================================================================== */
static unsigned __stdcall RxThreadProc(void *pArg)
{
	PDU_CONN_STATE *c;
	PASSTHRU_MSG msg;
	unsigned long timeout;
	unsigned long numMsgs;
	long ret;
	c = (PDU_CONN_STATE *)pArg;

	logmsg("RxThread Proc STARTED c=%p", c);
	while (c->RxThreadRun) {

		memset(&msg, 0, sizeof(msg));
		timeout = 50;
		numMsgs = 1;

		ret = g.pfReadMsgs(c->J2534ChannelID, &msg, &numMsgs, timeout);
		logmsg("RxThread: pfReadMsgs ret=%ld numMsgs=%lu J2534ChannelID=%d", ret, numMsgs, c->J2534ChannelID);

		if (ret == J2534_STATUS_NOERROR && numMsgs > 0) {

			if (msg.RxStatus & TX_MSG_TYPE) {
				logmsg("RxThread: dropping TX echo RxStatus=0x%lx", msg.RxStatus);
				Sleep(5);
				continue;
			}

			logmsg("RxThread: data[%lu]=", msg.DataSize);
			{
				unsigned long i;
				for (i = 0; i < msg.DataSize && i < sizeof(msg.Data); i++) {
					logmsg(" %02X", msg.Data[i]);
				}
			}
			logmsg("");

			if (c->ExpectedResponseId != 0) {
				if (msg.DataSize == 0) {
					logmsg("RxThread: drop, empty frame (primitive active)");
					Sleep(5);
					continue;
				}

				if (msg.Data[0] != c->ExpectedResponseId) {
					logmsg("RxThread: drop header=0x%02X expected=0x%02X (primitive active)",
						msg.Data[0], c->ExpectedResponseId);
					Sleep(5);
					continue;
				}

				c->LastCoPrimTime = GetTickCount();
				logmsg("RxThread: ACCEPT header=0x%02X expected=0x%02X",
					msg.Data[0], c->ExpectedResponseId);
			}

			if (!Cll_ResultAllowed(c, &msg)) {
				logmsg("RxThread: frame dropped by software message filter");
				Sleep(5);
				continue;
			}

			{
				PDU_RESULT_DATA *res;
				T_PDU_UINT8 *data;
				PDU_EVENT_ITEM *ev;

				res = (PDU_RESULT_DATA *)calloc(1, sizeof(PDU_RESULT_DATA));  // ONLY ONCE
				if (!res) {
					logmsg("RxThread: calloc res failed");
					Sleep(5);
					continue;
				}

				data = NULL;
				if (msg.DataSize > 0) {
					data = (T_PDU_UINT8 *)calloc(1, msg.DataSize);
					if (!data) {
						logmsg("RxThread: calloc data failed");
						free(res);
						Sleep(5);
						continue;
					}
					memcpy(data, msg.Data, msg.DataSize);
				}

				res->RxFlag.NumFlagBytes = 0;
				res->RxFlag.pFlagData = NULL;
				res->AcceptanceId = 0;
				res->TimestampFlags.NumFlagBytes = 0;
				res->TimestampFlags.pFlagData = NULL;
				res->TxMsgDoneTimestamp = 0;
				res->StartMsgTimestamp = 0;
				res->pExtraInfo = NULL;
				res->NumDataBytes = msg.DataSize;
				res->pDataBytes = data;

				if (msg.DataSize >= 2) {
					res->AcceptanceId = msg.Data[1];
				}
				else {
					res->AcceptanceId = 0;
				}

				res->UniqueRespIdentifier = PDU_ID_UNDEF;

				if (msg.DataSize >= 3 && c->ActiveURID.NumEntries > 0) {
					UNUM32 i;
					for (i = 0; i < c->ActiveURID.NumEntries; i++) {
						VPW_URID_ENTRY *e;

						e = &c->ActiveURID.pEntries[i];

						if (e->IsCatchAll || e->ExpectedSrcAddr == 0xFF) {
							continue;
						}

						if (msg.Data[0] == e->ExpectedHdrByte &&
							msg.Data[1] == e->ExpectedTargetAddr &&
							msg.Data[2] == e->ExpectedSrcAddr) {
							res->UniqueRespIdentifier = e->UniqueRespIdentifier;
							logmsg("RxThread: URID[%u] match id=0x%08X", i, e->UniqueRespIdentifier);
							break;
						}
					}

					if (res->UniqueRespIdentifier == PDU_ID_UNDEF) {
						logmsg("RxThread: no URID match %02X-%02X-%02X dropped",
							msg.Data[0], msg.Data[1], msg.Data[2]);
						if (res->pDataBytes) {
							free(res->pDataBytes);
						}
						free(res);
						Sleep(5);
						continue;
					}
				}

				ev = (PDU_EVENT_ITEM *)calloc(1, sizeof(PDU_EVENT_ITEM));
				if (!ev) {
					if (data) {
						free(data);
					}
					free(res);
					Sleep(5);
					continue;
				}

				ev->ItemType = PDU_IT_RESULT;
				ev->hCoPrimitive = c->LastCoPrimHandle;
				ev->Timestamp = msg.Timestamp ? msg.Timestamp : GetTickCount();
				ev->pData = res;

				logmsg("RxThread: pushing result ev=%p res=%p pDataBytes=%p NumDataBytes=%lu hCoPrim=%lu",
					ev, res, res->pDataBytes, res->NumDataBytes, ev->hCoPrimitive);

				if (!evq_push(&c->EvtQ, ev)) {
					logmsg("RxThread: queue full, dropping ev=%p res=%p data=%p",
						ev, res, res->pDataBytes);
					if (res->pDataBytes) {
						free(res->pDataBytes);
					}
					free(res);
					free(ev);
				}
				else {
					logmsg("RxThread: pushed event to queue");

					if (c->PrimitiveActive) {
						c->ResultCount++;
						logmsg("RxThread: result %d/%d hCoPrim=%lu",
							c->ResultCount, c->ExpectedResults, c->LastCoPrimHandle);

						if (c->ResultCount >= c->ExpectedResults) {
							PDU_EVENT_ITEM *evFin;
							T_PDU_UINT32 *pStatus;
							T_PDU_UINT32 finishedHandle;

							finishedHandle = c->LastCoPrimHandle;

							evFin = (PDU_EVENT_ITEM *)calloc(1, sizeof(PDU_EVENT_ITEM));
							pStatus = (T_PDU_UINT32 *)calloc(1, sizeof(T_PDU_UINT32));

							if (evFin && pStatus) {
								*pStatus = PDU_COPST_FINISHED;
								evFin->ItemType = PDU_IT_STATUS;
								evFin->hCoPrimitive = finishedHandle;
								evFin->pData = pStatus;

								if (!evq_push(&c->EvtQ, evFin)) {
									logmsg("RxThread: queue full dropping FINISHED hCoPrim=%lu", finishedHandle);
									free(pStatus);
									free(evFin);
								}
								else {
									logmsg("RxThread: FINISHED after %d results hCoPrim=%lu",
										c->ResultCount, finishedHandle);
								}
							}
							else {
								if (pStatus) {
									free(pStatus);
								}
								if (evFin) {
									free(evFin);
								}
							}

							c->PrimitiveActive = 0;
							c->ExpectedResponseId = 0;
							c->LastCoPrimHandle = 0;
							c->ResultCount = 0;
							c->ExpectedResults = 1;
						}
					}

					if (g.EventCallback) {
						g.EventCallback(PDU_EVT_DATA_AVAILABLE,
							1,
							(T_PDU_UINT32)(c - g.Connections),
							NULL,
							g.pCallbackUserData);
					}
				}
			}
		}
		else if (ret != J2534_ERR_BUFFER_EMPTY && ret != J2534_STATUS_NOERROR) {
			logmsg("RxThread: pfReadMsgs ret=%ld", ret);
		}

		Sleep(5);
	}
	logmsg("RXthread proc started c=%p", c);
	return 0;
}

static void StartRxThread(PDU_CONN_STATE *c)
{
	logmsg("StartRxThread: enabled c=%p", c);
	c->RxThreadRun = 1;
	c->hRxThread = (HANDLE)_beginthreadex(NULL, 0, RxThreadProc, c, 0, NULL);
	if (c->hRxThread == NULL) {
		logmsg("StartRxThread: FAILED errno=%d", errno);
		c->RxThreadRun = 0;
	}
	else {
		logmsg("StartRxThread: hRxThread=%p OK", c->hRxThread);
	}
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
	LOG1("ENTER, g.MdiPath = %s", g.MdiPath);

	char jpath[MAX_PATH];
	LOG0("Copying g.MdiPath into jpath");
	strncpy_s(jpath, MAX_PATH, g.MdiPath, _TRUNCATE);

	LOG1("Searching for last slash in %s", jpath);
	char *slash = strrchr(jpath, '\\');

	if (slash) {
		LOG1("Slash found at offset %ld — replacing filename with smj2534.dll",
			(long)(slash - jpath));
		strcpy_s(slash + 1, MAX_PATH - (slash - jpath) - 1, "smj2534.dll");
	}
	else {
		LOG0("No slash found — using smj2534.dll in current directory");
		strcpy_s(jpath, MAX_PATH, "smj2534.dll");
	}

	LOG1("Attempting LoadLibraryA on %s", jpath);
	g.hJ2534Dll = LoadLibraryA(jpath);

	if (!g.hJ2534Dll) {
		DWORD err = GetLastError();
		LOG1("LoadLibraryA FAILED, GetLastError = %lu", err);
		return 0;
	}

	LOG0("LoadLibraryA succeeded, resolving exports");

#define GP(name, type, field) \
    do { \
        LOG1("Resolving %s", name); \
        g.field = (type)GetProcAddress(g.hJ2534Dll, name); \
        LOG1("GetProcAddress(%s) returned %p", name, g.field); \
        if (!g.field) { \
            LOG1("GetProcAddress(%s) FAILED — unloading DLL", name); \
            FreeLibrary(g.hJ2534Dll); \
            g.hJ2534Dll = NULL; \
            return 0; \
        } \
    } while (0)

	GP("PassThruOpen", FN_PassThruOpen, pfOpen);
	GP("PassThruClose", FN_PassThruClose, pfClose);
	GP("PassThruConnect", FN_PassThruConnect, pfConnect);
	GP("PassThruDisconnect", FN_PassThruDisconnect, pfDisconnect);
	GP("PassThruReadMsgs", FN_PassThruReadMsgs, pfReadMsgs);
	GP("PassThruWriteMsgs", FN_PassThruWriteMsgs, pfWriteMsgs);
	GP("PassThruStartMsgFilter", FN_PassThruStartMsgFilter, pfStartFilter);
	GP("PassThruStopMsgFilter", FN_PassThruStopMsgFilter, pfStopFilter);
	GP("PassThruIoctl", FN_PassThruIoctl, pfIoctl);
	GP("PassThruGetLastError", FN_PassThruGetLastError, pfGetLastError);

#undef GP
	LOG0("All J2534 exports resolved — calling PassThruOpen");

	unsigned long devId = 0;
	long r = -1;

	__try {
		// Option A: NULL name (standard)
		r = g.pfOpen(NULL, &devId);

		// Option B: if driver crashes on NULL, try a name instead
		// char name[] = "SM2 Pro";
		// r = g.pfOpen(name, &devId);

	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DWORD code = GetExceptionCode();
		LOG1("PassThruOpen raised SEH exception 0x%08lx", code);
		FreeLibrary(g.hJ2534Dll);
		g.hJ2534Dll = NULL;
		return 0;
	}

	LOG1("PassThruOpen returned %ld devId=%lu", r, devId);

	if (g.pfGetLastError) {
		char desc[512] = { 0 };
		long r2 = g.pfGetLastError(desc);
		LOG1("PassThruGetLastError returned %ld desc=\"%s\"", r2, desc);
	}

	if (r != J2534_STATUS_NOERROR) {
		LOG0("PassThruOpen FAILED — unloading J2534 DLL");
		FreeLibrary(g.hJ2534Dll);
		g.hJ2534Dll = NULL;
		return 0;
	}

	g.J2534DeviceID = devId;
	g.DeviceOpen = 1;

	LOG0("SUCCESS — J2534 device opened");
	return 1;
}
/* =========================================================================
* Connection helpers
* ====================================================================== */
static int AllocConn(T_PDU_UINT32 *phConn)
{
	LOG0("ENTER");

	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		LOG1("Checking slot %d (InUse=%d)", i, g.Connections[i].InUse);

		if (!g.Connections[i].InUse) {
			LOG1("Allocating slot %d", i);

			memset(&g.Connections[i], 0, sizeof(PDU_CONN_STATE));
			LOG0("Cleared connection state");

			g.Connections[i].CllState = PDU_CLLST_OFFLINE;
			g.Connections[i].pWorkingURID = NULL;
			g.Connections[i].ActiveURID.NumEntries = 0;
			g.Connections[i].ActiveURID.pEntries = NULL;

			g.Connections[i].pActiveFilters = NULL;


			/* ADD THESE FOUR LINES: */
			g.Connections[i].PrimitiveActive = 0;
			g.Connections[i].ExpectedResponseId = 0;
			g.Connections[i].ResultCount = 0;
			g.Connections[i].ExpectedResults = 1;

			evq_init(&g.Connections[i].EvtQ);

			LOG0("Initialized event queue");

			g.Connections[i].InUse = 1;
			g.Connections[i].J2534ChannelID = (unsigned long)-1;
			g.Connections[i].J2534FilterID = (unsigned long)-1;

			*phConn = (T_PDU_UINT32)i;
			LOG1("Allocated connection handle %u", *phConn);

			return 1;
		}
	}

	LOG0("FAILED — no free connection slots");
	return 0;
}

static PDU_CONN_STATE *GetConn(T_PDU_UINT32 hConn)
{
	LOG1("ENTER hConn=%u", hConn);

	// Accept undefined handles safely (Tech2Win uses this constantly)
	if (hConn == PDU_ID_UNDEF || hConn == 0xFFFFFFFF) {
		LOG0("INFO: hConn is UNDEF, returning NULL safely");
		return NULL;
	}

	if (hConn >= MAX_CONNECTIONS) {
		LOG0("ERROR: hConn out of range");
		return NULL;
	}

	if (!g.Connections[hConn].InUse) {
		LOG1("INFO: slot %u not in use (not fatal for some calls)", hConn);
		return NULL;
	}

	LOG0("Returning connection pointer");
	return &g.Connections[hConn];
}

static void FreeConn(T_PDU_UINT32 hConn)
{
	LOG1("ENTER hConn=%u", hConn);

	if (hConn >= MAX_CONNECTIONS) {
		LOG0("ERROR: hConn out of range");
		return;
	}

	PDU_CONN_STATE *c = &g.Connections[hConn];

	LOG0("Stopping RX thread");
	StopRxThread(c);

	LOG0("Destroying event queue");
	evq_destroy(&c->EvtQ);

	LOG0("Clearing connection state");
	memset(c, 0, sizeof(PDU_CONN_STATE));

	LOG0("EXIT");
}

/* =========================================================================
* Registry helpers
* ====================================================================== */
#define REG_DPDUA_ROOT   "SOFTWARE\\D-PDU API"
#define REG_VENDOR       "Bosch"
#define REG_MODULE       "MODULE_TYPE_ID_MDI_2"

static void WriteRegString(HKEY hKey, const char *name, const char *value)
{
	LOG1("Writing registry value %s = %s", name, value);
	RegSetValueExA(hKey, name, 0, REG_SZ, (const BYTE*)value, (DWORD)(strlen(value) + 1));
}

static int RegisterDll(void)
{
	LOG0("ENTER");

	char keyPath[256];
	snprintf(keyPath, sizeof(keyPath), "%s\\%s\\%s", REG_DPDUA_ROOT, REG_VENDOR, REG_MODULE);
	LOG1("Registry path = %s", keyPath);

	HKEY hKey;
	LONG res = RegCreateKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);

	if (res != ERROR_SUCCESS) {
		LOG1("HKLM create failed (%ld), trying HKCU", res);

		res = RegCreateKeyExA(HKEY_CURRENT_USER, keyPath, 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);

		if (res != ERROR_SUCCESS) {
			LOG1("HKCU create failed (%ld)", res);
			return 0;
		}
	}

	char mdiPath[MAX_PATH];
	strncpy(mdiPath, g.DllPath, MAX_PATH - 1);
	mdiPath[MAX_PATH - 1] = '\0';

	char *slash = strrchr(mdiPath, '\\');
	if (slash) {
		LOG0("Replacing DLL filename with MDI2.mdi");
		strcpy(slash + 1, "MDI2.mdi");
	}

	WriteRegString(hKey, "VCI_MODULE_DESCRIPTION_FILE", mdiPath);
	WriteRegString(hKey, "MVCI_LIBRARY_FILE", g.DllPath);
	WriteRegString(hKey, "SUPPLIER_NAME", "Bosch");
	WriteRegString(hKey, "SHORT_NAME", "D_PDU_API_Bosch_MDI_2");

	RegCloseKey(hKey);

	LOG0("SUCCESS");
	return 1;
}

static int UnregisterDll(void)   // <—— ADD THIS HERE
{
	LOG0("ENTER");

	char keyPath[256];
	snprintf(keyPath, sizeof(keyPath), "%s\\%s\\%s",
		REG_DPDUA_ROOT, REG_VENDOR, REG_MODULE);

	LOG1("Deleting registry key %s", keyPath);

	RegDeleteKeyA(HKEY_LOCAL_MACHINE, keyPath);
	RegDeleteKeyA(HKEY_CURRENT_USER, keyPath);

	LOG0("SUCCESS");
	return 1;
}


/* =========================================================================
* DllMain
* ====================================================================== */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
	LOG1("DllMain ENTER reason=%lu", reason);

	switch (reason) {

	case DLL_PROCESS_ATTACH:
		LOG0("PROCESS_ATTACH");
		InitializeCriticalSection(&g.GlobalLock);
		GetModuleFileNameA((HMODULE)hInst, g.DllPath, MAX_PATH);
		LOG1("DLL path = %s", g.DllPath);
		break;

	case DLL_PROCESS_DETACH:
		LOG0("PROCESS_DETACH");
		DeleteCriticalSection(&g.GlobalLock);
		break;
	}

	LOG0("DllMain EXIT");
	return TRUE;
}

/* =========================================================================
* Registration entry points — call once from an installer / elevated cmd:
*   rundll32 pdu_mdi2.dll,DllRegisterServer
*   rundll32 pdu_mdi2.dll,DllUnregisterServer
* ====================================================================== */
HRESULT __stdcall DllRegisterServer(void)
{
	return RegisterDll() ? S_OK : E_FAIL;
}

HRESULT __stdcall DllUnregisterServer(void)
{
	return UnregisterDll() ? S_OK : E_FAIL;
}

/* =========================================================================
* Ready notification thread — fires callback 500ms after construct
* ====================================================================== */
static unsigned __stdcall ReadyThreadProc(void *arg)
{
	UNUSED(arg);
	logmsg("ReadyThread: disabled, exiting");
	return 0;
}
/* =========================================================================
* PDUConstruct — initialize, load MDI, load smj2534.dll
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUConstruct(const char *OptionStr, void *pAPITag)
{
	LOG1("PDUConstruct: ENTER OptionStr=%s pAPITag=%p",
		OptionStr ? OptionStr : "(null)", pAPITag);

	EnterCriticalSection(&g.GlobalLock);
	LOG0("PDUConstruct: Entered GlobalLock");

	if (g.Constructed) {
		LOG0("PDUConstruct: Already constructed — returning OK");
		LeaveCriticalSection(&g.GlobalLock);
		return PDU_STATUS_NOERROR;
	}

	LOG0("PDUConstruct: Initializing global state");
	memset(&g.Connections, 0, sizeof(g.Connections));

	// ADD RIGHT HERE:
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		InitializeCriticalSection(&g.Connections[i].uridLock);
		g.Connections[i].pWorkingURID = NULL;
	}

	// EVERYTHING BELOW STAYS EXACTLY THE SAME:
	g.EventCallback = NULL;
	g.pCallbackUserData = NULL;
	g.DeviceOpen = 0;
	g.hJ2534Dll = NULL;


	LOG1("PDUConstruct: g.DllPath = %s", g.DllPath);

	LOG0("PDUConstruct: Copying DLL path into g.MdiPath");
	strncpy_s(g.MdiPath, MAX_PATH, g.DllPath, _TRUNCATE);
	LOG1("PDUConstruct: After strncpy_s, g.MdiPath = %s", g.MdiPath);

	char *slash = strrchr(g.MdiPath, '\\');
	LOG1("PDUConstruct: Slash pointer = %p", slash);

	if (slash) {
		LOG0("PDUConstruct: Replacing filename with MDI2.mdi");
		strcpy_s(slash + 1,
			MAX_PATH - (slash - g.MdiPath) - 1,
			"MDI2.mdi");
	}
	else {
		LOG0("PDUConstruct: No slash found — using default MDI2.mdi");
		strcpy_s(g.MdiPath, MAX_PATH, "MDI2.mdi");
	}

	LOG1("PDUConstruct: Final MDI path = %s", g.MdiPath);

	LOG0("PDUConstruct: Checking if MDF/MDI file exists");
	FILE *f = fopen(g.MdiPath, "r");
	if (!f) {
		LOG1("PDUConstruct: ERROR — MDF/MDI not found at %s", g.MdiPath);
		LeaveCriticalSection(&g.GlobalLock);
		return PDU_ERR_FCT_FAILED;
	}
	fclose(f);
	LOG0("PDUConstruct: MDF/MDI found and accessible");

	LOG0("PDUConstruct: Calling LoadJ2534()");
	if (!LoadJ2534()) {
		LOG0("PDUConstruct: LoadJ2534 FAILED");
		LeaveCriticalSection(&g.GlobalLock);
		return PDU_ERR_FCT_FAILED;
	}

	LOG0("PDUConstruct: Building DPDU context");

	// --- MODULE ---
	gPdu.constructed = 1;
	gPdu.numModules = 1;
	gPdu.modules[0].hMod = 1;
	gPdu.modules[0].ModuleTypeId = 28;  // from <MODULETYPE><ID>28</ID>
	gPdu.modules[0].ModuleStatus = PDU_MODST_AVAIL;
	gPdu.modules[0].pVendorModuleName =
		strdup("22124708");  // from INI vendor_module_name
	gPdu.modules[0].pVendorAdditionalInfo =
		strdup("IFType='USB' IPAddress='192.168.171.2'"); // from INI vendor_add_info

	LOG0("PDUConstruct: Module initialized: hMod=1 TypeId=28 Status=AVAIL");

	// --- DEVICE ---
	gPdu.numDevices = 1;
	gPdu.devices[0].hDev = 1;
	gPdu.devices[0].hMod = 1;
	gPdu.devices[0].DeviceStatus = PDU_DEVST_AVAIL;

	LOG0("PDUConstruct: Device initialized: hDev=1 hMod=1 Status=AVAIL");

	// --- RESOURCES ---
	gPdu.numResources = NUM_RESOURCES;
	memcpy(gPdu.resources, g_Resources, sizeof(g_Resources));
	LOG1("PDUConstruct: Resource table copied (%d resources)", NUM_RESOURCES);

	// Mark constructed
	g.Constructed = 1;

	/* Initialize static empty event item once */
	g_EmptyEvent.ItemType = 0;
	g_EmptyEvent.hCoPrimitive = PDU_HANDLE_UNDEF;
	g_EmptyEvent.Timestamp = 0;
	g_EmptyEvent.pData = NULL;
	gPdu.constructed = 1;

	// Initialize module-level event queue
	if (!g_ModuleEvtQInit) {
		evq_init(&g_ModuleEvtQ);
		g_ModuleEvtQInit = 1;
		LOG0("PDUConstruct: Module event queue initialized");
	}

	// Create GM_MDI_Ident sentinel mutex so Tech2Win proceeds to PDUModuleConnect
	g.hIdentMutex = CreateMutexA(NULL, FALSE, "Global\\MDI-2.3.103.117");
	LOG1("PDUConstruct: Created MDI ident mutex handle=%p", g.hIdentMutex);

	// --- DPDU CONTEXT SUMMARY LOGGING (this is where your big dump belongs) ---
	logmsg("PDUConstruct: DPDU context summary:");
	logmsg("  constructed=%d", gPdu.constructed);
	logmsg("  numModules=%d", gPdu.numModules);
	logmsg("  numDevices=%d", gPdu.numDevices);
	logmsg("  numResources=%d", gPdu.numResources);

	for (int i = 0; i < gPdu.numModules; i++) {
		logmsg("  module[%d]: hMod=%u TypeId=%u Status=%u Name=%s Info=%s",
			i,
			gPdu.modules[i].hMod,
			gPdu.modules[i].ModuleTypeId,
			gPdu.modules[i].ModuleStatus,
			gPdu.modules[i].pVendorModuleName ?
			gPdu.modules[i].pVendorModuleName : "(null)",
			gPdu.modules[i].pVendorAdditionalInfo ?
			gPdu.modules[i].pVendorAdditionalInfo : "(null)");
	}

	for (int i = 0; i < gPdu.numResources; i++) {
		logmsg("  resource[%d]: id=%u proto=%u bustype=%u name=%s",
			i,
			gPdu.resources[i].ResourceId,
			gPdu.resources[i].ProtocolId,
			gPdu.resources[i].BusTypeId,
			gPdu.resources[i].ShortName ?
			gPdu.resources[i].ShortName : "(null)");
	}

	LeaveCriticalSection(&g.GlobalLock);
	LOG0("PDUConstruct: EXIT OK");
	return PDU_STATUS_NOERROR;
}
/* =========================================================================
* PDUDestruct
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUDestruct(void)
{
    LOG0("ENTER");

    EnterCriticalSection(&g.GlobalLock);
    LOG0("Entered GlobalLock");

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        LOG1("Checking connection slot %d (InUse=%d)", i, g.Connections[i].InUse);

        if (g.Connections[i].InUse) {
            PDU_CONN_STATE *c = &g.Connections[i];

            LOG1("Stopping RX thread for slot %d", i);
            StopRxThread(c);

            if (c->J2534FilterID != (unsigned long)-1 && g.pfStopFilter) {
                LOG1("Stopping J2534 filter %lu", c->J2534FilterID);
                g.pfStopFilter(c->J2534ChannelID, c->J2534FilterID);
            }

            if (c->J2534ChannelID != (unsigned long)-1 && g.pfDisconnect) {
                LOG1("Disconnecting J2534 channel %lu", c->J2534ChannelID);
                g.pfDisconnect(c->J2534ChannelID);
            }

            LOG0("Destroying event queue");
            evq_destroy(&c->EvtQ);

            // ADD — URID cleanup before slot is wiped:
            if (c->pWorkingURID) {
                FreeWorkingURID(c->pWorkingURID);
                c->pWorkingURID = NULL;
            }
            DeleteCriticalSection(&c->uridLock);
        }
    }

    if (g.DeviceOpen && g.pfClose) {
        LOG1("Closing J2534 device %lu", g.J2534DeviceID);
        g.pfClose(g.J2534DeviceID);
    }

    if (g.hJ2534Dll) {
        LOG0("Freeing J2534 DLL");
        FreeLibrary(g.hJ2534Dll);
    }

    char savedDllPath[MAX_PATH];
    strncpy(savedDllPath, g.DllPath, MAX_PATH - 1);

    LOG0("Clearing global state");
    LeaveCriticalSection(&g.GlobalLock);
    memset(&g, 0, sizeof(g));

    strncpy(g.DllPath, savedDllPath, MAX_PATH - 1);
    LOG1("Restored DLL path = %s", g.DllPath);

    InitializeCriticalSection(&g.GlobalLock);
    LOG0("Reinitialized GlobalLock");

    // ADD — re-init per-connection locks after memset wipes them:
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        InitializeCriticalSection(&g.Connections[i].uridLock);
        g.Connections[i].pWorkingURID = NULL;
    }
    LOG0("Reinitialized connection URID locks");

    LOG0("EXIT OK");
    return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUGetVersion
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetVersion(
	T_PDU_UINT32 hMod,
	PDU_VERSION_DATA *pVer)
{
	LOG1("ENTER hMod=%u pVer=%p", hMod, pVer);

	if (!g.Constructed) {
		LOG0("ERROR — API not constructed");
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	}

	if (!pVer) {
		LOG0("ERROR — pVer is NULL");
		return PDU_ERR_INVALID_PARAMETERS;
	}

	LOG0("Clearing version struct");
	memset(pVer, 0, sizeof(PDU_VERSION_DATA));

	LOG0("Filling standard version fields");
	pVer->MVCI_Part1StandardVersion = 33620224;
	pVer->MVCI_Part2StandardVersion = 33685504;

	LOG0("Filling hardware info");
	pVer->HwSerialNumber = 22124708;
	strncpy(pVer->HwName, "MDI", sizeof(pVer->HwName) - 1);
	pVer->HwVersion = 65536;
	pVer->HwDate = 637603840;
	pVer->HwInterface = 1;

	LOG0("Filling firmware info");
	strncpy(pVer->FwName, "MDI", sizeof(pVer->FwName) - 1);
	pVer->FwVersion = 134440821;
	pVer->FwDate = 788925952;

	LOG0("Filling vendor info");
	strncpy(pVer->VendorName, "Bosch", sizeof(pVer->VendorName) - 1);

	LOG0("Filling API software info");
	strncpy(pVer->PDUApiSwName, "MDI PDU-API", sizeof(pVer->PDUApiSwName) - 1);
	pVer->PDUApiSwVersion = 134440821;
	pVer->PDUApiSwDate = 788925952;

	LOG0("EXIT OK");
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUGetModuleIds — module type 28
* ====================================================================== */

PDU_API T_PDU_UINT32 PDU_CALL PDUGetModuleIds(PDU_MODULE_ITEM **pModuleIdList)
{
	logmsg("PDUGetModuleIds: ENTER");
	logmsg("PDUGetModuleIds: ENTER; gPdu.constructed=%d", gPdu.constructed);

	if (!pModuleIdList) return PDU_ERR_INVALID_PARAMETERS;

	// ONE PDU_MODULE_ITEM (the container)
	PDU_MODULE_ITEM *item = (PDU_MODULE_ITEM*)calloc(1, sizeof(PDU_MODULE_ITEM));
	if (!item) return PDU_ERR_FAILED;

	// SEPARATE array of PDU_MODULE_DATA (the actual entries)
	int n = 1; // start with 1, not 8
	PDU_MODULE_DATA *mods = (PDU_MODULE_DATA*)calloc(n, sizeof(PDU_MODULE_DATA));
	if (!mods) { free(item); return PDU_ERR_FAILED; }

	// Fill the container
	item->ItemType = PDU_IT_MODULE_ID;
	item->NumEntries = n;
	item->pModuleData = mods;

	// Fill the single module entry
	mods[0].ModuleTypeId = 28;
	mods[0].hMod = 1;
	mods[0].ModuleStatus = PDU_MODST_AVAIL; //0x8040
	mods[0].pVendorModuleName = "22124708";        // CHAR8*, plain ASCII
	mods[0].pVendorAdditionalInfo = "IFType='USB' IPAddress='192.168.171.2'"; // CHAR8*, plain ASCII

	logmsg("PDUGetModuleIds: mod[0] hMod=0x%08X TypeId=%u Status=%u",
		mods[0].hMod, mods[0].ModuleTypeId, mods[0].ModuleStatus);

	*pModuleIdList = item;
	logmsg("PDUGetModuleIds: EXIT OK");

	// Fire module status change event so Tech2Win knows module is ready
	return PDU_STATUS_NOERROR;
}
/* =========================================================================
* PDUGetResourceIds — Bosch-style, 2 parameters
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetResourceIds(
	T_PDU_UINT32  hMod,
	void         *pResourceIdData,
	T_PDU_UINT32 **ppList)
{
	UNUSED(hMod); UNUSED(pResourceIdData);

	logmsg("PDUGetResourceIds: ENTER hMod=%u ppList=%p", hMod, ppList);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	if (!ppList)
		return PDU_ERR_INVALID_PARAMETERS;

	/* Accept PDU_HANDLE_UNDEF as a valid hMod (some callers use it) */
	if (hMod != RUNTIME_ModuleID && hMod != PDU_HANDLE_UNDEF) {
		logmsg("PDUGetResourceIds: invalid hMod=%u", hMod);
		return PDU_ERR_INVALID_HANDLE;
	}

	/* Allocate list: [count, id0, id1, ..., PDU_ID_UNDEF] */
	T_PDU_UINT32 *list = (T_PDU_UINT32*)malloc((NUM_RESOURCES + 2) * sizeof(T_PDU_UINT32));
	if (!list) {
		logmsg("PDUGetResourceIds: malloc FAILED");
		return PDU_ERR_FCT_FAILED;
	}

	list[0] = NUM_RESOURCES;
	for (int i = 0; i < NUM_RESOURCES; i++)
		list[i + 1] = g_Resources[i].ResourceId;

	list[NUM_RESOURCES + 1] = PDU_ID_UNDEF;

	*ppList = list;
	logmsg("PDUGetResourceIds: EXIT count=%u first=%u", list[0], list[1]);
	return PDU_STATUS_NOERROR;
}


PDU_API T_PDU_UINT32 PDU_CALL PDUOpenResource(T_PDU_UINT32  hMod,
	T_PDU_UINT32  hRes,
	T_PDU_UINT32 *pHConn)
{
	logmsg("PDUOpenResource: CALLED");
	T_PDU_UINT32   conn;
	int            valid;
	int            i;
	PDU_CONN_STATE *c;

	UNUSED(hMod);
	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!pHConn)        return PDU_ERR_INVALID_PARAMETERS;

	/* Accept any resource that exists in g_Resources */
	valid = 0;
	for (i = 0; i < NUM_RESOURCES; i++) {
		if (g_Resources[i].ResourceId == hRes) {
			valid = 1;
			break;
		}
	}
	if (!valid) return PDU_ERR_INVALID_HANDLE;

	if (!AllocConn(&conn)) {
		logmsg("PDUOpenResource: AllocConn FAILED");
		return PDU_ERR_FCT_FAILED;
	}

	c = &g.Connections[conn];
	c->ResourceId = hRes;
	c->ModHandle = hMod;
	c->ChannelOpen = 0;   /* not connected yet */

	*pHConn = conn;
	logmsg("PDUOpenResource: EXIT conn=%u", conn);
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUModuleConnect — PassThruOpen to SM2
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUModuleConnect(T_PDU_UINT32 hMod)
{
	LOG1("PDUModuleConnect: CALLED hMod=%u", hMod);
	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (hMod != 1) return PDU_ERR_INVALID_HANDLE;
	g.DeviceOpen = 1;
	gPdu.modules[0].ModuleStatus = PDU_MODST_READY;
	LOG0("PDUModuleConnect: SUCCESS");
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUModuleDisconnect
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUModuleDisconnect(T_PDU_UINT32 hMod)
{
	logmsg("PDUModuleDisconnect: CALLED");
	UNUSED(hMod);
	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	g.DeviceOpen = 0;
	// DO NOT call g.pfClose here — device stays open until PDUDestruct
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUGetObjectId – global Bosch-style catalog
* ========================================================================= */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetObjectId(
	T_PDU_UINT32  type,
	const char   *pShortName,
	T_PDU_UINT32 *pId)
{
	logmsg("PDUGetObjectId: ENTER type=%u name=%s", type, pShortName ? pShortName : "(null)");

	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!pShortName || !pId) return PDU_ERR_INVALID_PARAMETERS;

	*pId = PDU_ID_UNDEF;

	switch (type) {

		/* =====================================================================
		* BUSTYPE (PDU_OBJT_BUSTYPE)
		* =================================================================== */
	case PDU_OBJT_BUSTYPE:
	{
		static const struct { const char *name; T_PDU_UINT32 id; } bt[] = {
			{ "SAE_J1850_VPW",          BUSTYPE_J1850_VPW },   /* real */
			{ "ISO_11898_2_DWCAN",      0x1001 },              /* stubs */
			{ "ISO_11898_3_DWFTCAN",    0x1002 },
			{ "ISO_9141_2_UART",        0x1003 },
			{ "ISO_14230_1_UART",       0x1004 },
			{ "SAE_J1939_11_DWCAN",     0x1005 },
			{ "SAE_J2411_SWCAN",        0x1006 },
			{ "SAE_J2740_UART",         0x1007 },
			{ "KW_UART",                0x1008 },
		};
		for (size_t i = 0; i < sizeof(bt) / sizeof(bt[0]); i++) {
			if (strcmp(pShortName, bt[i].name) == 0) {
				*pId = bt[i].id;
				break;
			}
		}
		break;
	}

	/* =====================================================================
	* PROTOCOL (PDU_OBJT_PROTOCOL)
	* =================================================================== */
	case PDU_OBJT_PROTOCOL:
	{
		static const struct { const char *name; T_PDU_UINT32 id; } proto[] = {
			{ "SAE_J2190_on_SAE_J1850_VPW",   PROTO_ID_J2190_J1850VPW },
			{ "ISO_15031_5_on_SAE_J1850_VPW", PROTO_ID_ISO15031_J1850VPW },
			{ "ISO_OBD_on_SAE_J1850",         PROTO_ID_ISOOB_J1850 },

			{ "SAE_J2190_on_ISO_9141_2",      0x2001 },
			{ "ISO_15031_5_on_ISO_15765_4",   0x2002 },
			{ "ISO_14230_3_on_ISO_14230_2",   0x2003 },
			{ "ISO_15765_3_on_ISO_15765_2",   0x2004 },
			{ "SAE_J1939_73_on_SAE_J1939_21", 0x2005 },
			{ "ISO_11898_RAW",                0x2006 },
			{ "XDE_5024_on_XDE_5024_UART",    0x2007 },
			{ "KW82_on_KW_UART",              0x2008 },
			{ "KW81_on_KW_UART",              0x2009 },
			{ "KW309_on_KW_UART",             0x200A },
		};
		for (size_t i = 0; i < sizeof(proto) / sizeof(proto[0]); i++) {
			if (strcmp(pShortName, proto[i].name) == 0) {
				*pId = proto[i].id;
				break;
			}
		}
		break;
	}

	/* =====================================================================
	* RESOURCE (PDU_OBJT_RESOURCE)
	* =================================================================== */
	case PDU_OBJT_RESOURCE:
		for (int i = 0; i < NUM_RESOURCES; i++) {
			if (strcmp(pShortName, g_Resources[i].ShortName) == 0) {
				*pId = g_Resources[i].ResourceId;
				break;
			}
		}
		break;

		/* =====================================================================
		* COMPARAM (PDU_OBJT_COMPARAM)
		* =================================================================== */
	case PDU_OBJT_COMPARAM:
	{
		static const struct { const char *name; T_PDU_UINT32 id; } cp[] = {

			/* BUSTYPE-level */
			{ "CP_Baudrate",                 1 },
			{ "CP_J1850IFRCtrl",             12 },

			/* Timing */
			{ "CP_P2Max",                    112 },
			{ "CP_P2Min",                    111 },
			{ "CP_P2Star",                   113 },
			{ "CP_P3Min",                    114 },
			{ "CP_P3Max",                    115 },
			{ "CP_P3Phys",                   116 },
			{ "CP_CyclicRespTimeout",        117 },

			/* Loopback / Indication */
			{ "CP_Loopback",                 118 },
			{ "CP_TransmitIndEnable",        119 },
			{ "CP_StartMsgIndEnable",        120 },

			/* Tester Present family */
			{ "CP_TesterPresentHandling",    121 },
			{ "CP_TesterPresentTime",        122 },
			{ "CP_TesterPresentMessage",     123 },
			{ "CP_TesterPresentAddrMode",    124 },
			{ "CP_TesterPresentFormatCtrl",  125 },
			{ "CP_TesterPresentReqRsp",      126 },
			{ "CP_TesterPresentSendType",    127 },
			{ "CP_TesterPresentExpPosResp",  128 },
			{ "CP_TesterPresentExpNegResp",  129 },

			/* RC21 / RC23 / RC78 */
			{ "CP_RC21Handling",             130 },
			{ "CP_RC21RequestTime",          131 },
			{ "CP_RC21CompletionTimeout",    132 },
			{ "CP_RC23Handling",             133 },
			{ "CP_RC23RequestTime",          134 },
			{ "CP_RC23CompletionTimeout",    135 },
			{ "CP_HeaderFormatJ1850",        136 },
			{ "CP_RC78Handling",             137 },
			{ "CP_RC78CompletionTimeout",    138 },

			/* ECU Comm / addressing */
			{ "CP_RCByteOffset",             139 },
			{ "CP_RepeatReqCountApp",        140 },
			{ "CP_RepeatReqCountTrans",      141 },
			{ "CP_RequestAddrMode",          142 },
			{ "CP_EcuRespSourceAddr",        143 },
			{ "CP_TesterSourceAddr",         144 },
			{ "CP_PhysReqFormatPrioType",    145 },
			{ "CP_PhysReqTargetAddr",        146 },
			{ "CP_PhysRespFormatPrioType",   147 },
			{ "CP_FuncReqFormatPrioType",    148 },
			{ "CP_FuncReqTargetAddr",        149 },
			{ "CP_FuncRespFormatPrioType",   150 },
			{ "CP_FuncRespTargetAddr",       151 },
			{ "CP_EnableConcatenation",      152 },
			{ "CP_FillerByte",               153 },
			{ "CP_FillerByteHandling",       154 },
			{ "CP_FillerByteLength",         155 },
			{ "CP_SuspendQueueOnError",      156 },
			{ "CP_EnablePerfTest",           157 },
			{ "CP_ChangeSpeedCtrl",          158 },
			{ "CP_ChangeSpeedRate",          159 },
			{ "CP_ChangeSpeedResCtrl",       160 },
			{ "CP_ChangeSpeedTxDelay",       161 },
			{ "CP_ChangeSpeedMessage",       162 },
			{ "CP_TerminationType",          163 },
			{ "CP_ByteCountOffset",          164 },
			{ "CP_MessageScheduler",         165 },

			/* Extra ones Tech2Win probes – stub IDs */
			{ "CP_5BaudAddressPhys",         0x3001 },
			{ "CP_5BaudMode",                0x3002 },
			{ "CP_As",                       0x3003 },
			{ "CP_Bs",                       0x3004 },
			{ "CP_HeaderFormatKW",           0x3005 },
			{ "CP_InitializationSettings",   0x3006 },
			{ "CP_P4Max",                    0x3007 },
			{ "CP_P4Min",                    0x3008 },
			{ "CP_TIdle",                    0x3009 },
			{ "CP_TInil",                    0x300A },
			{ "CP_TWup",                     0x300B },
			{ "CP_W1Max",                    0x300C },
			{ "CP_W1Min",                    0x300D },
			{ "CP_W2Max",                    0x300E },
			{ "CP_W2Min",                    0x300F },
			{ "CP_W3Max",                    0x3010 },
			{ "CP_W3Min",                    0x3011 },
			{ "CP_W4Max",                    0x3012 },
			{ "CP_W4Min",                    0x3013 },

			{ "CP_BitSamplePoint",           0x3014 },
			{ "CP_SamplesPerBit",            0x3015 },
			{ "CP_SyncJumpWidth",            0x3016 },
			{ "CP_K_L_LineInit",             0x3017 },

			{ "CP_CanFillerByte",            0x3018 },
			{ "CP_CanFillerByteHandling",    0x3019 },
			{ "CP_CanPhysReqExtAddr",        0x301A },
			{ "CP_CanPhysReqFormat",         0x301B },
			{ "CP_CanPhysReqId",             0x301C },
			{ "CP_CanRespUSDTExtAddr",       0x301D },
			{ "CP_CanRespUSDTFormat",        0x301E },
			{ "CP_CanRespUSDTId",            0x301F },
			{ "CP_CanRespUUDTExtAddr",       0x3020 },
			{ "CP_CanRespUUDTFormat",        0x3021 },
			{ "CP_CanRespUUDTId",            0x3022 },

			{ "CP_P3Min_J2740",              0x3023 },
			{ "CP_SetPollResponse",          0x3024 },
			{ "CP_SetRelinquishMastership",  0x3025 },
			{ "CP_SwCan_HighVoltage",        0x3026 },
		};

		for (size_t i = 0; i < sizeof(cp) / sizeof(cp[0]); i++) {
			if (strcmp(pShortName, cp[i].name) == 0) {
				*pId = cp[i].id;
				break;
			}
		}

		if (*pId == PDU_ID_UNDEF)
			*pId = 0x3FFF;   /* generic “supported” placeholder */

		break;
	}

	/* =====================================================================
	* IO_CTRL (PDU_OBJT_IO_CTRL)
	* =================================================================== */
	case PDU_OBJT_IO_CTRL:
	{
		static const struct { const char *name; T_PDU_UINT32 id; } io[] = {
			{ "PDU_IOCTL_RESET",                 PDU_IOCTL_RESET },
			{ "PDU_IOCTL_CLEAR_TX_QUEUE",        PDU_IOCTL_CLEAR_TX_QUEUE },
			{ "PDU_IOCTL_CLEAR_RX_QUEUE",        PDU_IOCTL_CLEAR_RX_QUEUE },
			{ "PDU_IOCTL_GENERIC",               PDU_IOCTL_GENERIC },
			{ "PDU_IOCTL_READ_VBATT",            PDU_IOCTL_READ_VBATT },
			{ "PDU_IOCTL_START_MSG_FILTER",      PDU_IOCTL_START_MSG_FILTER },
			{ "PDU_IOCTL_STOP_MSG_FILTER",       PDU_IOCTL_STOP_MSG_FILTER },
			{ "PDU_IOCTL_CLEAR_MSG_FILTER",      PDU_IOCTL_CLEAR_MSG_FILTER },

			{ "PDU_IOCTL_SET_PROG_VOLTAGE",      PDU_IOCTL_SET_PROG_VOLTAGE },
			{ "PDU_IOCTL_READ_PROG_VOLTAGE",     PDU_IOCTL_READ_PROG_VOLTAGE },
			{ "PDU_IOCTL_SET_BUFFER_SIZE",       PDU_IOCTL_SET_BUFFER_SIZE },
			{ "PDU_IOCTL_GET_CABLE_ID",          PDU_IOCTL_GET_CABLE_ID },
			{ "PDU_IOCTL_SET_EVENT_QUEUE_PROPS", PDU_IOCTL_SET_EVENT_QUEUE_PROPS },
			{ "PDU_IOCTL_SEND_BREAK",            PDU_IOCTL_SEND_BREAK },
			{ "PDU_IOCTL_READ_IGNITION_SENSE",   PDU_IOCTL_READ_IGNITION_SENSE },
			{ "PDU_IOCTL_CLEAR_TX_PENDING",      PDU_IOCTL_CLEAR_TX_PENDING },
		};
		for (size_t i = 0; i < sizeof(io) / sizeof(io[0]); i++) {
			if (strcmp(pShortName, io[i].name) == 0) {
				*pId = io[i].id;
				break;
			}
		}
		break;
	}

	/* =====================================================================
	* PINTYPE (PDU_OBJT_PINTYPE)
	* =================================================================== */
	case PDU_OBJT_PINTYPE:
	{
		static const struct { const char *name; T_PDU_UINT32 id; } pt[] = {
			{ "HI",     1 },
			{ "LOW",    2 },
			{ "K",      3 },
			{ "L",      4 },
			{ "PLUS",   7 },
			{ "SINGLE", 9 },
		};
		for (size_t i = 0; i < sizeof(pt) / sizeof(pt[0]); i++) {
			if (strcmp(pShortName, pt[i].name) == 0) {
				*pId = pt[i].id;
				break;
			}
		}
		break;
	}

	default:
		break;
	}

	logmsg("PDUGetObjectId: EXIT id=%u", *pId);
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUSetComParam – accept everything, store basic UINT32 values
* ========================================================================= */
PDU_API T_PDU_UINT32 PDU_CALL PDUSetComParam(
	T_PDU_UINT32   hMod,
	T_PDU_UINT32   hCLL,
	PDU_PARAM_ITEM *pParamItem)
{
	UNUSED(hMod);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	if (!pParamItem || !pParamItem->pComParamData)
		return PDU_ERR_INVALID_PARAMETERS;

	CP_STORE *store = GetCpStore(hCLL);
	if (!store)
		return PDU_ERR_INVALID_HANDLE;

	if (pParamItem->ComParamDataType != PDU_PT_UNUM32) {
		LOG1("PDUSetComParam: unsupported data type 0x%08X", pParamItem->ComParamDataType);
		return PDU_ERR_COMPARAM_NOT_SUPPORTED;
	}

	UNUM32 pid = pParamItem->ComParamId;
	UNUM32 val = *(UNUM32*)pParamItem->pComParamData;

	CpSet(store, pid, val);

	LOG1("PDUSetComParam: hCLL=%u ParamId=%u Val=%u", hCLL, pid, val);
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUGetComParam – return stored values or sane defaults
* ========================================================================= */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetComParam(
    T_PDU_UINT32    hMod,
    T_PDU_UINT32    hCLL,
    T_PDU_UINT32    ParamId,
    PDU_PARAM_ITEM **ppParamItem)
{
    UNUSED(hMod);

    if (!g.Constructed)
        return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

    if (!ppParamItem)
        return PDU_ERR_INVALID_PARAMETERS;

    CP_STORE *store = GetCpStore(hCLL);
    if (!store)
        return PDU_ERR_INVALID_HANDLE;

    UNUM32 val = 0;
    int found = CpGet(store, ParamId, &val);
    if (!found) {
        // For now: default 0. You can plug in spec defaults here later.
        val = 0;
    }

    PDU_PARAM_ITEM *item = (PDU_PARAM_ITEM*)calloc(1, sizeof(PDU_PARAM_ITEM));
    if (!item)
        return PDU_ERR_FCT_FAILED;

    UNUM32 *pVal = (UNUM32*)malloc(sizeof(UNUM32));
    if (!pVal) {
        free(item);
        return PDU_ERR_FCT_FAILED;
    }
    *pVal = val;

    item->ItemType         = PDU_IT_PARAM;
    item->ComParamId       = ParamId;
    item->ComParamDataType = PDU_PT_UNUM32;
    item->ComParamClass    = PDU_PC_PROTOCOL;  // or PDU_PC_BUSTYPE for CP_Baudrate if you care
    item->pComParamData    = pVal;

    *ppParamItem = item;

    LOG1("PDUGetComParam: hCLL=%u ParamId=%u Val=%u", hCLL, ParamId, val);
    return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUCreateComLogicalLink — PassThruConnect J1850VPW @ 10400 baud
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUCreateComLogicalLink(
	T_PDU_UINT32   hMod,
	PDU_RSC_DATA  *pRscData,
	T_PDU_UINT32   resourceId,
	void          *pCllTag,
	T_PDU_UINT32  *phCLL,
	PDU_FLAG_DATA *pCllCreateFlag)
{
	logmsg("PDUCreateComLogicalLink: CALLED");
	UNUSED(hMod);
	UNUSED(pCllTag);
	UNUSED(pCllCreateFlag);

	if (!g.Constructed) return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!g.DeviceOpen)  return PDU_ERR_MODULE_NOT_CONNECTED;
	if (!phCLL)         return PDU_ERR_INVALID_PARAMETERS;

	logmsg("PDUCreateComLogicalLink: resourceId=%u BusTypeId=%u (forcing accept)",
		resourceId,
		pRscData ? pRscData->BusTypeId : 0);

	// reuse existing channel if you want
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (g.Connections[i].InUse && g.Connections[i].ChannelOpen) {
			logmsg("PDUCreateComLogicalLink: reusing existing channel slot %d chanID=%lu",
				i, g.Connections[i].J2534ChannelID);
			*phCLL = i;              // <-- CRITICAL: return connection index
			return PDU_STATUS_NOERROR;
		}
	}

	T_PDU_UINT32 hConn;
	if (!AllocConn(&hConn)) return PDU_ERR_FCT_FAILED;

	PDU_CONN_STATE *c = &g.Connections[hConn];
	c->ResourceId = resourceId;
	c->ModHandle = hMod;

	unsigned long chanID = 0;
	logmsg("PDUCreateComLogicalLink: calling pfConnect proto=0x%lx baud=%lu",
		J2534_J1850VPW, BUSTYPE_J1850_VPW_BAUD);
	long ret = g.pfConnect(g.J2534DeviceID, J2534_J1850VPW, 0,
		BUSTYPE_J1850_VPW_BAUD, &chanID);
	logmsg("PDUCreateComLogicalLink: pfConnect returned %ld chanID=%lu", ret, chanID);

	if (ret != J2534_STATUS_NOERROR) {
		FreeConn(hConn);
		return PDU_ERR_FCT_FAILED;
	}

	c->J2534ChannelID = chanID;
	c->ChannelOpen = 1;

	*phCLL = hConn;                  // <-- THIS is what PDUSetComParam will see as hCLL

	logmsg("PDUCreateComLogicalLink: EXIT OK hCLL=%u chanID=%lu", hConn, chanID);
	return PDU_STATUS_NOERROR;
}


/* =========================================================================
* PDUDestroyComLogicalLink
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUDestroyComLogicalLink(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn)
{
	logmsg("PDUDestroyComLogicalLink: CALLED");
	UNUSED(hMod);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	// Accept undefined handles safely
	if (hConn == PDU_ID_UNDEF || hConn == 0xFFFFFFFF) {
		logmsg("PDUDestroyComLogicalLink: hConn is UNDEF, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	PDU_CONN_STATE *c = GetConn(hConn);

	// If no connection exists, return success (Tech2Win expects this)
	if (!c) {
		logmsg("PDUDestroyComLogicalLink: no such connection, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	// Normal cleanup path
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


PDU_API T_PDU_UINT32 PDU_CALL PDUConnect(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn)
{
	PDU_CONN_STATE *c;
	PDU_EVENT_ITEM *ev;
	DWORD *status;

	logmsg("PDUConnect: CALLED");

	UNUSED(hMod);

	if (!g.Constructed) {
		logmsg("PDUConnect: g.Constructed == 0");
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	}

	if (hConn == PDU_ID_UNDEF || hConn == 0xFFFFFFFF) {
		logmsg("PDUConnect: hConn is UNDEF, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	c = GetConn(hConn);
	if (!c) {
		logmsg("PDUConnect: no such connection, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	if (!c->ChannelOpen) {
		logmsg("PDUConnect: Channel not open yet, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	if (c->Connected) {
		logmsg("PDUConnect: already connected");
		return PDU_STATUS_NOERROR;
	}

	/* 1) Sync working -> active config */
	if (c->pWorkingURID) {
		logmsg("PDUConnect: syncing existing URID table");
		Cll_SyncURID(c);
	}
	else {
		logmsg("PDUConnect: no working URID table yet");
	}

	/* 2) Configure filtering only if client did NOT override via PDUIoCtl */
	// Always rebuild hardware filters for basic RX
	logmsg("PDUConnect: building hardware filters regardless");
	RebuildJ2534Filters(c);

	// THEN apply software override on top
	if (c->FilterActive) {
		logmsg("PDUConnect: software filters also active");
	}

	else {
		logmsg("PDUConnect: client filter override active, skipping default filter rebuild");
	}

	/* 3) Mark ONLINE + push event */
	c->Connected = 1;
	c->CllState = PDU_CLLST_ONLINE;

	ev = (PDU_EVENT_ITEM*)calloc(1, sizeof(PDU_EVENT_ITEM));
	status = (DWORD*)calloc(1, sizeof(DWORD));

	if (ev && status) {
		*status = PDU_CLLST_ONLINE;
		ev->ItemType = PDU_IT_STATUS;
		ev->hCoPrimitive = PDU_HANDLE_UNDEF;
		ev->Timestamp = GetTickCount();
		ev->pData = status;

		if (!evq_push(&c->EvtQ, ev)) {
			free(status);
			free(ev);
		}
	}
	else {
		if (status) free(status);
		if (ev) free(ev);
	}

	/* 4) Start RX thread */
	StartRxThread(c);

	logmsg("PDUConnect: EXIT OK");
	return PDU_STATUS_NOERROR;
}


PDU_API T_PDU_UINT32 PDU_CALL PDUDisconnect(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn)
{
	logmsg("PDUDisconnect: CALLED");
	UNUSED(hMod);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	// Accept undefined handles safely
	if (hConn == PDU_ID_UNDEF || hConn == 0xFFFFFFFF) {
		logmsg("PDUDisconnect: hConn is UNDEF, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	PDU_CONN_STATE *c = GetConn(hConn);

	// If no connection exists, return success (Tech2Win expects this)
	if (!c) {
		logmsg("PDUDisconnect: no such connection, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	// Normal cleanup path
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
PDU_API T_PDU_UINT32 PDU_CALL PDUStartComPrimitive(
	T_PDU_UINT32  hMod,
	T_PDU_UINT32  hConn,
	T_PDU_UINT32  CoPrimitiveType,
	T_PDU_UINT32  CoPrimitiveDataSize,
	T_PDU_UINT8  *pCoPrimitiveData,
	PDU_COP_CTRL_DATA *pCopCtrlData,
	PDU_ITEM      *pCopTag,
	T_PDU_UINT32  *pHCoPrimitive)
{
	logmsg("PDUStartComPrimitive: CALLED");

	UNUSED(hMod);
	UNUSED(pCopTag);

	if (!g.Constructed) {
		logmsg("PDUStartComPrimitive: g.Constructed == 0");
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	}

	PDU_CONN_STATE *c = GetConn(hConn);
	if (!c) {
		logmsg("PDUStartComPrimitive: invalid/no open channel (c=%p)", c);
		return PDU_ERR_MODULE_NOT_CONNECTED;
	}

	c->ResultCount = 0;

	if (pCopCtrlData && pCopCtrlData->NumReceiveCycles > 0) {
		c->ExpectedResults = pCopCtrlData->NumReceiveCycles;
	}
	else {
		c->ExpectedResults = 1;
	}

	T_PDU_INT32 NumReceiveCycles = 1;
	if (pCopCtrlData) {
		NumReceiveCycles = pCopCtrlData->NumReceiveCycles;
		logmsg("PDUStartComPrimitive: CoPType=0x%08lX NumSend=%d NumRecv=%d",
			CoPrimitiveType, pCopCtrlData->NumSendCycles, NumReceiveCycles);
	}

	static T_PDU_UINT32 s_NextCoPrim = 1;
	T_PDU_UINT32 hCoPrim = s_NextCoPrim++;
	if (s_NextCoPrim == PDU_HANDLE_UNDEF) s_NextCoPrim = 1;

	if (pHCoPrimitive) *pHCoPrimitive = hCoPrim;

	c->LastCoPrimHandle = hCoPrim;
	c->LastCoPrimTime = GetTickCount();

	if (!pCoPrimitiveData || CoPrimitiveDataSize == 0) {
		logmsg("PDUStartComPrimitive: receive-only primitive NumReceiveCycles=%d", NumReceiveCycles);

		if (NumReceiveCycles != -1) {
			PDU_EVENT_ITEM *evFin = (PDU_EVENT_ITEM*)calloc(1, sizeof(PDU_EVENT_ITEM));
			T_PDU_UINT32 *pStat = (T_PDU_UINT32*)calloc(1, sizeof(T_PDU_UINT32));
			*pStat = PDU_COPST_FINISHED;
			evFin->ItemType = PDU_IT_STATUS;
			evFin->hCoPrimitive = hCoPrim;
			evFin->pData = pStat;
			evq_push(&c->EvtQ, evFin);

			if (CoPrimitiveType == PDU_COPT_STARTCOMM) {
				PDU_EVENT_ITEM *evComm = (PDU_EVENT_ITEM*)calloc(1, sizeof(PDU_EVENT_ITEM));
				T_PDU_UINT32 *pComm = (T_PDU_UINT32*)calloc(1, sizeof(T_PDU_UINT32));
				*pComm = PDU_CLLST_COMM_STARTED;
				evComm->ItemType = PDU_IT_STATUS;
				evComm->hCoPrimitive = PDU_HANDLE_UNDEF;
				evComm->pData = pComm;
				evq_push(&c->EvtQ, evComm);
			}
		}
		else {
			logmsg("PDUStartComPrimitive: infinite receive primitive, NOT pushing FINISHED");
		}

		c->PrimitiveActive = 0;
		c->ExpectedResponseId = 0;

		return PDU_STATUS_NOERROR;
	}

	if (CoPrimitiveDataSize > J2534_MAX_DATA_SIZE) {
		logmsg("PDUStartComPrimitive: size %u > J2534_MAX_DATA_SIZE", CoPrimitiveDataSize);
		return PDU_ERR_INVALID_PARAMETERS;
	}

	PASSTHRU_MSG msg = { 0 };
	msg.ProtocolID = J2534_J1850VPW;
	msg.TxFlags = 0;
	msg.DataSize = CoPrimitiveDataSize;
	memcpy(msg.Data, pCoPrimitiveData, CoPrimitiveDataSize);

	c->PrimitiveActive = 0;
	c->ExpectedResponseId = 0;

	if (msg.DataSize >= 2) {
		uint8_t target = msg.Data[1];
		c->PrimitiveActive = 1;
		c->ExpectedResponseId = msg.Data[0];
		c->LastCoPrimHandle = hCoPrim;
		c->LastCoPrimTime = GetTickCount();

		logmsg("PDUStartComPrimitive: Expecting response header 0x%02X hCoPrim=%lu",
			c->ExpectedResponseId, hCoPrim);
	}

	unsigned long numMsgs = 1;
	logmsg("PDUStartComPrimitive: calling pfWriteMsgs chan=%u size=%u",
		c->J2534ChannelID, CoPrimitiveDataSize);

	logmsg("PDUStartComPrimitive: data= %02X %02X %02X %02X %02X %02X %02X %02X",
		msg.Data[0], msg.Data[1], msg.Data[2], msg.Data[3],
		msg.Data[4], msg.Data[5], msg.Data[6], msg.Data[7]);

	long ret = g.pfWriteMsgs(c->J2534ChannelID, &msg, &numMsgs, 500);
	logmsg("PDUStartComPrimitive: pfWriteMsgs ret=%ld numMsgs=%lu", ret, numMsgs);

	if (ret != J2534_STATUS_NOERROR) {
		c->PrimitiveActive = 0;
		c->ExpectedResponseId = 0;
		return PDU_ERR_FCT_FAILED;
	}

	if (CoPrimitiveType == PDU_COPT_STARTCOMM) {
		PDU_EVENT_ITEM *evComm = (PDU_EVENT_ITEM*)calloc(1, sizeof(PDU_EVENT_ITEM));
		T_PDU_UINT32 *pCommStatus = (T_PDU_UINT32*)calloc(1, sizeof(T_PDU_UINT32));
		*pCommStatus = PDU_CLLST_COMM_STARTED;
		evComm->ItemType = PDU_IT_STATUS;
		evComm->hCoPrimitive = PDU_HANDLE_UNDEF;
		evComm->pData = pCommStatus;
		evq_push(&c->EvtQ, evComm);
	}

	logmsg("PDUStartComPrimitive: EXIT OK hCoPrim=%lu active=%d expected=0x%02X",
		hCoPrim, c->PrimitiveActive, c->ExpectedResponseId);

	return PDU_STATUS_NOERROR;
}


/* =========================================================================
* PDUCancelComPrimitive
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUCancelComPrimitive(T_PDU_UINT32 hMod, T_PDU_UINT32 hConn, T_PDU_UINT32 hCoPrimitive)
{
	logmsg("PDUCancelComPrimitive: CALLED");
	UNUSED(hMod); UNUSED(hConn); UNUSED(hCoPrimitive);
	return PDU_STATUS_NOERROR;
}

/* =========================================================================
* PDUGetEventItem
* Tech2Win calls this ? DLL pops one item from queue populated by RX thread
*
* SM2 ? PassThruReadMsgs ? RxThread ? evq_push ? PDUGetEventItem ? Tech2Win
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetEventItem(T_PDU_UINT32 hMod,
	T_PDU_UINT32 hConn, PDU_EVENT_ITEM **ppEventItem)
{
	logmsg("PDUGetEventItem: ENTER hMod=%u hConn=%u", hMod, hConn);
	UNUSED(hMod);

	if (!g.Constructed)        return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!ppEventItem)          return PDU_ERR_INVALID_PARAMETERS;

	*ppEventItem = NULL;

	// Module-level event request
	if (hConn == PDU_HANDLE_UNDEF || hConn == 0xFFFFFFFF) {
		PDU_EVENT_ITEM *ev = evq_pop(&g_ModuleEvtQ);
		if (ev) {
			*ppEventItem = ev;
			logmsg("PDUGetEventItem: returning module event type=0x%04lX", ev->ItemType);
			return PDU_STATUS_NOERROR;
		}
		return PDU_ERR_EVENT_QUEUE_EMPTY;
	}

	// Connection-level event request
	PDU_CONN_STATE *c = GetConn(hConn);
	if (!c) return PDU_ERR_INVALID_PARAMETERS;

	PDU_EVENT_ITEM *ev = evq_pop(&c->EvtQ);
	if (ev) {
		*ppEventItem = ev;
		logmsg("PDUGetEventItem: returning event type=0x%04lX hConn=%u",
			ev->ItemType, hConn);
		return PDU_STATUS_NOERROR;
	}

	return PDU_ERR_EVENT_QUEUE_EMPTY;
}

PDU_API T_PDU_UINT32 PDU_CALL PDUSetUniqueRespIdTable(
    T_PDU_UINT32 hMod,
    T_PDU_UINT32 hConn,
    PDU_UNIQUE_RESP_ID_TABLE_ITEM *pURIDTable)
{
    logmsg("PDUSetUniqueRespIdTable: ENTER hMod=%u hConn=%u pURIDTable=%p",
        hMod, hConn, pURIDTable);

    UNUSED(hMod);

    if (!g.Constructed)
        return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

    if (hConn == PDU_ID_UNDEF || hConn == 0xFFFFFFFF)
        return PDU_ERR_INVALID_PARAMETERS;

    PDU_CONN_STATE *c = GetConn(hConn);
    if (!c)
        return PDU_ERR_MODULE_NOT_CONNECTED;

	FreeWorkingURID(c->pWorkingURID);
	c->pWorkingURID = NULL;

    if (!pURIDTable || pURIDTable->NumEntries == 0 || !pURIDTable->pUniqueData)
        return PDU_STATUS_NOERROR;

    c->pWorkingURID = (PDU_UNIQUE_RESP_ID_TABLE_ITEM*)calloc(1, sizeof(PDU_UNIQUE_RESP_ID_TABLE_ITEM));
    if (!c->pWorkingURID)
        return PDU_ERR_FCT_FAILED;

    c->pWorkingURID->ItemType = PDU_IT_UNIQUE_RESP_ID_TABLE;
    c->pWorkingURID->NumEntries = pURIDTable->NumEntries;

    c->pWorkingURID->pUniqueData =
        (PDU_ECU_UNIQUE_RESP_DATA*)calloc(c->pWorkingURID->NumEntries, sizeof(PDU_ECU_UNIQUE_RESP_DATA));
    if (!c->pWorkingURID->pUniqueData)
        goto fail;

    for (UNUM32 i = 0; i < c->pWorkingURID->NumEntries; i++) {
        PDU_ECU_UNIQUE_RESP_DATA *dst = &c->pWorkingURID->pUniqueData[i];
        PDU_ECU_UNIQUE_RESP_DATA *src = &pURIDTable->pUniqueData[i];

        dst->UniqueRespIdentifier = src->UniqueRespIdentifier;
        dst->NumParamItems = src->NumParamItems;

        if (dst->NumParamItems) {
            dst->pParams = (PDU_PARAM_ITEM*)calloc(dst->NumParamItems, sizeof(PDU_PARAM_ITEM));
            if (!dst->pParams)
                goto fail;

            for (UNUM32 p = 0; p < dst->NumParamItems; p++) {
                dst->pParams[p] = src->pParams[p];

                if (src->pParams[p].pComParamData) {
                    UNUM32 *val = (UNUM32*)calloc(1, sizeof(UNUM32));
                    if (!val)
                        goto fail;
                    *val = *(UNUM32*)src->pParams[p].pComParamData;
                    dst->pParams[p].pComParamData = val;
                }
            }
        }
    }
	Cll_SyncURID(c);
    return PDU_STATUS_NOERROR;

fail:
	FreeWorkingURID(c->pWorkingURID);
	c->pWorkingURID = NULL;
	return PDU_ERR_FCT_FAILED;
}

/* =========================================================================
* PDUSendReceive — minimal working implementation
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUSendReceive(
	T_PDU_UINT32 hMod,
	T_PDU_UINT32 hConn,
	T_PDU_UINT32 hCoP,
	PDU_DATA_ITEM *pTxData,
	T_PDU_UINT32 NumExpResp,
	PDU_EXP_RESP_DATA *pExpResp,
	T_PDU_UINT32 *pNumResp,
	PDU_RESULT_DATA **ppRespList)
{
	logmsg("PDUSendReceive: ENTER hConn=%u hCoP=%u NumExpResp=%u",
		hConn, hCoP, NumExpResp);

	UNUSED(hMod);
	UNUSED(pExpResp);   /* full OEM behavior later */
	UNUSED(hCoP);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	PDU_CONN_STATE *c = GetConn(hConn);
	if (!c)
		return PDU_ERR_MODULE_NOT_CONNECTED;

	if (!pTxData || !pTxData->pData)
		return PDU_ERR_INVALID_PARAMETERS;

	/* ------------------------------------------------------------
	* 1. Decode TX PDU_DATA_ITEM as IO_BYTEARRAY
	* ------------------------------------------------------------ */
	if (pTxData->ItemType != PDU_IT_IO_BYTEARRAY)
		return PDU_ERR_INVALID_PARAMETERS;

	PDU_IO_BYTEARRAY_DATA *txBa = (PDU_IO_BYTEARRAY_DATA*)pTxData->pData;
	if (!txBa || !txBa->pData || txBa->DataSize == 0)
		return PDU_ERR_INVALID_PARAMETERS;

	/* ------------------------------------------------------------
	* 2. Build J2534 TX frame
	* ------------------------------------------------------------ */
	PASSTHRU_MSG tx;
	memset(&tx, 0, sizeof(tx));

	tx.ProtocolID = J2534_J1850VPW;
	tx.DataSize = (unsigned long)txBa->DataSize;

	if (tx.DataSize > sizeof(tx.Data))
		tx.DataSize = sizeof(tx.Data);

	memcpy(tx.Data, txBa->pData, tx.DataSize);

	{
		unsigned long numTx = 1;
		long ret = g.pfWriteMsgs(c->J2534ChannelID, &tx, &numTx, 50);

		if (ret != J2534_STATUS_NOERROR) {
			logmsg("PDUSendReceive: pfWriteMsgs failed ret=%ld", ret);
			return PDU_ERR_FCT_FAILED;
		}
	}

	/* ------------------------------------------------------------
	* 3. Wait for responses
	* ------------------------------------------------------------ */
	DWORD start = GetTickCount();
	DWORD timeout = 200;   /* minimal VPW P2 timeout */

	PDU_RESULT_DATA *results = NULL;
	T_PDU_UINT32     count = 0;

	while (GetTickCount() - start < timeout) {

		PASSTHRU_MSG rx;
		memset(&rx, 0, sizeof(rx));

		{
			unsigned long n = 1;
			long r = g.pfReadMsgs(c->J2534ChannelID, &rx, &n, 10);

			if (r != J2534_STATUS_NOERROR || n == 0) {
				Sleep(2);
				continue;
			}
		}

		/* Drop echoes */
		if (rx.RxStatus & TX_MSG_TYPE)
			continue;

		/* Build PDU_RESULT_DATA */
		PDU_RESULT_DATA *res = (PDU_RESULT_DATA*)calloc(1, sizeof(PDU_RESULT_DATA));
		if (!res) {
			Sleep(2);
			continue;
		}

		res->NumDataBytes = rx.DataSize;
		if (rx.DataSize > 0) {
			res->pDataBytes = (T_PDU_UINT8*)calloc(1, rx.DataSize);
			if (!res->pDataBytes) {
				free(res);
				Sleep(2);
				continue;
			}
			memcpy(res->pDataBytes, rx.Data, rx.DataSize);
		}

		/* AcceptanceId (target) */
		if (rx.DataSize >= 2)
			res->AcceptanceId = rx.Data[1];

		/* UniqueRespIdentifier (source) */
		if (rx.DataSize >= 3)
			res->UniqueRespIdentifier = rx.Data[2];

		/* Apply URID + filter gating */
		if (!Cll_ResultAllowed(c, res)) {
			if (res->pDataBytes) free(res->pDataBytes);
			free(res);
			Sleep(2);
			continue;
		}

		/* Append to result list */
		{
			PDU_RESULT_DATA *tmp =
				(PDU_RESULT_DATA*)realloc(results, (count + 1) * sizeof(PDU_RESULT_DATA));
			if (!tmp) {
				if (res->pDataBytes) free(res->pDataBytes);
				free(res);
				Sleep(2);
				continue;
			}

			results = tmp;
			memcpy(&results[count], res, sizeof(PDU_RESULT_DATA));
			count++;

			free(res); /* shallow free; data already copied */
		}

		Sleep(2);
	}

	/* ------------------------------------------------------------
	* 4. Return results
	* ------------------------------------------------------------ */
	if (pNumResp)
		*pNumResp = count;

	if (ppRespList)
		*ppRespList = results;

	logmsg("PDUSendReceive: EXIT with %u responses", count);
	return PDU_STATUS_NOERROR;
}


/* =========================================================================
* PDUGetEventItems — drain entire queue into linked list
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetEventItems(void)
{
	return PDU_STATUS_NOERROR;
}


/* =========================================================================
* PDURegisterEventCallback
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDURegisterEventCallback(
	T_PDU_UINT32 hMod,
	T_PDU_UINT32 hCLL,
	T_PDU_CALLBACK cb
)
{
	logmsg("PDURegisterEventCallback: hMod=%u hCLL=%u cb=%p",
		hMod, hCLL, cb);

	/* For now we ignore hMod/hCLL and just store the function pointer.
	If you want to be fancy later, you can track per-module/CLL callbacks. */
	g.EventCallback = cb;
	g.pCallbackUserData = NULL;   /* could store pAPITag from PDUConstruct if desired */

	return PDU_STATUS_NOERROR;
}


/* =========================================================================
* PDUIoCtl — maps PDU IoCtl IDs 1-18 to J2534 ioctl calls
* ====================================================================== */
T_PDU_UINT32 PDU_CALL PDUIoCtl(
	T_PDU_UINT32  hMod,
	T_PDU_UINT32  hConn,
	T_PDU_UINT32  IoCtlCommandId,
	PDU_DATA_ITEM *pInputData,
	PDU_DATA_ITEM **ppOutputData)
{
	logmsg("PDUIoCtl: ENTER hMod=%u hConn=%u IoCtl=%u pIn=%p ppOut=%p",
		hMod, hConn, IoCtlCommandId, pInputData, ppOutputData);

	if (pInputData) {
		PDU_ITEM *item = (PDU_ITEM*)pInputData;
		logmsg("PDUIoCtl: pInputData->ItemType=0x%04lX", item->ItemType);
	}


	if (IoCtlCommandId == PDU_ID_UNDEF && hConn == PDU_HANDLE_UNDEF) {
		// Tech2Win's own wrapper returns 0x50 for this case per Ghidra analysis
		// Do NOT touch ppOutputData - Tech2Win doesn't dereference it on error return
		return PDU_ERR_INVALID_PARAMETERS;
	}

	UNUSED(hMod);

	if (!g.Constructed)
		return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;

	// *** KEY FIX: accept undefined handles safely ***
	if (hConn == PDU_ID_UNDEF || hConn == 0xFFFFFFFF) {
		logmsg("PDUIoCtl: hConn is UNDEF, returning NOERROR");
		return PDU_STATUS_NOERROR;
	}

	PDU_CONN_STATE *c = GetConn(hConn);
	unsigned long chanID =
		(c && c->J2534ChannelID != (unsigned long)-1) ? c->J2534ChannelID : 0;

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
		if (c) {
			if (c->pActiveFilters) {
				if (c->pActiveFilters->pFilterData)
					free(c->pActiveFilters->pFilterData);
				free(c->pActiveFilters);
				c->pActiveFilters = NULL;
			}
			c->FilterActive = 0;
			c->FilterOverrideByIoCtl = 1;
			logmsg("PDUIoCtl: CLEAR_MSG_FILTER - software filters disabled");
		}
		return PDU_STATUS_NOERROR;

	case PDU_IOCTL_SUSPEND_TX_QUEUE:
	case PDU_IOCTL_RESUME_TX_QUEUE:
		return PDU_STATUS_NOERROR;


	case PDU_IOCTL_READ_VBATT:
	{
		g.pfIoctl(g.J2534DeviceID, IOCTL_READ_VBATT, NULL, &sLong);

		if (ppOutputData) {
			PDU_DATA_ITEM         *item = (PDU_DATA_ITEM*)calloc(1, sizeof(PDU_DATA_ITEM));
			PDU_IO_BYTEARRAY_DATA *ba = (PDU_IO_BYTEARRAY_DATA*)calloc(1, sizeof(PDU_IO_BYTEARRAY_DATA));
			T_PDU_UINT32          *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));

			if (!item || !ba || !v) {
				free(item);
				free(ba);
				free(v);
				return PDU_ERR_FCT_FAILED;
			}

			*v = (T_PDU_UINT32)sLong.Value;

			ba->DataSize = sizeof(T_PDU_UINT32);
			ba->pData = (UNUM8*)v;

			item->ItemType = PDU_IT_IO_BYTEARRAY;
			item->pData = ba;

			*ppOutputData = item;
		}
		return PDU_STATUS_NOERROR;
	}


	case PDU_IOCTL_READ_PROG_VOLTAGE:
	{
		g.pfIoctl(g.J2534DeviceID, IOCTL_READ_PROG_VOLTAGE, NULL, &sLong);

		if (ppOutputData) {
			PDU_DATA_ITEM         *item = (PDU_DATA_ITEM*)calloc(1, sizeof(PDU_DATA_ITEM));
			PDU_IO_BYTEARRAY_DATA *ba = (PDU_IO_BYTEARRAY_DATA*)calloc(1, sizeof(PDU_IO_BYTEARRAY_DATA));
			T_PDU_UINT32          *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));

			if (!item || !ba || !v) {
				free(item);
				free(ba);
				free(v);
				return PDU_ERR_FCT_FAILED;
			}

			*v = (T_PDU_UINT32)sLong.Value;

			ba->DataSize = sizeof(T_PDU_UINT32);
			ba->pData = (UNUM8*)v;

			item->ItemType = PDU_IT_IO_BYTEARRAY;
			item->pData = ba;

			*ppOutputData = item;
		}
		return PDU_STATUS_NOERROR;
	}


	case PDU_IOCTL_SET_PROG_VOLTAGE:
	{
		if (pInputData && pInputData->ItemType == PDU_IT_IO_BYTEARRAY && pInputData->pData) {
			PDU_IO_BYTEARRAY_DATA *ba = (PDU_IO_BYTEARRAY_DATA*)pInputData->pData;
			if (ba->pData && ba->DataSize >= sizeof(long)) {
				long val = 0;
				memcpy(&val, ba->pData, sizeof(long));
				sLong.Value = val;
				g.pfIoctl(g.J2534DeviceID, IOCTL_SET_CONFIG, &sLong, NULL);
			}
		}
		return PDU_STATUS_NOERROR;
	}


	case PDU_IOCTL_READ_IGNITION_SENSE:
	{
		if (ppOutputData) {
			PDU_DATA_ITEM         *item = (PDU_DATA_ITEM*)calloc(1, sizeof(PDU_DATA_ITEM));
			PDU_IO_BYTEARRAY_DATA *ba = (PDU_IO_BYTEARRAY_DATA*)calloc(1, sizeof(PDU_IO_BYTEARRAY_DATA));
			T_PDU_UINT32          *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));

			if (!item || !ba || !v) {
				free(item);
				free(ba);
				free(v);
				return PDU_ERR_FCT_FAILED;
			}

			*v = 1; /* Ignition ON */

			ba->DataSize = sizeof(T_PDU_UINT32);
			ba->pData = (UNUM8*)v;

			item->ItemType = PDU_IT_IO_BYTEARRAY;
			item->pData = ba;

			*ppOutputData = item;
		}
		return PDU_STATUS_NOERROR;
	}


	case PDU_IOCTL_GET_CABLE_ID:
	{
		if (ppOutputData) {
			PDU_DATA_ITEM         *item = (PDU_DATA_ITEM*)calloc(1, sizeof(PDU_DATA_ITEM));
			PDU_IO_BYTEARRAY_DATA *ba = (PDU_IO_BYTEARRAY_DATA*)calloc(1, sizeof(PDU_IO_BYTEARRAY_DATA));
			T_PDU_UINT32          *v = (T_PDU_UINT32*)malloc(sizeof(T_PDU_UINT32));

			if (!item || !ba || !v) {
				free(item);
				free(ba);
				free(v);
				return PDU_ERR_FCT_FAILED;
			}

			*v = (T_PDU_UINT32)g.J2534DeviceID;

			ba->DataSize = sizeof(T_PDU_UINT32);
			ba->pData = (UNUM8*)v;

			item->ItemType = PDU_IT_IO_BYTEARRAY;
			item->pData = ba;

			*ppOutputData = item;
		}
		return PDU_STATUS_NOERROR;
	}


	case PDU_IOCTL_START_MSG_FILTER:
	{
		if (!pInputData || pInputData->ItemType != PDU_IT_IO_FILTER || !pInputData->pData)
			return PDU_ERR_INVALID_PARAMETERS;

		PDU_IO_FILTER_LIST *list = (PDU_IO_FILTER_LIST*)pInputData->pData;
		if (!list || !list->NumFilterEntries || !list->pFilterData)
			return PDU_ERR_INVALID_PARAMETERS;

		if (c) {
			// Free old filters
			if (c->pActiveFilters) {
				if (c->pActiveFilters->pFilterData)
					free(c->pActiveFilters->pFilterData);
				free(c->pActiveFilters);
			}

			// Deep copy new filters
			c->pActiveFilters = calloc(1, sizeof(PDU_IO_FILTER_LIST));
			if (c->pActiveFilters) {
				c->pActiveFilters->NumFilterEntries = list->NumFilterEntries;
				c->pActiveFilters->pFilterData = calloc(list->NumFilterEntries, sizeof(PDU_IO_FILTER_DATA));
				if (c->pActiveFilters->pFilterData) {
					memcpy(c->pActiveFilters->pFilterData, list->pFilterData,
						list->NumFilterEntries * sizeof(PDU_IO_FILTER_DATA));
				}
				c->FilterActive = 1;
				c->FilterOverrideByIoCtl = 1;
			}
			logmsg("PDUIoCtl: START_MSG_FILTER stored %u filters (software only)", list->NumFilterEntries);
		}
		return PDU_STATUS_NOERROR;
	}

	case PDU_IOCTL_STOP_MSG_FILTER:
		logmsg("PDUIoCtl: STOP_MSG_FILTER ignored (software filtering)");
		if (c) c->FilterOverrideByIoCtl = 1;
		return PDU_STATUS_NOERROR;

	case PDU_IOCTL_SET_BUFFER_SIZE:
	case PDU_IOCTL_SET_EVENT_QUEUE_PROPS:
	case PDU_IOCTL_SEND_BREAK:

	case PDU_IOCTL_GENERIC:
		return PDU_STATUS_NOERROR;

	case 0xFFFFFFFE:
		if (pInputData) {
			unsigned char *raw = (unsigned char*)pInputData;
			logmsg("PDUIoCtl: raw bytes: %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X",
				raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
				raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
		}
		return PDU_STATUS_NOERROR;


	default:
		logmsg("PDUIoCtl: unknown IoCtlCommandId=0x%08X, returning NOERROR", IoCtlCommandId);
		return PDU_STATUS_NOERROR;
	}
}

/* =========================================================================
* PDUGetLastError
* ====================================================================== */
PDU_API T_PDU_UINT32 PDU_CALL PDUGetLastError(T_PDU_UINT32  hMod, T_PDU_UINT32  hConn,
	T_PDU_UINT32 *pErrCode, T_PDU_UINT32 *pErrEvtCount,
	T_PDU_UINT32 *pErrDestHandle, T_PDU_UINT32 param6)
{
	logmsg("PDUGetLastError: CALLED");
	UNUSED(hMod); UNUSED(hConn); UNUSED(param6);
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
PDU_API T_PDU_UINT32 PDU_CALL PDUGetTimestamp(T_PDU_UINT32 hMod, T_PDU_UINT32 *pTimestamp)
{
	UNUSED(hMod);
	if (pTimestamp) *pTimestamp = GetTickCount();
	return PDU_STATUS_NOERROR;
}

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
	logmsg("PDUGetStatus: CALLED hMod=%lu hConn=%lu hCoPrimitive=%lu pStatus=%p",
		hMod, hConn, hCoPrimitive, pStatus);

	UNUSED(hMod);
	UNUSED(hConn);
	UNUSED(hCoPrimitive);
	UNUSED(pStatus);
	UNUSED(param5);
	UNUSED(param6);

	if (pStatus) {
		if (hCoPrimitive == PDU_HANDLE_UNDEF || hCoPrimitive == 0xFFFFFFFF) {
			*pStatus = 0x8052;  // PDU_CLLST_COMM_STARTED
		}
		else {
			PDU_CONN_STATE *c = GetConn(hConn);
			if (c && c->LastCoPrimHandle == hCoPrimitive) {
				DWORD elapsed = GetTickCount() - c->LastCoPrimTime;
				if (elapsed > 150) {
					// Timed out — push finished event and report done
					PDU_EVENT_ITEM *evFin = (PDU_EVENT_ITEM*)calloc(1, sizeof(PDU_EVENT_ITEM));
					T_PDU_UINT32 *pStat = (T_PDU_UINT32*)calloc(1, sizeof(T_PDU_UINT32));
					*pStat = PDU_COPST_FINISHED;  // PDU_COPST_FINISHED
					evFin->ItemType = PDU_IT_STATUS;
					evFin->hCoPrimitive = hCoPrimitive;
					evFin->pData = pStat;
					evq_push(&c->EvtQ, evFin);
					*pStatus = PDU_COPST_FINISHED;  // FINISHED
				}
				else {
					*pStatus = 0x8011;  // EXECUTING
				}
			}
			else {
				*pStatus = PDU_COPST_FINISHED;  // FINISHED
			}
		}
	}
	return PDU_STATUS_NOERROR;

}

/* =========================================================================
* PDUDestroyItem / PDUDestroyItems — free memory returned to Tech2Win
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
			free(r);
		}
		free(p);
	}
	else if (p->ItemType == PDU_IT_STATUS) {
		PDU_EVENT_ITEM *ev = (PDU_EVENT_ITEM*)p;
		if (ev->pData) free(ev->pData);
		free(p);
	}
	else if (p->ItemType == PDU_IT_MODULE_ID) {
		PDU_MODULE_ITEM *mi = (PDU_MODULE_ITEM*)p;
		if (mi->pModuleData) free(mi->pModuleData);
		free(p);
	}
	else if (p->ItemType == PDU_IT_UNIQUE_RESP_ID_TABLE) {  // ADD THIS
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
	logmsg("PDUGetUniqueRespIdTable: CALLED:");
	if (!g.Constructed)      return PDU_ERR_PDUAPI_NOT_CONSTRUCTED;
	if (!pUniqueRespIdTable) return PDU_ERR_INVALID_PARAMETERS;

	PDU_CONN_STATE *c = GetConn(hCLL);
	if (!c) return PDU_ERR_INVALID_HANDLE;

	PDU_UNIQUE_RESP_ID_TABLE_ITEM *tbl =
		(PDU_UNIQUE_RESP_ID_TABLE_ITEM*)calloc(1, sizeof(PDU_UNIQUE_RESP_ID_TABLE_ITEM));
	if (!tbl) return PDU_ERR_FCT_FAILED;

	tbl->ItemType = PDU_IT_UNIQUE_RESP_ID_TABLE;
	tbl->NumEntries = 1;

	PDU_ECU_UNIQUE_RESP_DATA *entry =
		(PDU_ECU_UNIQUE_RESP_DATA*)calloc(1, sizeof(PDU_ECU_UNIQUE_RESP_DATA));
	if (!entry) { free(tbl); return PDU_ERR_FCT_FAILED; }

	entry->UniqueRespIdentifier = PDU_ID_UNDEF;
	entry->NumParamItems = 3;

	PDU_PARAM_ITEM *params =
		(PDU_PARAM_ITEM*)calloc(3, sizeof(PDU_PARAM_ITEM));
	if (!params) { free(entry); free(tbl); return PDU_ERR_FCT_FAILED; }

	params[0].ComParamId = CP_EcuRespSourceAddress;
	params[0].ComParamClass = PDU_PC_UNIQUE_ID;
	params[0].ComParamDataType = PDU_PT_UNUM32;
	params[0].pComParamData = calloc(1, sizeof(UNUM32));
	if (!params[0].pComParamData) { free(params); free(entry); free(tbl); return PDU_ERR_FCT_FAILED; }
	*(UNUM32*)params[0].pComParamData = 0x10;

	params[1].ComParamId = CP_FuncRespFormatPriorityType;
	params[1].ComParamClass = PDU_PC_UNIQUE_ID;
	params[1].ComParamDataType = PDU_PT_UNUM32;
	params[1].pComParamData = calloc(1, sizeof(UNUM32));
	if (!params[1].pComParamData) {
		free(params[0].pComParamData);
		free(params);
		free(entry);
		free(tbl);
		return PDU_ERR_FCT_FAILED;
	}
	*(UNUM32*)params[1].pComParamData = 0x48;

	params[2].ComParamId = CP_FuncRespTargetAddr;
	params[2].ComParamClass = PDU_PC_UNIQUE_ID;
	params[2].ComParamDataType = PDU_PT_UNUM32;
	params[2].pComParamData = calloc(1, sizeof(UNUM32));
	if (!params[2].pComParamData) {
		free(params[1].pComParamData);
		free(params[0].pComParamData);
		free(params);
		free(entry);
		free(tbl);
		return PDU_ERR_FCT_FAILED;
	}
	*(UNUM32*)params[2].pComParamData = 0x6B;



	entry->pParams = params;
	tbl->pUniqueData = entry;

	*pUniqueRespIdTable = tbl;
	logmsg("PDUGetUniqueRespIdTable: returned default VPW template");
	return PDU_STATUS_NOERROR;
}


PDU_API T_PDU_UINT32 PDU_CALL PDUDestroyItems(void)
{

	return PDU_STATUS_NOERROR;
}


// --- VCI STUBS ---------------------------------------------------------

// If you don't have T_PDU_UINT32 typedef'd, this is what Bosch uses:
typedef unsigned long VCI_UINT32;

// If T_PDU_UINT32 already exists, just use that instead of VCI_UINT32 below.

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