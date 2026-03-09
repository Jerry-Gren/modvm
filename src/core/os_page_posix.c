/* src/arch/x86_64/kvm_os_page.c (Example for Linux/KVM) */
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include <modvm/os_page.h>
#include <modvm/err.h>

size_t os_get_page_size(void)
{
	return (size_t)sysconf(_SC_PAGESIZE);
}

void *os_alloc_pages(size_t size)
{
	void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (ptr == MAP_FAILED)
		return ERR_PTR(-ENOMEM);

	return ptr;
}

void os_free_pages(void *ptr, size_t size)
{
	/* Safely ignore NULL pointers and explicitly encoded error pointers */
	if (ptr && !IS_ERR(ptr))
		munmap(ptr, size);
}