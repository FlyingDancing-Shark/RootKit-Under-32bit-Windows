#include <ntddk.h>
#include "datatype.h"
#include "dbgmsg.h"
#include "ctrlcode.h"
#include "device.h"

#define ETHREAD_OFFSET_SERVICE_TABLE				0xbc
//ʹ�� build  /D /g /b /B /e /F /S /s /$ /why /v /w /y  ������������Դ�ļ�
//���Բ��Ҳ�ѧϰ WRK Դ�������ж� Mdl  API ��ص���ȷ�÷�

/*
MDL �ṹ������ WRK Դ��� ntosdef.h ͷ�ļ���

Define a Memory Descriptor List (MDL)

An MDL describes pages in a virtual buffer in terms of physical pages.  The
pages associated with the buffer are described in an array that is allocated
just after the MDL header structure itself.

One simply calculates the base of the array by adding one to the base
MDL pointer:

    Pages = (PPFN_NUMBER) (Mdl + 1);

Notice that while in the context of the subject thread, the base virtual
address of a buffer mapped by an MDL may be referenced using the following:

    Mdl->StartVa | Mdl->ByteOffset
   */

PMDL  mdl_ptr;

//MmProbeAndLockPages() �ڷ���ǰ��Ѵ������� MDL ָ���趨�� NULL�������ڵ�����ǰ���б��ݣ�
PMDL  backup_mdl_ptr;

// һöָ�룬�����洢һ�������ַ���õ�ַ���������� PFN_NUMBER�������� MDL �ṹ���������ҳ��ţ�
PPFN_NUMBER  pfn_array_follow_mdl;

short  mdl_header_length = sizeof(MDL);

DWORD*  mapped_ki_service_table;

/* os_SSDT_ptr ��ʵ���� KTHREAD �ṹ�� ServiceTable �ֶΣ���ָ�� SSDT �ĵ�ַ���洢��ȫ�ֱ��� os_SSDT �У���
��Ҫ��� SSDT �ĵ�ַ���� kd.exe �������� dd ת�� os_SSDT_ptr��������Ӧ�þ��� SSDT 
�ĵ�ַ������ dt nt!*DescriptorTable* -v ��������ĵ�һ���ַƥ�䣬����������� dd ת�� SSDT����õ�
ϵͳ�����ĵ�ַ���洢��ȫ�ֱ��� os_ki_service_table �У��������ں˱��� KiServiceTable ��ֵ��ͬ������ dd/dps ת��
KiServiceTable/os_ki_service_table����õ����е�ϵͳ�������̵ĵ�ַ/��������

kd.exe ���
SSDT_ptr��KTHREAD.ServiceTable ��Ա��ַ����0x
SSDT��0x
KiServiceTable��0x

������ӡ���
os_SSDT_ptr��0x �����������еĵ�ǰ�̵߳� KTHREAD.ServiceTable ��Ա��ַ��ͬ��ָ�� ��
os_SSDT��0x
mapped_addr��Mdl �� os_SSDT_ptr ӳ�䵽�ĵ�ַ����
os_ki_service_table��

ͨ�� kd.exe ���ں˵�ַ��չָ�� !cmkd.kvas ������֪��KTHREAD.ServiceTable ��Աλ�ڲ��ɻ�ҳ�أ�
ϵͳ������������SSDT�����Լ�ϵͳ��������ָ����� KiServiceTable��λ�� BootLoaded ���͵��ں˿ռ䣨��Ӧ��ö������Ϊ 
MiVaBootLoaded����ϵͳ��ʼ���׶Σ�������Ȩת���ں�ʱ������� winload.exe ӳ�䵽�����ں˿ռ䣬
����Ϊ��Ҫ�ڴ����ں˿ռ��й��� SSDT �� KiServiceTable������ͼ�д�����

MDL �� KTHREAD.ServiceTable ��Ա��ָ�� SSDT��ӳ�䵽���ں˿ռ����� SystemPte����ϵͳҳ����Ŀ�������ں˿ռ��ж�����;��
�����ṩ�� MDL ���� SSDT/KiServiceTable ӳ�䵽�˴���
ϵͳҳ����Ŀ��PTEs���ں˿ռ䣬���ڶ�̬��ӳ��ϵͳҳ�棬���� I/O �ռ䣬�ں�ջ���Լ�ӳ���ڴ��������б�MDLs����
ϵͳ PTE �ķ����߳��˸���ִ����/�ں�����⣬������һЩ���ص��ں˿ռ���豸��������������ϵͳ�Դ��ģ�Ҳ�е�������Ӳ����Ӧ�̿����ģ�
����������ϵͳ PTE �����з����ڴ��Ŀ�Ķ�����ӳ����ͼ��MDLs���ڴ��������б����������ڴ�ӳ�䣬��������ӳ��
�ں�ջ�� I/O ӳ�����صġ�
���磬MiFindContiguousMemory() -> MmMapIoSpace() -> MiInsertIoSpaceMap -> ExAllocatePoolWithTag() -> 
MiAllocatePoolPages() -> MiAllocatePagedPoolPages() -> MiObtainSystemVa()
�ɴ˿�֪��������ֱ�ӻ��Ǽ�ӵ��� MiObtainSystemVa() ������ϵͳ PTE ��������ڴ棬���ᱻ���ټ�¼����
������ϵͳ PTE �����߸��ٺ󣩡�



*/

