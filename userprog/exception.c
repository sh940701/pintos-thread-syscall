#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

/* 사용자 프로그램에 의해 발생할 수 있는 인터럽트에 대한 핸들러를 등록합니다.

   실제 Unix와 유사한 운영 체제에서는 대부분의 이러한 인터럽트가
   시그널 형태로 사용자 프로세스로 전달됩니다. [SV-386] 3-24 및 3-25에서 설명한 것처럼,
   그러나 우리는 시그널을 구현하지 않습니다. 대신, 그냥 사용자 프로세스를 종료합니다.

   페이지 폴트는 예외입니다. 여기서는 다른 예외들과 동일하게 처리되지만,
   가상 메모리를 구현하려면 이 부분을 변경해야 합니다.

   각 예외에 대한 설명은 [IA32-v3a] 섹션 5.15 "예외 및 인터럽트 참조"를 참조하십시오. */
void exception_init(void)
{
	/* 이러한 예외들은 사용자 프로그램에 의해 명시적으로 발생할 수 있습니다.
	   예를 들어, INT, INT3, INTO 및 BOUND 명령을 통해.
	   따라서 DPL==3로 설정하여 사용자 프로그램이 이러한 명령을 통해
	   이들을 호출할 수 있도록 합니다. */
	intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int(5, 3, INTR_ON, kill,
					  "#BR BOUND Range Exceeded Exception");

	/* 이러한 예외들은 DPL==0으로, 사용자 프로세스가
	   INT 명령을 통해 이를 호출하는 것을 방지합니다. 여전히
	   간접적으로 발생할 수 있습니다. 예를 들어 #DE는 나누기로
	   인해 발생할 수 있습니다. */
	intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int(7, 0, INTR_ON, kill,
					  "#NM Device Not Available Exception");
	intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int(19, 0, INTR_ON, kill,
					  "#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트를 켜고 처리할 수 있습니다.
	   페이지 폴트의 경우 인터럽트를 비활성화해야 합니다. 왜냐하면
	   폴트 주소가 CR2에 저장되어 보존되어야 하기 때문입니다. */
	intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력합니다. */
void exception_print_stats(void)
{
	printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스에 의해 발생한 것으로 추정되는 예외를 처리하는 핸들러입니다. */
static void
kill(struct intr_frame *f)
{
	/* 이 인터럽트는 사용자 프로세스에 의해 발생한 것으로 추정됩니다.
	   예를 들어, 프로세스가 매핑되지 않은 가상 메모리에 액세스하려고 시도했을 수 있습니다
	   (페이지 폴트). 현재로서는 단순히 사용자 프로세스를 종료합니다.
	   나중에는 페이지 폴트를 커널에서 처리해야 합니다.
	   실제 Unix와 유사한 운영 체제에서는 대부분의 예외를 신호로 다시 프로세스로 전달하지만,
	   우리는 그러한 것을 구현하지 않습니다. */

	/* 인터럽트 프레임의 코드 세그먼트 값은 예외가 발생한 위치를 알려줍니다. */
	switch (f->cs)
	{
	case SEL_UCSEG:
		/* 사용자의 코드 세그먼트이므로, 우리가 예상한 대로 사용자 예외입니다.
		   사용자 프로세스를 종료합니다. */
		printf("%s: dying due to interrupt %#04llx (%s).\n",
			   thread_name(), f->vec_no, intr_name(f->vec_no));
		intr_dump_frame(f);
		thread_exit();

	case SEL_KCSEG:
		/* 커널의 코드 세그먼트는 커널 버그를 나타냅니다.
		   커널 코드는 예외를 발생시키지 않아야 합니다. (페이지 폴트는 커널 예외를 일으킬 수 있지만,
		   여기에 도달해서는 안 됩니다.) 커널을 패닉 상태로 만들어서 주장합니다. */
		intr_dump_frame(f);
		PANIC("Kernel bug - unexpected interrupt in kernel");

	default:
		/* 다른 코드 세그먼트? 발생해서는 안 됩니다. 커널을 패닉 상태로 만듭니다. */
		printf("Interrupt %#04llx (%s) in unknown segment %04x\n",
			   f->vec_no, intr_name(f->vec_no), f->cs);
		thread_exit();
	}
}

/* 페이지 폴트 핸들러입니다. 이것은 가상 메모리를 구현하기 위해 채워야 하는 뼈대입니다.
   프로젝트 2의 일부 솔루션은 또한 이 코드를 수정해야 합니다.

   입구에서, 폴트가 발생한 주소는 CR2 (Control Register 2)에 있으며
   폴트에 대한 정보는 F의 error_code 멤버에 설명된 대로 포맷으로 있습니다.
   여기에는 PF_* 매크로에 대한 자세한 정보가 있습니다.
   [IA32-v3a] 섹션 5.15 "예외 및 인터럽트 참조"의 "Interrupt 14--Page Fault Exception (#PF)"에서.
*/

// page_fault 로 들어오는 것들은, system call 을 실행하기 이전에 CPU -> MMU 레벨에서 현재 MMU 에 mapping 되어있지 않은 주소라는 것을 판단하고 넘어온 상황이다.
// 그러므로 우리가 해야 할 것은, 해당 fault 를 발생시킨 주소가 정말 접근해서는 안 되는 주소인지, 접근 가능하지만 아직 할당이 안 된 페이지인지 파악하고
// 이에 대한 처리를 해주는 것이다.
static void page_fault(struct intr_frame *f)
{
	bool not_present; /* 참: 존재하지 않는 페이지, 거짓: 읽기 전용 페이지. */
	bool write;		  /* 참: 쓰기 접근, 거짓: 읽기 접근. */
	bool user;		  /* 참: 사용자에 의한 접근, 거짓: 커널에 의한 접근. */
	void *fault_addr; /* 오류 주소. */

	/* 페이지 폴트를 발생시킨 가상 주소를 얻습니다.
	   이 주소는 폴트를 발생시킨 가상 주소입니다.
	   코드나 데이터를 가리킬 수 있습니다.
	   이것은 폴트를 발생시킨 명령어의 주소가 아닙니다 (그것은 f->rip입니다). */

	fault_addr = (void *)rcr2();

	/* 인터럽트를 다시 활성화합니다 (CR2가 변경되기 전에 읽을 수 있도록 인터럽트가 꺼져 있었습니다). */
	intr_enable();

	/* 원인을 결정합니다. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* 프로젝트 3 이후로. */
	if (vm_try_handle_fault(f, fault_addr, user, write, not_present))
		return;
#endif

	/* 페이지 폴트를 카운트합니다. */
	page_fault_cnt++;

	if ((!not_present && write) || (fault_addr < 0x400000 || fault_addr >= USER_STACK)) {
		exit(-1);
	} else {
		// page allocation 으로 변경
		kill(f);
	}

	/* 폴트가 진짜 폴트인 경우 정보를 표시하고 종료합니다. */
	// printf("Page fault at %p: %s error %s page in %s context.\n",
	// 	   fault_addr,
	// 	   not_present ? "not present" : "rights violation",
	// 	   write ? "writing" : "reading",
	// 	   user ? "user" : "kernel");

}