void**  os_SSDT_ptr;
//PVOID  os_SSDT_ptr;

 DWORD*  os_SSDT;
 //DWORD  os_SSDT;

 DWORD  os_ki_service_table;


 typedef NTSTATUS(*OriginalSystemServicePtr)
(
	HANDLE PortHandle
);

 OriginalSystemServicePtr  ori_sys_service_ptr;


 NTSTATUS our_hooking_routine(HANDLE PortHandle) 
 {
		
	 return (ori_sys_service_ptr(PortHandle));
 
 }


PVOID  MapMdl(PMDL  mdl_pointer, PVOID  VirtualAddress, ULONG  Length);
void  UnMapMdl(PMDL  mdl_pointer, PVOID  baseaddr);

//��̬ж�غ�dps ת�� mapped_ki_service_table ���������Ӧ�ò���ϵͳ���������� 
VOID Unload(PDRIVER_OBJECT driver)
{

	DBG_TRACE("OnUnload", "ж��ǰ����ȡ�� MDL �� KiServiceTable ��ӳ��");
	UnMapMdl(mdl_ptr, mapped_ki_service_table);
	DBG_TRACE("OnUnload",  "UseMdlMappingSSDT.sys ��ж��");
	return;

}


NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path)
{
	BYTE*   currentETHREADpointer = NULL;
	

	driver->DriverUnload = Unload;
	currentETHREADpointer = (UCHAR*)PsGetCurrentThread();

	 os_SSDT_ptr = (void**)(currentETHREADpointer + ETHREAD_OFFSET_SERVICE_TABLE);
	 // os_SSDT_ptr = (void*)(currentETHREADpointer + ETHREAD_OFFSET_SERVICE_TABLE);

	 os_SSDT = *(DWORD**)os_SSDT_ptr;
	 // os_SSDT = *(DWORD*)os_SSDT_ptr;

	os_ki_service_table = *(DWORD*)os_SSDT;

	//��ε��ûᵼ�� bugcheck code Ϊ 0x000000BE���༴������ֻ�����ں˵�ַд�룬��Ϊԭʼ�� KiServiceTable ����ֻ����
	//RtlFillMemory((DWORD*)os_ki_service_table, 0x4, (UCHAR)'A');

	//������õĵڶ�������������ǰ���ȡ������ SSDT ָ�룬SSDT������ KiServiceTable����������Ӧ��ʵ��
	// �˴��� KiServiceTable���Լ�ϵͳ����ָ����� os_ki_service_table �������棬ӳ�䵽��һ���ں˵�ַ��
	mapped_ki_service_table = MapMdl(mdl_ptr, (PVOID)os_ki_service_table,  0x191 * 4);

	if (mapped_ki_service_table == NULL) {

		DBG_TRACE("Driver Entry", ".........�޷����� MDL ������ OS �� SSDT��������ӳ�䵽��һ���ں˵�ַ����ҹ����޸�.......");
	}

	
	DbgPrint("���ǰ�ԭʼ�� OS ϵͳ����ָ�����дȨ��ӳ�䵽�����ں˿ռ�Ϊ:   %p\r\n", mapped_ki_service_table);
	DbgPrint("����������ں˵�ַ��Ӧ�þ��Ǳ��еĵ�һ��ϵͳ����ĵ�ַ�������õ��������� !dps ��������Ƿ�Ϊͬһ�ŵ��ñ�:   %p\r\n", *mapped_ki_service_table);

		
//0x39 ��ϵͳ����Ϊ nt!NtCompleteConnectPort() ����Ϊ��ֻ��һ���������������ĵ����ģ����Խ��� hook ���ض���

	ori_sys_service_ptr = mapped_ki_service_table[0x39];

	mapped_ki_service_table[0x39] = our_hooking_routine;

	DbgPrint("���ǰ� 0x39 ��ϵͳ����ҹ�Ϊ:   %p\r\n", mapped_ki_service_table[0x39]);

	/*
	//RtlFillMemory(mapped_ki_service_table, 0x4, (UCHAR)'S');
	//DbgPrint("���� RtlFillMemory() �ѵ�һ��ϵͳ�������ĵ�ַ���Ϊȫ��Z��:   %p\r\n", *mapped_ki_service_table);
	*/


	return STATUS_SUCCESS;
}



PVOID  MapMdl(PMDL  mdl_pointer,  PVOID  VirtualAddress,  ULONG  Length) {

		PVOID  mapped_addr;
		//PVOID  mapped_addr2;
		// PVOID  mapped_addr3;

		// ��ӡ os_SSDT_ptr ����KHTREAD �� ServiceTable �ֶΣ��ĵ�ַ
		DbgPrint(" _KTHREAD.ServiceTable ����ĵ�ַ:   %p\r\n", &os_SSDT_ptr);
		DbgPrint(" ServiceTable ָ��:   %p\r\n", os_SSDT_ptr);
		DbgPrint(" ServiceTable ��ָ��������:   %p\r\n", *os_SSDT_ptr);
		DbgPrint(" SSDT���༴ nt!KeServiceDescriptorTable ��ַ���� ServiceTable ��ָ������һ��: %p\r\n", os_SSDT);

	// ��ӡϵͳ��������ָ���ĵ�ַ������ͨ�� MDL + MmProbeAndLockPages()����ϵͳ��������ָ����Կ�д����ʽӳ�䵽
	//SystemPTE ���͵��ں˿ռ䣬Ȼ���Զ�ӳ�䵽�ĵ�ִַ�� RtlZeroMemory() ֮��Ĳ��������Ƿ��ı�ԭʼ�� KiServiceTable ��
	// �������� !pte ������֤ʵ MDL + MmProbeAndLockPages() ӳ�䵽�������ַ��Ӧ������ҳΪ��д
	// ע�⣬Ӧ�ö� KiServiceTable ֤ʵ����Ϊ SSDT ��ԭʼ״̬���ǿ�д��
		DbgPrint(" nt!KeServiceDescriptorTable ��ָ��������:   %X\r\n", *os_SSDT);
		DbgPrint(" KiServiceTable ��ַ��������һ��:   %X\r\n", os_ki_service_table);
		DBG_TRACE("MapMdl", ".......���е�ϵͳ�����ַ����ͨ�� dps ת�� os_ki_service_table �鿴��..........r\n");

	// �����ȷ��Ҫӳ��Ļ������ڷǻ�ҳ���У���������ú���� MmProbeAndLockPages()��Ҳ���ò��� try......except �߼���

		try {

			mdl_pointer = IoAllocateMdl(VirtualAddress, 0x191 * 4, FALSE, FALSE, NULL);

			if (mdl_pointer == NULL) {

				DBG_TRACE("MapMdl", ".........�޷�����һ�� MDL ������ԭʼ�� KiServiceTable ��..........\r\n");
				return  NULL;
			}

			DbgPrint("����� MDL ָ������ĵ�ַ:  %p ������ dd ת�������еĵ�ַ\r\n", &mdl_pointer);
			DbgPrint("����� MDL ָ��ָ��һ�� _MDL �ĵ�ַ:   %p���� dd %p �����һ�£�����������ԭʼ�� KiServiceTable\r\n", mdl_pointer, &mdl_pointer);

			//�ѷ���� MDL ָ�뱸����������Ϊ MmGetSystemAddressForMdlSafe() ���û�Ѵ������� MDL ָ��ָ������ϵͳ����
			backup_mdl_ptr = mdl_pointer;

			// �������õ������ϵ���Ϊ�˹۲����ǰ��� _MDL.MdlFlags ��α仯
			__asm { 

				int 3;
			}

			if (mdl_pointer->MdlFlags & MDL_ALLOCATED_FIXED_SIZE)
			{
				DBG_TRACE("MapMdl", ".....IoAllocateMdl() ����� MDL �ṹ�й̶���С��MDL_ALLOCATED_FIXED_SIZE��........\r\n");
			}

			MmProbeAndLockPages(mdl_pointer, KernelMode, IoWriteAccess);

			__asm {

				int 3;
			}

			if ((mdl_pointer->MdlFlags & MDL_ALLOCATED_FIXED_SIZE) &&
				(mdl_pointer->MdlFlags & MDL_WRITE_OPERATION) &&
				(mdl_pointer->MdlFlags & MDL_PAGES_LOCKED))
			{
				DBG_TRACE("MapMdl", " MmProbeAndLockPages() ��дȨ�ޣ�MDL_WRITE_OPERATION���� MDL ������ԭʼ KiServiceTable ����ҳ�������������ڴ��У�MDL_PAGES_LOCKED��\r\n");
			}
			
			/*ǰ��ֱ�����һ���ϵ㣬��֤ MmGetSystemAddressForMdlSafe() �Ƿ�� MDL �ͷ��ˡ�������
			����֤��MmGetSystemAddressForMdlSafe() ��� MDL ָ������ϵͳ���ݣ������ͷ���ָ��� _MDL �ṹ����� MmGetSystemAddressForMdlSafe()
			���ú��޷�������� MDL ָ���Ƿ�Ϊ�������� _MDL �ṹ�Ƿ��ͷ�*/

			mapped_addr = MmGetSystemAddressForMdlSafe(mdl_pointer, NormalPagePriority);

			// �˴�˳��۲� _MDL.MdlFlags �ı仯
			__asm {

				int 3;
			}

			if (
				(mdl_pointer->MdlFlags & MDL_ALLOCATED_FIXED_SIZE) &&
				(mdl_pointer->MdlFlags & MDL_WRITE_OPERATION) &&
				(mdl_pointer->MdlFlags & MDL_PAGES_LOCKED) &&
				(mdl_pointer->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
				)
			{
				DBG_TRACE("MapMdl", " MmGetSystemAddressForMdlSafe() �� MDL �ṹ������ԭʼ KiServiceTable ӳ�䵽��һ���ں������ַ��MDL_MAPPED_TO_SYSTEM_VA��\r\n");
			}


			DbgPrint("MmGetSystemAddressForMdlSafe() ������Ȼ����ͨ��ԭʼ�� MDL ָ����� _MDL �ĵ�ַ:   %p\r\n", mdl_pointer);
			DbgPrint("Ҳ����ͨ�����ݵ� MDL ָ����� _MDL �ĵ�ַ:   %p���ⶼ˵�� MDL �ṹ��δ���ͷţ�\r\n", backup_mdl_ptr);

			
			pfn_array_follow_mdl = (PPFN_NUMBER)(mdl_pointer + 1);

			
			DbgPrint(" MDL �ṹ��ƫ�� %2x ��ַ����һ�� PFN ���飬�����洢�� MDL ���������⻺����ӳ�䵽������ҳ���\r\n", mdl_header_length);
			DbgPrint(" �� PFN �������ʼ��ַΪ��%p\r\n", pfn_array_follow_mdl);
			DbgPrint(" ��һ������ҳ���Ϊ��%p\r\n", *pfn_array_follow_mdl);
			

				/*��ӡǰ����÷��صĵ�ַ������� Mdl ��ԭʼ�� os_SSDT_ptr ӳ�䵽����һ���ں˵�ַ��������ˣ�
				�� kd.exe ��ת�� os_SSDT_ptr �� mapped_addr��Ӧ�õó���ͬ�����ݣ��������Ƕ�ָ�� SSDT��
ע�⣬MmProbeAndLockPages() ���û���� mdl �����Ļ������ڿɻ�ҳ���У����ҽ���������ǰ��˵����
 IoAllocateMdl() �����ʼ�� MDL ����� PFN ���飬����ֱ�Ӱ��� IoAllocateMdl() -> MmGetSystemAddressForMdlSafe()
 Ȼ���ӡ���� os_SSDT_ptr ӳ�䵽�ĵ�ַ�Ǵ���ģ�kd.exe �޷�ת����֤�������Ҫִ����������֮һ����ȷ���裺
1 �� IoAllocateMdl() -> MmProbeAndLockPages() -> MmGetSystemAddressForMdlSafe() �� os_SSDT_ptr ��ӳ�䵽��һ��
 �ں˵�ַ��mapped_addr ������ os_SSDT_ptr������ͬ��ָ�� SSDT

2�� IoAllocateMdl() -> MmBuildMdlForNonPagedPool() -> MmGetSystemAddressForMdlSafe()����Ϊ
MmBuildMdlForNonPagedPool() ���� Mdl ������ os_SSDT_ptr �Ѿ��ڲ��ɻ�ҳ���У������ᴴ��������ں˵�ַӳ�䣬
 ��� mapped_addr2 �͵��� os_SSDT_ptr

			����ǰ��Ϊ MapMdl() �ĵڶ������������ʵ�Σ����ɲ�ͬ�Ĵ�ӡ��Ϣ
			DbgPrint("the MmGetSystemAddressForMdlSafe() mapping OS SSDT pointer to :   %p",  mapped_addr);

	MmBuildMdlForNonPagedPool() �� MmProbeAndLockPages()��MmMapLockedPagesSpecifyCache() 
		����ͬʱʹ�ã�MmBuildMdlForNonPagedPool()
		�Ĳ��� mdl ������ IoAllocateMdl() �����ģ��������ɻ�ҳ�ص� mdl���ڴ˳����У�mdl ������ ETHREAD �ṹӦ��λ��
	���ɻ�ҳ���У��� MmGetSystemAddressForMdlSafe() ����һ���� MmBuildMdlForNonPagedPool() ������ MDL ������ġ�
		����������£�MmGetSystemAddressForMdlSafe() ����ֻ�Ƿ����ɸ� MDL �����Ļ���������ʼ�����ַ��
				MmBuildMdlForNonPagedPool(mdl);
				mapped_addr2 = MmGetSystemAddressForMdlSafe(mdl,  NormalPagePriority);

	ͨ��������Ĵ�ӡ�������һ���Ƚϣ����Լ�� MmBuildMdlForNonPagedPool() �Ƿ��Ϊ os_SSDT_ptr ���������ӳ�䣺
	��� mapped_addr2 �� mapped_addr ��ͬ���򲻻ᡣ
			����ǲ��ᣬMDL �����Ļ���������ʼ�����ַ�����̵߳� KTHREAD.ServiceTable ��Ա��ַ��
			DbgPrint("the MmGetSystemAddressForMdlSafe() ->MmBuildMdlForNonPagedPool()  mapping OS SSDT pointer to :   %p",  mapped_addr2);
			*/
				
			return mapped_addr;
				
				/*
				mapped_addr3 = MmMapLockedPagesSpecifyCache(mdl,
																									KernelMode,
																									MmNonCached,
																									NULL,
																									FALSE,
																									NormalPagePriority);

				DbgPrint("MmMapLockedPagesSpecifyCache() mapping the SSDT start from:   %p", mapped_addr3);
								if (mapped_addr3 == NULL) {
										DBG_TRACE("MapMdl", "..........all the way can't access mapped SSDT, give up!.........");
										MmUnlockPages(mdl);
										IoFreeMdl(mdl);
								}
								return  mapped_addr;
						}
						return  mapped_addr;
			}
		*/
		}

		except (STATUS_ACCESS_VIOLATION) {

			IoFreeMdl(mdl_pointer);
			return NULL;
		}
		
}



void  UnMapMdl(PMDL  mdl_pointer,  PVOID  baseaddr) {

	if (mdl_pointer != backup_mdl_ptr) {

		DBG_TRACE("UnMapMdl", ".......�Ƚ������� MDL ӳ���ҳ�棬Ȼ���ͷű��ݵ� MDL........");

		MmUnlockPages(backup_mdl_ptr);	// �����̵�Ч���ǣ��޷�ͨ��ӳ���ϵͳ��ַ������ KiServiceTable���� _MDL �ṹ�и��ֶ��ѷ����仯��
		IoFreeMdl(backup_mdl_ptr);		// �����̵�Ч���ǣ�MDL ָ�벻�ٳ��� _MDL �ṹ�ĵ�ַ


		if (backup_mdl_ptr == NULL) {

			DBG_TRACE("UnMapMdl", ".............����ҳ�棬�ͷű��� MDL ��ɣ�................");
		}

		return;
	}


	DBG_TRACE("UnMapMdl", ".........ԭʼ MDL δ���޸ģ�������ӳ���ҳ����ͷ���...........");
		
		// ���ǰ��ʹ�� MmBuildMdlForNonPagedPool() ���Ͳ���ִ������ǰ2������
		//MmUnmapLockedPages(baseaddr,  mdl);
	MmUnlockPages(mdl_pointer);
	IoFreeMdl(mdl_pointer);

	if (mdl_pointer == NULL) {

		DBG_TRACE("UnMapMdl", ".............����ҳ�棬�ͷ�ԭʼ MDL ��ɣ�................");
	}

	return;
}
